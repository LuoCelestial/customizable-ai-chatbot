#include "assistant_client.h"
#include "assistant_setup.h"
#include "assistant_state.h"
#include "assistant_ui.h"
#include "assistant_tts.h"
#include "assistant_weather.h"
#include "assistant_wallpaper.h"
#include "assistant_wifi_policy.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define WIFI_CONNECTED BIT0
#define WIFI_RETRY BIT1
#define WIFI_FAILED BIT2
#define EVENT_DEPTH 16
static const char *TAG = "assistant";
static assistant_wifi_policy_t s_wifi_policy;
typedef enum { EVENT_BUTTON, EVENT_STAGE, EVENT_DONE, EVENT_SAVED } event_kind_t;
typedef struct {
    event_kind_t kind;
    bsp_btn_t button;
    bsp_btn_ev_t button_event;
    assistant_client_stage_t stage;
    esp_err_t error;
    bool clear;
} app_event_t;
typedef enum { WORK_VOICE, WORK_CLEAR } work_t;
static QueueHandle_t s_events, s_work;
static EventGroupHandle_t s_network;
static bool s_weather_worker_started = false;

static void weather_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 40; ++i) {
        if (time(NULL) > 1700000000) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while (1) {
        if (xEventGroupGetBits(s_network) & WIFI_CONNECTED) {
            assistant_weather_fetch(NULL, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000));
    }
}

static assistant_settings_t s_settings;
static assistant_client_result_t s_result;
static voice_state_t s_state = VOICE_IDLE;
static atomic_bool s_stop_recording;
static atomic_bool s_abort_tts;
static atomic_bool s_is_playing_tts;
static bool s_audio_ready;
static bool s_memory_ready;
static bool s_services_ready;
static bool s_wifi_started;
static bool s_battery_ready;
static bool s_record_format_ready;
static bool s_sntp_started;
static unsigned s_recorded_bytes;
static unsigned s_last_record_second;
static esp_partition_mmap_handle_t s_wallpaper_handle;
static bool s_wallpaper_mapped;

static void status(const char *title, const char *detail)
{
    if (bsp_lvgl_lock(500)) {
        assistant_ui_status(title, detail);
        bsp_lvgl_unlock();
    }
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    const app_event_t item = {.kind = EVENT_BUTTON, .button = button, .button_event = event};
    if (s_events) (void)xQueueSend(s_events, &item, 0);
}

static void on_saved(void)
{
    const app_event_t event = {.kind = EVENT_SAVED};
    (void)xQueueSend(s_events, &event, portMAX_DELAY);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (esp_wifi_connect() != ESP_OK) xEventGroupSetBits(s_network, WIFI_RETRY);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_network, WIFI_CONNECTED);
        xEventGroupSetBits(s_network, WIFI_RETRY | WIFI_FAILED);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_network, WIFI_RETRY | WIFI_FAILED);
        xEventGroupSetBits(s_network, WIFI_CONNECTED);
        if (!s_sntp_started) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "ntp.aliyun.com");
            esp_sntp_setservername(1, "pool.ntp.org");
            esp_sntp_init();
            s_sntp_started = true;
        }
        if (!s_weather_worker_started) {
            s_weather_worker_started = true;
            xTaskCreate(weather_worker, "weather_worker", 4096, NULL, 1, NULL);
        }
    }
}

static esp_err_t station_configure(bool backup)
{
    const char *ssid = backup ? s_settings.backup.ssid : s_settings.ssid;
    const char *password = backup ? s_settings.backup.password : s_settings.wifi_password;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

static esp_err_t station_start(void)
{
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) { esp_netif_destroy_default_wifi(netif); return err; }
    esp_event_handler_instance_t wifi_handler = NULL, ip_handler = NULL;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &wifi_handler);
    if (err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, &ip_handler);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = station_configure(false);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        if (wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
        if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        esp_wifi_deinit(); esp_netif_destroy_default_wifi(netif);
    }
    return err;
}

static bool needs_clock(void)
{
    return s_settings.client.mode == ASSISTANT_MODE_DIRECT ||
           strncmp(s_settings.client.gateway_url, "https://", 8) == 0;
}

static esp_err_t prepare_network(void)
{
    if (!(xEventGroupGetBits(s_network) & WIFI_CONNECTED)) return ESP_ERR_INVALID_STATE;
    if (!needs_clock()) return ESP_OK;
    if (!s_sntp_started) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.cloudflare.com");
        esp_sntp_init();
        s_sntp_started = true;
    }
    // Certificate validation always remains enabled. Wait for a plausible epoch.
    for (unsigned i = 0; i < 150; ++i) {
        if (time(NULL) > 1735689600) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static void on_stage(void *context, assistant_client_stage_t stage)
{
    (void)context;
    const app_event_t event = {.kind = EVENT_STAGE, .stage = stage};
    (void)xQueueSend(s_events, &event, portMAX_DELAY);
}

static esp_err_t read_audio(void *context, void *pcm, size_t bytes)
{
    (void)context;
    if (atomic_load(&s_stop_recording)) { memset(pcm, 0, bytes); return ESP_OK; }
    if (!s_record_format_ready) {
        esp_err_t err = bsp_audio_wake();
        if (err == ESP_OK) err = bsp_audio_set_format(ASSISTANT_SAMPLE_RATE, 16, 1);
        if (err != ESP_OK) return err;
        s_record_format_ready = true;
    }
    esp_err_t err = bsp_audio_read(pcm, bytes);
    if (err != ESP_OK) return err;
    s_recorded_bytes += bytes;
    unsigned seconds = s_recorded_bytes / (ASSISTANT_SAMPLE_RATE * 2);
    if (seconds != s_last_record_second) {
        s_last_record_second = seconds;
        if (bsp_lvgl_lock(100)) {
            assistant_ui_recording(seconds, ASSISTANT_RECORD_SECONDS);
            bsp_lvgl_unlock();
        }
    }
    return ESP_OK;
}

static void worker(void *arg)
{
    (void)arg;
    work_t work;
    for (;;) {
        if (xQueueReceive(s_work, &work, portMAX_DELAY) != pdTRUE) continue;
        memset(&s_result, 0, sizeof(s_result));
        esp_err_t err = ESP_OK;
        // Local memory clearing also works while Wi-Fi is offline.
        if (work != WORK_CLEAR || s_settings.client.mode == ASSISTANT_MODE_GATEWAY) {
            err = prepare_network();
            if (err != ESP_OK) strcpy(s_result.error, err == ESP_ERR_TIMEOUT ? "Clock sync failed. Retry or check NTP access." : "Wi-Fi is offline. Check router or reopen setup.");
        }
        if (err == ESP_OK && work == WORK_CLEAR)
            err = assistant_client_clear_memory(&s_settings.client, &s_result);
        else if (err == ESP_OK) {
            if (!s_audio_ready) {
                err = bsp_audio_init();
                s_audio_ready = err == ESP_OK;
                if (s_audio_ready) bsp_audio_set_volume(s_settings.client.volume);
            }
            if (err == ESP_OK) {
                s_recorded_bytes = 0;
                s_last_record_second = 0;
                s_record_format_ready = false;
                err = assistant_client_voice(&s_settings.client, ASSISTANT_PCM_BYTES,
                                             read_audio, NULL, on_stage, NULL, &s_result);
                if (s_record_format_ready && bsp_audio_sleep() != ESP_OK)
                    ESP_LOGW(TAG, "Microphone suspend failed");
            } else strcpy(s_result.error, "Microphone initialization failed.");
        }
        ESP_LOGI(TAG, "Request complete: status=%s heap=%u largest=%u", esp_err_to_name(err),
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        const app_event_t event = {.kind = EVENT_DONE, .error = err, .clear = work == WORK_CLEAR};
        (void)xQueueSend(s_events, &event, portMAX_DELAY);
        if (err == ESP_OK && work != WORK_CLEAR && s_result.reply[0] &&
            s_settings.client.tts_enabled &&
            s_settings.client.tencent_secret_id[0] && s_settings.client.asr_key[0]) {
            vTaskDelay(pdMS_TO_TICKS(150));
            status("正在播放", "按确定键可停止播放");
            atomic_store(&s_abort_tts, false);
            atomic_store(&s_is_playing_tts, true);
            (void)bsp_audio_wake();
            bsp_audio_set_volume(s_settings.client.volume);
            char tts_detail[96];
            esp_err_t tts_err = assistant_tts_speak(&s_settings.client, s_result.reply,
                                                  &s_abort_tts, tts_detail, sizeof(tts_detail));
            (void)bsp_audio_sleep();
            atomic_store(&s_is_playing_tts, false);
            if (tts_err != ESP_OK && !atomic_load(&s_abort_tts)) {
                status("TTS未成功", tts_detail);
            } else {
                status("回答完成", "已保存记忆，按确定键继续");
            }
        }
    }
}

static void handle_button(const app_event_t *event)
{
    if (atomic_load(&s_is_playing_tts)) {
        if (event->button == BSP_BTN_OK && event->button_event == BSP_BTN_CLICK) {
            atomic_store(&s_abort_tts, true);
            return;
        }
        if (event->button_event == BSP_BTN_CLICK) {
            if (event->button == BSP_BTN_UP) {
                if (s_settings.client.volume <= 90) s_settings.client.volume += 10;
                else s_settings.client.volume = 100;
            } else if (event->button == BSP_BTN_DOWN) {
                if (s_settings.client.volume >= 10) s_settings.client.volume -= 10;
                else s_settings.client.volume = 0;
            }
            bsp_audio_set_volume(s_settings.client.volume);
            char vol_str[32];
            snprintf(vol_str, sizeof(vol_str), "当前音量: %u%%", (unsigned)s_settings.client.volume);
            status("调节音量", vol_str);
            (void)assistant_settings_save(&s_settings);
            return;
        }
    }
    voice_input_t input;
    if (event->button_event == BSP_BTN_LONG && event->button == BSP_BTN_OK) input = VOICE_SETTINGS;
    else if (event->button_event == BSP_BTN_LONG && event->button == BSP_BTN_DOWN) input = VOICE_FORGET;
    else if (event->button_event != BSP_BTN_CLICK) return;
    else if (event->button == BSP_BTN_OK) input = VOICE_OK;
    else if (event->button == BSP_BTN_UP) input = VOICE_UP;
    else input = VOICE_DOWN;
    voice_action_t action = voice_handle_input(&s_state, input);
    if (action == VOICE_OPEN_SETTINGS) {
        if (assistant_settings_request_setup() == ESP_OK) esp_restart();
        s_state = VOICE_IDLE;
        status("设置失败", "无法保存设置入口，请重试");
    } else if (action == VOICE_STOP) {
        atomic_store(&s_stop_recording, true);
        status("正在识别", "录音已结束，请稍候");
    } else if (action == VOICE_START || action == VOICE_CLEAR) {
        if (!s_services_ready) {
            s_state = VOICE_IDLE;
            status("请完成服务设置", "长按确定配置 API");
            return;
        }
        if ((!s_memory_ready && action != VOICE_CLEAR) || !s_work) {
            s_state = VOICE_IDLE;
            status("存储不可用", "原有记录已保留，请检查 NVS");
            return;
        }
        atomic_store(&s_stop_recording, false);
        if (bsp_lvgl_lock(500)) {
            assistant_ui_confirm_clear(false);
            assistant_ui_status(action == VOICE_START ? "连接服务" : "清除记忆", "请稍候");
            bsp_lvgl_unlock();
        }
        const work_t work = action == VOICE_START ? WORK_VOICE : WORK_CLEAR;
        if (xQueueSend(s_work, &work, 0) != pdTRUE) {
            s_state = VOICE_IDLE;
            status("暂时忙碌", "请稍后重试");
        }
    } else if (bsp_lvgl_lock(500)) {
        if (action == VOICE_SCROLL_UP) assistant_ui_scroll(-1);
        if (action == VOICE_SCROLL_DOWN) assistant_ui_scroll(1);
        if (action == VOICE_SHOW_CLEAR) assistant_ui_confirm_clear(true);
        if (action == VOICE_CANCEL_CLEAR) assistant_ui_confirm_clear(false);
        bsp_lvgl_unlock();
    }
}

void app_main(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display unavailable"); return;
    }
    assistant_settings_defaults(&s_settings);
    esp_err_t err = nvs_flash_init();
    bool nvs_ready = err == ESP_OK;
    bool wifi_ready = nvs_ready && assistant_settings_load(&s_settings) == ESP_OK;
    const uint8_t *wallpaper_pixels = NULL;
    s_wallpaper_mapped = assistant_wallpaper_map(&wallpaper_pixels, &s_wallpaper_handle) == ESP_OK;
    if (!bsp_lvgl_lock(1000)) {
        if (s_wallpaper_mapped) {
            assistant_wallpaper_unmap(s_wallpaper_handle);
            s_wallpaper_mapped = false;
        }
        return;
    }
    bool ui_ready = assistant_ui_init(wallpaper_pixels, &s_settings.client.profile);
    bsp_lvgl_unlock();
    bsp_display_backlight(75);
    if (!ui_ready) {
        if (s_wallpaper_mapped) {
            assistant_wallpaper_unmap(s_wallpaper_handle);
            s_wallpaper_mapped = false;
        }
        ESP_LOGE(TAG, "UI initialization or font audit failed"); return;
    }
    s_events = xQueueCreate(EVENT_DEPTH, sizeof(app_event_t));
    s_network = xEventGroupCreate();
    if (!s_events || !s_network) { status("内存不足", "请重新启动设备"); return; }
    if (bsp_button_init(on_key, NULL) != ESP_OK) { status("按键不可用", "请检查设备连接"); return; }
    s_battery_ready = bsp_battery_init() == ESP_OK;
    if (!nvs_ready) { status("存储不可用", "NVS 初始化失败，未擦除数据"); return; }
    err = esp_netif_init();
    if (err == ESP_OK) err = esp_event_loop_create_default();
    if (err != ESP_OK) { status("网络不可用", "请重新启动设备"); return; }
    s_services_ready = wifi_ready && assistant_settings_valid(&s_settings);
    bool setup_requested = assistant_settings_take_setup_request();
    if (!wifi_ready || setup_requested) {
        char ssid[33], password[17];
        s_state = VOICE_SETUP;
        err = assistant_setup_start(ssid, password, on_saved);
        if (err == ESP_OK && bsp_lvgl_lock(500)) {
            assistant_ui_setup(ssid, password);
            bsp_lvgl_unlock();
        } else status("设置不可用", "请重新启动设备");
    } else {
        if (bsp_lvgl_lock(500)) {
            if (s_services_ready) assistant_ui_mode(s_settings.client.mode == ASSISTANT_MODE_DIRECT);
            else {
                char detail[128];
                snprintf(detail, sizeof(detail), "联网后长按 OK，设置%s和语音识别服务。",
                         s_settings.client.profile.name);
                assistant_ui_conversation("无线网络已保存", detail);
            }
            bsp_lvgl_unlock();
        }
        err = assistant_client_init();
        s_memory_ready = err == ESP_OK || s_settings.client.mode == ASSISTANT_MODE_GATEWAY;
        if (!s_memory_ready) status("记忆读取失败", "长按下键清除损坏的记忆后重试");
        err = station_start();
        s_wifi_started = err == ESP_OK;
        if (err != ESP_OK) status("网络启动失败", "长按确定键进入设置");
        else status("连接无线网络", "长按确定键可重新设置");
        if (s_services_ready) s_work = xQueueCreate(1, sizeof(work_t));
        if (s_services_ready && (!s_work || xTaskCreate(worker, "voice_worker", 12288, NULL, 4, NULL) != pdPASS)) {
            if (s_work) { vQueueDelete(s_work); s_work = NULL; }
            status("内存不足", "无法启动语音服务");
        }
    }
    TickType_t last_poll = xTaskGetTickCount();
    TickType_t connect_since = last_poll;
    bool connect_timeout_shown = false;
    bool was_connected = false;
    for (;;) {
        app_event_t event;
        if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (event.kind == EVENT_SAVED) { vTaskDelay(pdMS_TO_TICKS(600)); esp_restart(); }
            if (event.kind == EVENT_BUTTON) handle_button(&event);
            if (event.kind == EVENT_STAGE) {
                if (event.stage == ASSISTANT_STAGE_RECORDING) {
                    s_state = VOICE_RECORDING;
                    if (bsp_lvgl_lock(500)) { assistant_ui_recording(0, ASSISTANT_RECORD_SECONDS); bsp_lvgl_unlock(); }
                } else {
                    s_state = VOICE_BUSY;
                    if (event.stage == ASSISTANT_STAGE_CONNECTING) status("连接服务", "建立安全连接，请稍候");
                    if (event.stage == ASSISTANT_STAGE_TRANSCRIBING) status("正在识别", "语音转文字，请稍候");
                    if (event.stage == ASSISTANT_STAGE_THINKING) {
                        char detail[96];
                        snprintf(detail, sizeof(detail), "%s正在回答", s_settings.client.profile.name);
                        status("正在思考", detail);
                    }
                }
            }
            if (event.kind == EVENT_DONE) {
                s_state = VOICE_IDLE;
                if (bsp_lvgl_lock(500)) {
                    if (event.error == ESP_OK) {
                        if (event.clear) {
                            s_memory_ready = true;
                            assistant_ui_conversation("", "当前模式的记忆已清除。");
                            assistant_ui_status("已清除", "按确定键开始新对话");
                        } else {
                            assistant_ui_conversation(s_result.transcript, s_result.reply);
                            assistant_ui_status("回答完成", !s_result.memory_saved ? "记忆保存失败；回答仅本次有效" :
                                                s_result.reply_truncated ? "回答过长，已截断；请追问" : "已保存记忆，按确定键继续");
                        }
                    } else assistant_ui_status("请求失败", s_result.error[0] ? s_result.error : "请检查配置或网络后重试");
                    bsp_lvgl_unlock();
                }
            }
        }
        TickType_t now = xTaskGetTickCount();
        static TickType_t s_last_clock = 0;
        if ((TickType_t)(now - s_last_clock) >= pdMS_TO_TICKS(1000)) {
            s_last_clock = now;
            char t_buf[16];
            if (assistant_weather_get_time_str(t_buf, sizeof(t_buf))) {
                if (bsp_lvgl_lock(100)) {
                    assistant_ui_time_weather(t_buf, assistant_weather_get_cached());
                    bsp_lvgl_unlock();
                }
            }
        }
        if ((TickType_t)(now - last_poll) >= pdMS_TO_TICKS(5000)) {
            last_poll = now;
            int battery = s_battery_ready ? bsp_battery_soc() : -1;
            if (bsp_lvgl_lock(200)) { assistant_ui_battery(battery); bsp_lvgl_unlock(); }
            if (s_state != VOICE_SETUP && s_wifi_started) {
                EventBits_t bits = xEventGroupGetBits(s_network);
                bool connected = (bits & WIFI_CONNECTED) != 0;
                bool target_changed = false;
                if (connected) assistant_wifi_connected(&s_wifi_policy);
                else if (bits & WIFI_FAILED) {
                    xEventGroupClearBits(s_network, WIFI_FAILED);
                    assistant_wifi_policy_t next = s_wifi_policy;
                    target_changed = assistant_wifi_failed(&next, s_settings.backup.ssid[0] != '\0');
                    if (!target_changed || station_configure(next.using_backup) == ESP_OK) {
                        s_wifi_policy = next;
                        if (target_changed)
                            ESP_LOGI(TAG, "Wi-Fi target switched to %s", next.using_backup ? "backup" : "primary");
                    } else {
                        target_changed = false;
                        ESP_LOGW(TAG, "Wi-Fi target change failed; retaining previous configuration");
                    }
                }
                if (connected != was_connected || target_changed) {
                    connect_since = now;
                    connect_timeout_shown = false;
                    if (s_state == VOICE_IDLE && (!s_services_ready || (s_memory_ready && s_work))) {
                        char title[64], detail[96];
                        if (connected) {
                            snprintf(title, sizeof(title), "%s", s_services_ready ? "准备就绪" : "网络已连接");
                            snprintf(detail, sizeof(detail), "%s已连接，按确定键说话", s_wifi_policy.using_backup ? s_settings.backup.ssid : s_settings.ssid);
                        } else {
                            snprintf(title, sizeof(title), "%s", s_wifi_policy.using_backup ? "连接备用网络" : "无线网络断开");
                            snprintf(detail, sizeof(detail), "正在重连 %s，长按确定设置", s_wifi_policy.using_backup ? s_settings.backup.ssid : s_settings.ssid);
                        }
                        status(title, detail);
                    }
                }
                was_connected = connected;
                if (!connected && !connect_timeout_shown &&
                    (TickType_t)(now - connect_since) >= pdMS_TO_TICKS(30000) && s_state == VOICE_IDLE) {
                    status("无线连接超时", "继续重试，长按确定键设置");
                    connect_timeout_shown = true;
                }
                if (!connected && (bits & WIFI_RETRY)) {
                    xEventGroupClearBits(s_network, WIFI_RETRY);
                    if (esp_wifi_connect() != ESP_OK) xEventGroupSetBits(s_network, WIFI_RETRY);
                }
            }
        }
    }
}

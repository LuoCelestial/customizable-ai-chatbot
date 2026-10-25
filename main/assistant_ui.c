#include "assistant_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_pins.h"
#include "lvgl.h"

LV_FONT_DECLARE(passport_noto_sans_sc_16);
LV_FONT_DECLARE(passport_kaomoji_16);
/* Writable, application-lifetime descriptor; generated font data stays in Flash. */
static lv_font_t s_text_font;

#define UI_PANEL 0x102A4C
#define UI_TEXT 0xFFFFFF
#define UI_WARNING 0xFFD166
#define UI_TEXT_BYTES 3072

static lv_obj_t *s_screen;
static lv_obj_t *s_mode;
static lv_obj_t *s_battery;
static lv_obj_t *s_state;
static lv_obj_t *s_detail;
static lv_obj_t *s_progress;
static lv_obj_t *s_conversation;
static lv_obj_t *s_transcript_title;
static lv_obj_t *s_transcript;
static lv_obj_t *s_reply_title;
static lv_obj_t *s_reply;
static lv_obj_t *s_notice;
static lv_obj_t *s_hint;
static bool s_setup;
static bool s_confirm;
static bool s_content_warning;
static bool s_status_warning;
static lv_image_dsc_t s_wallpaper_dsc;
static uint32_t s_primary = ASSISTANT_DEFAULT_THEME;
static uint32_t s_primary_end = 0x7A5CFA;
static uint32_t s_accent = ASSISTANT_DEFAULT_THEME;
static uint32_t s_muted = 0xC9F1FF;
static uint32_t s_secondary = 0xFF66B3;
static char s_name[ASSISTANT_NAME_BYTES] = ASSISTANT_DEFAULT_NAME;
/* One shared bounded scratch buffer, serialized by the caller's LVGL lock. */
static char s_text[UI_TEXT_BYTES + 1];

static uint32_t mix(uint32_t first, uint32_t second, unsigned second_part)
{
    unsigned first_part = 255 - second_part;
    unsigned r = (((first >> 16) & 255) * first_part + ((second >> 16) & 255) * second_part) / 255;
    unsigned g = (((first >> 8) & 255) * first_part + ((second >> 8) & 255) * second_part) / 255;
    unsigned b = ((first & 255) * first_part + (second & 255) * second_part) / 255;
    return (r << 16) | (g << 8) | b;
}

static void set_theme(const assistant_profile_t *profile)
{
    assistant_profile_t fallback;
    if (!assistant_profile_valid(profile)) {
        assistant_profile_defaults(&fallback);
        profile = &fallback;
    }
    s_primary = profile->theme_color;
    s_primary_end = mix(s_primary, 0x7A5CFA, 112);
    s_muted = mix(s_primary, 0xFFFFFF, 190);
    s_secondary = mix(s_primary, 0xFF4FA8, 164);
    unsigned luminance = ((s_primary >> 16) & 255) * 54 + ((s_primary >> 8) & 255) * 183 +
                         (s_primary & 255) * 19;
    s_accent = luminance < 64 * 256 ? mix(s_primary, 0xFFFFFF, 112) : s_primary;
    memcpy(s_name, profile->name, sizeof(s_name));
    s_name[sizeof(s_name) - 1] = '\0';
}

static bool has_glyph(uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph = {0};
    return lv_font_get_glyph_dsc(&s_text_font, &glyph, codepoint, 0)
           && !glyph.is_placeholder;
}

/* Invalid UTF-8 consumes one byte and becomes an explicit replacement. */
static size_t decode(const unsigned char *p, uint32_t *codepoint)
{
    if (p[0] < 0x80) { *codepoint = p[0]; return 1; }
    size_t count = p[0] >= 0xC2 && p[0] <= 0xDF ? 2 :
                   p[0] >= 0xE0 && p[0] <= 0xEF ? 3 :
                   p[0] >= 0xF0 && p[0] <= 0xF4 ? 4 : 0;
    if (!count) { *codepoint = 0xFFFD; return 1; }
    uint32_t cp = p[0] & (0x7Fu >> count);
    for (size_t i = 1; i < count; ++i) {
        if (!p[i] || (p[i] & 0xC0) != 0x80) { *codepoint = 0xFFFD; return 1; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) ||
        (count == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp > 0x10FFFF) { *codepoint = 0xFFFD; return 1; }
    *codepoint = cp;
    return count;
}

static bool set_text(lv_obj_t *label, const char *text)
{
    if (!label) return false;
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    size_t used = 0;
    bool warning = false;
    while (*p) {
        uint32_t cp;
        size_t count = decode(p, &cp);
        if (cp == 0x0D) { p += count; continue; }
        if (cp == 0x2103) { /* deg C -> degree + C */
            if (used + 3 <= UI_TEXT_BYTES) {
                s_text[used++] = 0xC2; s_text[used++] = 0xB0;
                s_text[used++] = 'C';
            }
            p += count;
            continue;
        }
        if (cp == 0x2109) { /* deg F -> degree + F */
            if (used + 3 <= UI_TEXT_BYTES) {
                s_text[used++] = 0xC2; s_text[used++] = 0xB0;
                s_text[used++] = 'F';
            }
            p += count;
            continue;
        }
        if (cp == 0x3000) { /* fullwidth space -> space */
            if (used + 1 <= UI_TEXT_BYTES) s_text[used++] = ' ';
            p += count;
            continue;
        }
        if (cp == 0x2022 || cp == 0x25CF || cp == 0x25CB) { /* bullet -> middle dot */
            if (used + 2 <= UI_TEXT_BYTES) {
                s_text[used++] = 0xC2; s_text[used++] = 0xB7;
            }
            p += count;
            continue;
        }
        if (cp == '\t') {
            if (used == UI_TEXT_BYTES) { warning = true; break; }
            s_text[used++] = ' ';
        } else if (cp == '\n' || has_glyph(cp)) {
            if (used + count > UI_TEXT_BYTES) { warning = true; break; }
            memcpy(s_text + used, p, count);
            used += count;
        } else {
            if (used == UI_TEXT_BYTES) { warning = true; break; }
            s_text[used++] = '?';
            warning = true;
        }
        p += count;
    }
    s_text[used] = '\0';
    lv_label_set_text(label, s_text);
    return warning;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int width, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    if (!obj) return NULL;
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    /* Every Chinese/dynamic label explicitly selects the generated font. */
    lv_obj_set_style_text_font(obj, &s_text_font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(obj, 3, 0);
    set_text(obj, text);
    return obj;
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) return NULL;
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_PANEL), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(s_accent), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_40, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void notice(void)
{
    if (s_content_warning || s_status_warning) lv_obj_remove_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_notice, LV_OBJ_FLAG_HIDDEN);
}

static void hint(void)
{
    set_text(s_hint, s_setup ? "保存后设备自动重启" : s_confirm ?
             "OK 确认清除\n上 / 下 取消" :
             "OK 说话  /  上下翻页\n长按 OK 设置 · 长按下清除");
}

bool assistant_ui_init(const uint8_t *wallpaper_pixels, const assistant_profile_t *profile)
{
    if (s_screen) return true;
    set_theme(profile);
    s_text_font = passport_noto_sans_sc_16;
    s_text_font.fallback = &passport_kaomoji_16;
    if (s_text_font.line_height < passport_kaomoji_16.line_height)
        s_text_font.line_height = passport_kaomoji_16.line_height;
    /* Positive and negative probes catch wrong/missing font linkage. Detailed
     * inventory verification is also provided with the generated font asset. */
    if (!has_glyph(0x4E2D) || !has_glyph(0x6587) || !has_glyph(0xFF0C) ||
        !has_glyph(0x3002) || !has_glyph(0x03C9) || !has_glyph(0x2267) ||
        !has_glyph(0x3064) || !has_glyph('?') || has_glyph(0x1F642)) return false;

    s_screen = lv_obj_create(NULL);
    if (!s_screen) return false;
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, BSP_LCD_W, BSP_LCD_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(s_primary), 0);
    lv_obj_set_style_bg_grad_color(s_screen, lv_color_hex(s_primary_end), 0);
    lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (wallpaper_pixels) {
        memset(&s_wallpaper_dsc, 0, sizeof(s_wallpaper_dsc));
        s_wallpaper_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_wallpaper_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_wallpaper_dsc.header.w = BSP_LCD_W;
        s_wallpaper_dsc.header.h = BSP_LCD_H;
        s_wallpaper_dsc.header.stride = BSP_LCD_W * sizeof(uint16_t);
        s_wallpaper_dsc.data_size = BSP_LCD_W * BSP_LCD_H * sizeof(uint16_t);
        s_wallpaper_dsc.data = wallpaper_pixels;
        lv_obj_t *wallpaper = lv_image_create(s_screen);
        if (wallpaper) {
            lv_image_set_src(wallpaper, &s_wallpaper_dsc);
            lv_obj_set_pos(wallpaper, 0, 0);
            lv_obj_set_style_image_opa(wallpaper, LV_OPA_70, 0);
        }
    }

    lv_obj_t *title = label(s_screen, s_name, 146, UI_TEXT);
    s_battery = label(s_screen, "--%", 48, s_muted);
    s_mode = label(s_screen, "语音伙伴 · 有记忆的对话", 208, s_muted);
    lv_obj_t *status = panel(s_screen, 14, 68, BSP_LCD_W - 28, 78);
    if (!title || !s_battery || !s_mode || !status) goto fail;
    lv_obj_set_pos(title, 20, 15);
    lv_obj_set_height(title, 23);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_battery, BSP_LCD_W - 68, 18);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_mode, 20, 43);

    s_state = label(status, "正在启动", 188, s_accent);
    s_detail = label(status, "准备语音和网络", 188, s_muted);
    s_progress = lv_bar_create(status);
    s_conversation = panel(s_screen, 14, 150, BSP_LCD_W - 28, 114);
    if (!s_state || !s_detail || !s_progress || !s_conversation) goto fail;
    lv_obj_set_pos(s_state, 12, 5);
    lv_obj_set_pos(s_detail, 12, 28);
    lv_obj_set_height(s_detail, 45);
    lv_obj_set_pos(s_progress, 12, 74);
    lv_obj_set_size(s_progress, 188, 3);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(s_secondary), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(s_accent), LV_PART_INDICATOR);
    lv_obj_add_flag(s_progress, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_bg_opa(s_conversation, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(s_conversation, 6, 0);
    lv_obj_set_style_pad_ver(s_conversation, 3, 0);
    lv_obj_set_style_pad_row(s_conversation, 5, 0);
    lv_obj_set_flex_flow(s_conversation, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_conversation, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_conversation, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_conversation, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_conversation, lv_color_hex(s_accent), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_conversation, 2, LV_PART_SCROLLBAR);
    s_transcript_title = label(s_conversation, "你说", 194, s_muted);
    s_transcript = label(s_conversation, "按 OK 开始一段语音对话。", 194, UI_TEXT);
    s_reply_title = label(s_conversation, s_name, 194, s_accent);
    s_reply = label(s_conversation, "我会记住当前模式里的对话。", 194, UI_TEXT);
    s_notice = label(s_conversation, "部分文字以 ? 显示或已截断", 194, UI_WARNING);
    s_hint = label(s_screen, "", 218, s_muted);
    if (!s_transcript_title || !s_transcript || !s_reply_title || !s_reply || !s_notice || !s_hint) goto fail;
    lv_obj_set_pos(s_hint, 11, 273);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    s_setup = false;
    s_confirm = false;
    s_content_warning = false;
    s_status_warning = false;
    notice();
    hint();
    lv_screen_load(s_screen);
    return true;

fail:
    lv_obj_delete(s_screen);
    s_screen = NULL;
    return false;
}

void assistant_ui_status(const char *state, const char *detail)
{
    if (!s_screen) return;
    s_status_warning = set_text(s_state, state);
    s_status_warning |= set_text(s_detail, detail);
    lv_obj_add_flag(s_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_state, lv_color_hex(
        state && (strstr(state, "失败") || strstr(state, "错误")) ? UI_WARNING : s_accent), 0);
    notice();
    hint();
}

void assistant_ui_conversation(const char *transcript, const char *reply)
{
    if (!s_screen) return;
    s_content_warning = set_text(s_transcript, transcript);
    s_content_warning |= set_text(s_reply, reply);
    set_text(s_transcript_title, "你说");
    set_text(s_reply_title, s_name);
    notice();
    lv_obj_scroll_to_y(s_conversation, 0, LV_ANIM_OFF);
}

void assistant_ui_setup(const char *ssid, const char *password)
{
    if (!s_screen) return;
    s_setup = true;
    set_text(s_mode, "设置连接 · 语音与记忆");
    assistant_ui_status("连接设备热点", "用手机浏览器完成设置");
    lv_obj_add_flag(s_transcript_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_transcript, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_reply_title, LV_OBJ_FLAG_HIDDEN);
    char detail[160];
    snprintf(detail, sizeof(detail), "%s\n%s\n192.168.4.1", ssid ? ssid : "", password ? password : "");
    s_content_warning = set_text(s_reply, detail);
    notice();
    hint();
    lv_obj_scroll_to_y(s_conversation, 0, LV_ANIM_OFF);
}

void assistant_ui_time_weather(const char *time_str, const char *weather_str)
{
    if (!s_screen || s_setup) return;
    char text[64];
    if (time_str && time_str[0] && weather_str && weather_str[0]) {
        snprintf(text, sizeof(text), "%s · %s", time_str, weather_str);
    } else if (time_str && time_str[0]) {
        snprintf(text, sizeof(text), "%s · 语音伙伴", time_str);
    } else {
        return;
    }
    set_text(s_mode, text);
}

void assistant_ui_mode(bool direct_cloud)
{
    if (!s_screen) return;
    s_setup = false;
    set_text(s_mode, direct_cloud ? "云端直连 · 本机记忆" : "电脑网关 · 持久记忆");
    hint();
}

void assistant_ui_battery(int percent)
{
    if (!s_screen) return;
    if (percent < 0 || percent > 100) lv_label_set_text(s_battery, "--%");
    else lv_label_set_text_fmt(s_battery, "%d%%", percent);
    lv_obj_set_style_text_color(s_battery,
        lv_color_hex(percent >= 0 && percent <= 15 ? UI_WARNING : s_muted), 0);
}

void assistant_ui_scroll(int direction)
{
    if (!s_screen || !direction) return;
    lv_obj_scroll_by(s_conversation, 0, direction < 0 ? 62 : -62, LV_ANIM_OFF);
}

void assistant_ui_recording(unsigned seconds, unsigned limit_seconds)
{
    if (!s_screen) return;
    char elapsed[64];
    snprintf(elapsed, sizeof(elapsed), "%02u / %02u 秒 · 按 OK 结束",
             seconds, limit_seconds);
    assistant_ui_status("正在聆听", elapsed);
    set_text(s_hint, "请对着设备说话\n说完再次按 OK");
    lv_bar_set_range(s_progress, 0, 100);
    unsigned value = !limit_seconds || seconds >= limit_seconds ? 100 :
                     (unsigned)((uint64_t)seconds * 100 / limit_seconds);
    lv_bar_set_value(s_progress, (int32_t)value, LV_ANIM_OFF);
    lv_obj_remove_flag(s_progress, LV_OBJ_FLAG_HIDDEN);
}

void assistant_ui_confirm_clear(bool active)
{
    if (!s_screen) return;
    s_confirm = active;
    assistant_ui_status(active ? "清除当前模式的记忆？" : "继续对话",
                        active ? "清除后无法恢复" : "按 OK 开始录音");
    hint();
}

void assistant_ui_destroy(void)
{
    if (!s_screen) return;
    lv_obj_t *blank = lv_obj_create(NULL);
    if (!blank) return;
    lv_screen_load(blank);
    lv_obj_delete(s_screen);
    s_screen = NULL;
}

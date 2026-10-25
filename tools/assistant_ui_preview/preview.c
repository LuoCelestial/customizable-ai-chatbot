#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "assistant_ui.h"
#include "bsp_display_rounding.h"

LV_FONT_DECLARE(passport_noto_sans_sc_16);
LV_FONT_DECLARE(passport_kaomoji_16);
static unsigned char s_frame[240 * 320 * 3];
static unsigned char s_draw[240 * 20 * 2];

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    int width = area->x2 - area->x1 + 1;
    unsigned stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            const unsigned char *p = pixels + (y - area->y1) * stride + (x - area->x1) * 2;
            unsigned color = p[0] | ((unsigned)p[1] << 8);
            unsigned char *out = s_frame + (y * 240 + x) * 3;
            if (bsp_display_pixel_outside_rounded_rect(x, y, 240, 320, 30)) {
                out[0] = out[1] = out[2] = 0;
                continue;
            }
            out[0] = ((color >> 11) & 31) * 255 / 31;
            out[1] = ((color >> 5) & 63) * 255 / 63;
            out[2] = (color & 31) * 255 / 31;
        }
    }
    lv_display_flush_ready(display);
}

static void save(const char *name)
{
    lv_refr_now(NULL);
    FILE *file = fopen(name, "wb");
    assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    assert(fwrite(s_frame, 1, sizeof(s_frame), file) == sizeof(s_frame));
    fclose(file);
}

int main(int argc, char **argv)
{
    lv_init();
    if (argc == 3 && strcmp(argv[1], "--glyphs") == 0) {
        FILE *file = fopen(argv[2], "r");
        assert(file);
        unsigned code;
        while (fscanf(file, "%x", &code) == 1) {
            lv_font_glyph_dsc_t glyph = {0};
            if (!lv_font_get_glyph_dsc(&passport_noto_sans_sc_16, &glyph, code, 0) || glyph.is_placeholder) {
                fprintf(stderr, "Missing required glyph U+%04X\n", code); return 1;
            }
        }
        fclose(file);
        lv_font_glyph_dsc_t glyph = {0};
        assert(!lv_font_get_glyph_dsc(&passport_noto_sans_sc_16, &glyph, 0x1f642, 0) || glyph.is_placeholder);
        puts("Generated font glyph coverage: PASS");
        return 0;
    }
    lv_display_t *display = lv_display_create(240, 320);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, s_draw, NULL, sizeof(s_draw), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    assistant_profile_t profile;
    assistant_profile_defaults(&profile);
    assert(assistant_ui_init(NULL, &profile));
    assistant_ui_mode(true);
    assistant_ui_battery(83);
    assistant_ui_status("准备就绪", "按确定键开始说话");
    save("idle.ppm");
    assistant_ui_recording(3, 8);
    save("recording.ppm");
    assistant_ui_conversation("请记住，我喜欢喝绿茶。", "记住了，你喜欢喝绿茶。下次在当前模式中对话，我会参考这段记录。");
    assistant_ui_status("回答完成", "已保存记忆，按确定键继续");
    save("conversation.ppm");
    assistant_ui_conversation("给我看看颜文字", "(≧▽≦) (｡･ω･｡)\n(つ≧▽≦)つ (╥﹏╥)\nʕ•ᴥ•ʔ ヽ(・∀・)ﾉ");
    save("kaomoji.ppm");
    assistant_ui_scroll(1);
    save("conversation-scroll.ppm");
    char long_question[769], long_reply[1537];
    for (unsigned i = 0; i < 768; i += 3) memcpy(long_question + i, "问", 3);
    for (unsigned i = 0; i < 1536; i += 3) memcpy(long_reply + i, "答", 3);
    long_question[768] = long_reply[1536] = 0;
    for (unsigned i = 0; i < 20; ++i) {
        assistant_ui_conversation(long_question, long_reply);
        lv_refr_now(NULL);
        assistant_ui_scroll(1);
        lv_refr_now(NULL);
    }
    save("long-text.ppm");
    assistant_ui_confirm_clear(true);
    save("confirm.ppm");
    assistant_ui_setup("Passport-A1B2C3", "0123456789abcdef");
    save("setup.ppm");
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("UI preview PASS; LVGL pool used %u / %u bytes\n", (unsigned)(memory.total_size - memory.free_size), (unsigned)memory.total_size);
    return 0;
}

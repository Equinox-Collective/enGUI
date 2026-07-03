#include "terminal_app.h"
#include "../desktop.h"
#include <equos.h>
#include <string.h>

lv_obj_t *win_terminal = nullptr;
static lv_obj_t *term_history = nullptr;

static void term_input_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target_obj(e);
    if (code == LV_EVENT_READY) {
        const char *cmd = lv_textarea_get_text(ta);
        if (strlen(cmd) == 0) return;

        lv_textarea_add_text(term_history, "\n# ");
        lv_textarea_add_text(term_history, cmd);
        lv_textarea_add_text(term_history, "\n");

        char out_buf[1024] = {0};
        _syscall(73, (uint64_t)cmd, (uint64_t)out_buf, sizeof(out_buf), 0, 0); // SYS_SHELL_EXEC

        if (strlen(out_buf) > 0) {
            lv_textarea_add_text(term_history, out_buf);
        } else {
            lv_textarea_add_text(term_history, "[Ядро выполнило команду, но вывода нет]\n");
        }

        lv_textarea_set_text(ta, "");
    }
}

void terminal_app_init() {
    lv_obj_t *content = create_custom_window("💻 Kernel Console Shell", 480, 360, &win_terminal);

    term_history = lv_textarea_create(content);
    lv_obj_set_size(term_history, LV_PCT(100), LV_PCT(80));
    lv_obj_align(term_history, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_text(term_history, "--- EquinoxOS GUI Kernel Terminal ---\nВведите 'help' для списка команд ядра.\n");
    lv_obj_set_style_bg_color(term_history, lv_color_hex(0x0A0B0E), LV_PART_MAIN);
    lv_obj_set_style_text_color(term_history, lv_color_hex(0x33FF33), LV_PART_MAIN);
    lv_textarea_set_cursor_click_pos(term_history, false);

    lv_obj_t *term_input = lv_textarea_create(content);
    lv_obj_set_size(term_input, LV_PCT(100), 32);
    lv_obj_align(term_input, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_textarea_set_one_line(term_input, true);
    lv_obj_set_style_bg_color(term_input, lv_color_hex(0x14161B), LV_PART_MAIN);
    lv_obj_set_style_text_color(term_input, lv_color_hex(0xE6E6E6), LV_PART_MAIN);
    lv_obj_add_event_cb(term_input, term_input_event_handler, LV_EVENT_READY, nullptr);
}
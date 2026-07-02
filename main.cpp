#include "lvgl.h"
#include "api_gui.h"
#include <equos.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

// Параметры экрана
static uint32_t screen_w = 0;
static uint32_t screen_h = 0;
static uint32_t screen_pitch = 0;
static uint32_t *fb_vram = nullptr;
static bool boot_anim_notified = false;

// Глобальные объекты интерфейса
static lv_obj_t *clock_label = nullptr;
static lv_obj_t *ram_label = nullptr;
static lv_obj_t *start_menu = nullptr;
static lv_obj_t *process_table = nullptr;
static lv_obj_t *ram_chart = nullptr;
static lv_chart_series_t *ram_series = nullptr;

static lv_obj_t *win_terminal = nullptr;
static lv_obj_t *win_sysmon = nullptr;

// --- Дисплейный Flush Callback ---
static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t *src = (uint32_t *)px_map;
    int32_t area_w = lv_area_get_width(area);

    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)fb_vram + y * screen_pitch) + area->x1;
        memcpy(dst, src, area_w * 4);
        src += area_w;
    }

    lv_display_flush_ready(disp);

    // Гасим бут-анимацию при выводе самого первого кадра
    if (!boot_anim_notified) {
        boot_anim_notified = true;
        _syscall(88, 0, 0, 0, 0, 0); // SYS_BOOT_ANIM_DONE
    }
}

// --- Обработка ввода (Мышь) ---
static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    int mx = 0, my = 0;
    bool left = false, right = false;
    sysgui_get_mouse(&mx, &my, &left, &right);

    data->point.x = mx;
    data->point.y = my;
    data->state = left ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// --- Обработка ввода (Клавиатура PS/2 Set 1) ---
static bool is_shift_pressed = false;
static void keyboard_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    uint8_t sc = sysgui_get_scancode();
    if (sc == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    bool release = (sc & 0x80) != 0;
    uint8_t key = sc & 0x7F;

    if (key == 0x2A || key == 0x36) { // Left/Right Shift
        is_shift_pressed = !release;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->state = release ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;

    // Маппинг управляющих клавиш в LVGL
    switch (key) {
        case 0x48: data->key = LV_KEY_UP; return;
        case 0x50: data->key = LV_KEY_DOWN; return;
        case 0x4B: data->key = LV_KEY_LEFT; return;
        case 0x4D: data->key = LV_KEY_RIGHT; return;
        case 0x1C: data->key = LV_KEY_ENTER; return;
        case 0x01: data->key = LV_KEY_ESC; return;
        case 0x0E: data->key = LV_KEY_BACKSPACE; return;
        case 0x0F: data->key = LV_KEY_NEXT; return; // Tab
        case 0x53: data->key = LV_KEY_DEL; return;
        case 0x47: data->key = LV_KEY_HOME; return;
        case 0x4F: data->key = LV_KEY_END; return;
    }

    if (!release) {
        static const char map_lo[128] = {
            0,   27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b','\t',
            'q','w','e','r','t','y','u','i','o','p','[',']','\n',  0, 'a','s',
            'd','f','g','h','j','k','l',';','\'','`',  0,'\\','z','x','c','v',
            'b','n','m',',','.','/',  0, '*',   0, ' ',   0,   0,   0,   0,   0,
        };
        static const char map_hi[128] = {
            0,   27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b','\t',
            'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',  0, 'A','S',
            'D','F','G','H','J','K','L',':','"','~',  0, '|','Z','X','C','V',
            'B','N','M','<','>','?',  0, '*',   0, ' ',   0,   0,   0,   0,   0,
        };
        char c = is_shift_pressed ? map_hi[key] : map_lo[key];
        if (c >= 32 && c <= 126) {
            data->key = c;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    }
}

// --- Помощник создания кастомных окон ---
static lv_obj_t *create_custom_window(const char *title, int w, int h, lv_obj_t **out_win) {
    lv_obj_t *win = lv_obj_create(lv_screen_active());
    *out_win = win;
    lv_obj_set_size(win, w, h);
    lv_obj_set_style_bg_color(win, lv_color_hex(0x1E2127), LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 0, LV_PART_MAIN);
    lv_obj_add_flag(win, LV_OBJ_FLAG_DRAGGABLE);
    lv_obj_center(win);
    lv_obj_add_flag(win, LV_OBJ_FLAG_HIDDEN); // Скрыто по умолчанию

    // Заголовок
    lv_obj_t *header = lv_obj_create(win);
    lv_obj_set_size(header, LV_PCT(100), 32);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 5, LV_PART_MAIN);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    // Кнопка закрытия
    lv_obj_t *close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 22, 22);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xDF5B5B), LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        lv_obj_t *w = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, win);

    // Контейнер контента
    lv_obj_t *content = lv_obj_create(win);
    lv_obj_set_size(content, LV_PCT(100), h - 32);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x14161B), LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 10, LV_PART_MAIN);

    return content;
}

// --- Интерактивный Shell Терминал Ядра ---
static lv_obj_t *term_history = nullptr;
static void term_input_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_READY) {
        const char *cmd = lv_textarea_get_text(ta);
        if (strlen(cmd) == 0) return;

        lv_textarea_add_text(term_history, "\n# ");
        lv_textarea_add_text(term_history, cmd);
        lv_textarea_add_text(term_history, "\n");

        // Выполняем в консоли Ring-0 ядра (сисколл 73)
        char out_buf[1024] = {0};
        _syscall(73, (uint64_t)cmd, (uint64_t)out_buf, sizeof(out_buf), 0, 0);

        if (strlen(out_buf) > 0) {
            lv_textarea_add_text(term_history, out_buf);
        } else {
            lv_textarea_add_text(term_history, "[Ядро не вернуло ответа]\n");
        }

        lv_textarea_set_text(ta, "");
    }
}

// --- Сборка оконных утилит ---
static void build_terminal_window() {
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

static void build_sysmon_window() {
    lv_obj_t *content = create_custom_window("📊 System Statistics & Task Manager", 540, 320, &win_sysmon);

    // Левая колонка: Диспетчер задач
    process_table = lv_table_create(content);
    lv_obj_set_size(process_table, LV_PCT(55), LV_PCT(100));
    lv_obj_align(process_table, LV_ALIGN_LEFT_MID, 0, 0);
    lv_table_set_col_width(process_table, 0, 45);  // PID
    lv_table_set_col_width(process_table, 1, 80);  // Status
    lv_table_set_col_width(process_table, 2, 75);  // Heap limit
    lv_table_set_col_width(process_table, 3, 75);  // Memory mapping
    
    lv_table_set_cell_value(process_table, 0, 0, "PID");
    lv_table_set_cell_value(process_table, 0, 1, "Status");
    lv_table_set_cell_value(process_table, 0, 2, "Heap Brk");
    lv_table_set_cell_value(process_table, 0, 3, "CR3 Page");

    // Правая колонка: RAM Монитор
    lv_obj_t *ram_title = lv_label_create(content);
    lv_label_set_text(ram_title, "RAM History (%)");
    lv_obj_align(ram_title, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_set_style_text_color(ram_title, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    ram_chart = lv_chart_create(content);
    lv_obj_set_size(ram_chart, LV_PCT(40), LV_PCT(70));
    lv_obj_align(ram_chart, LV_ALIGN_BOTTOM_RIGHT, 0, -10);
    lv_chart_set_type(ram_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(ram_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    ram_series = lv_chart_add_series(ram_chart, lv_color_hex(0x6FA8DC), LV_CHART_AXIS_PRIMARY_Y);
}

// --- Системный таймер обновлений (каждую секунду) ---
static void update_system_stats_cb(lv_timer_t *timer) {
    // 1. Обновление времени
    uint64_t unix_secs = 0;
    _syscall(87, (uint64_t)&unix_secs, 0, 0, 0, 0); // SYS_GET_WALL_TIME
    char time_str[32];
    if (unix_secs != 0) {
        time_t t = (time_t)unix_secs;
        struct tm *tm_info = localtime(&t);
        if (tm_info) {
            strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
            lv_label_set_text(clock_label, time_str);
        }
    } else {
        uint32_t ticks = sysgui_get_time_ms();
        uint32_t secs = ticks / 1000;
        sprintf(time_str, "UPTIME: %02d:%02d:%02d", (secs / 3600) % 24, (secs / 60) % 60, secs % 60);
        lv_label_set_text(clock_label, time_str);
    }

    // 2. Обновление системной памяти
    uint64_t used = 0, total = 0;
    sysgui_get_mem_info(&used, &total);
    if (total == 0) total = 512 * 1024 * 1024; // fallback

    char mem_str[64];
    sprintf(mem_str, "RAM: %d MB / %d MB", (int)(used / (1024 * 1024)), (int)(total / (1024 * 1024)));
    lv_label_set_text(ram_label, mem_str);

    if (ram_chart && ram_series) {
        int percent = (int)((used * 100) / total);
        lv_chart_set_next_value(ram_chart, ram_series, percent);
    }

    // 3. Обновление диспетчера задач
    if (process_table) {
        TaskInfo tasks[20];
        int count = sysgui_get_task_list(tasks, 20);
        lv_table_set_row_cnt(process_table, count + 1);

        for (int i = 0; i < count; i++) {
            char pid_str[16], brk_str[16], cr3_str[16];
            sprintf(pid_str, "%d", (int)tasks[i].pid);
            sprintf(brk_str, "0x%x", (unsigned int)(tasks[i].brk & 0xFFFFFFFF));
            sprintf(cr3_str, "0x%x", (unsigned int)(tasks[i].cr3 & 0xFFFFFFFF));

            lv_table_set_cell_value(process_table, i + 1, 0, pid_str);
            lv_table_set_cell_value(process_table, i + 1, 1, tasks[i].running ? "RUNNING" : "STOPPED");
            lv_table_set_cell_value(process_table, i + 1, 2, brk_str);
            lv_table_set_cell_value(process_table, i + 1, 3, cr3_str);
        }
    }
}

// --- Поведение запускаторов меню и иконок ---
static void menu_launch_cb(lv_event_t *e) {
    const char *cmd = (const char *)lv_event_get_user_data(e);
    if (strcmp(cmd, "win_terminal") == 0) {
        lv_obj_remove_flag(win_terminal, LV_OBJ_FLAG_HIDDEN);
    } else if (strcmp(cmd, "win_sysmon") == 0) {
        lv_obj_remove_flag(win_sysmon, LV_OBJ_FLAG_HIDDEN);
    } else {
        sysgui_execute_app(cmd);
    }
    lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
}

// --- Помощник создания ярлыков рабочего стола ---
static void create_desktop_icon(const char *name, const char *cmd, const char *symbol, int x, int y) {
    lv_obj_t *icon_btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(icon_btn, 80, 80);
    lv_obj_set_pos(icon_btn, x, y);
    lv_obj_set_style_bg_color(icon_btn, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    lv_obj_set_style_border_color(icon_btn, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_btn, 1, LV_PART_MAIN);

    lv_obj_t *sym_lbl = lv_label_create(icon_btn);
    lv_label_set_text(sym_lbl, symbol);
    lv_obj_align(sym_lbl, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_font(sym_lbl, &lv_font_montserrat_14, LV_PART_MAIN); // Иконка

    lv_obj_t *name_lbl = lv_label_create(icon_btn);
    lv_label_set_text(name_lbl, name);
    lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    lv_obj_add_event_cb(icon_btn, [](lv_event_t *e) {
        const char *command = (const char *)lv_event_get_user_data(e);
        if (strcmp(command, "win_terminal") == 0) {
            lv_obj_remove_flag(win_terminal, LV_OBJ_FLAG_HIDDEN);
        } else if (strcmp(command, "win_sysmon") == 0) {
            lv_obj_remove_flag(win_sysmon, LV_OBJ_FLAG_HIDDEN);
        } else {
            sysgui_execute_app(command);
        }
    }, LV_EVENT_CLICKED, (void *)cmd);
}

// --- Построение окружения Рабочего Стола ---
static void build_desktop_ui() {
    // Стиль фона рабочего стола (Темный минимализм)
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x14161B), LV_PART_MAIN);

    // 1. Создание верхней панели управления (Top Bar)
    lv_obj_t *top_bar = lv_obj_create(lv_screen_active());
    lv_obj_set_size(top_bar, LV_PCT(100), 38);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x1E2127), LV_PART_MAIN);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 5, LV_PART_MAIN);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Кнопка Старт / "Equinox"
    lv_obj_t *start_btn = lv_button_create(top_bar);
    lv_obj_set_size(start_btn, 110, 28);
    lv_obj_align(start_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    
    lv_obj_t *start_btn_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_btn_lbl, "🌌 EquinoxOS");
    lv_obj_center(start_btn_lbl);
    lv_obj_set_style_text_color(start_btn_lbl, lv_color_hex(0x6FA8DC), LV_PART_MAIN);

    // Часы по центру
    clock_label = lv_label_create(top_bar);
    lv_label_set_text(clock_label, "00:00:00");
    lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(clock_label, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    // ОЗУ справа
    ram_label = lv_label_create(top_bar);
    lv_label_set_text(ram_label, "RAM: 0MB / 0MB");
    lv_obj_align(ram_label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_color(ram_label, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    // 2. Создание выпадающего меню "Пуск"
    start_menu = lv_obj_create(lv_screen_active());
    lv_obj_set_size(start_menu, 160, 200);
    lv_obj_set_pos(start_menu, 10, 42);
    lv_obj_set_style_bg_color(start_menu, lv_color_hex(0x1E2127), LV_PART_MAIN);
    lv_obj_set_style_border_color(start_menu, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(start_menu, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(start_menu, 5, LV_PART_MAIN);
    lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN); // Скрыто

    // Привязываем переключение меню пуск
    lv_obj_add_event_cb(start_btn, [](lv_event_t *e) {
        if (lv_obj_has_flag(start_menu, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Список приложений в пуске
    struct MenuItem {
        const char *name;
        const char *cmd;
    } menu_items[] = {
        {"💻 Terminal", "win_terminal"},
        {"📊 Stats & Tasks", "win_sysmon"},
        {"🕹️ Doom Classic", "doom"},
        {"🌐 Web Browser", "htmlview"},
        {"🐚 Bash Shell", "bash"},
        {"🔄 Reboot System", "reboot"}
    };

    for (size_t i = 0; i < sizeof(menu_items)/sizeof(menu_items[0]); i++) {
        lv_obj_t *btn = lv_button_create(start_menu);
        lv_obj_set_size(btn, LV_PCT(100), 28);
        lv_obj_set_pos(btn, 0, i * 31);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2D34), LV_PART_MAIN);
        
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menu_items[i].name);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE6E6E6), LV_PART_MAIN);
        
        lv_obj_add_event_cb(btn, menu_launch_cb, LV_EVENT_CLICKED, (void *)menu_items[i].cmd);
    }

    // 3. Сборка системных окон
    build_terminal_window();
    build_sysmon_window();

    // 4. Добавление ярлыков на рабочий стол
    create_desktop_icon("Terminal", "win_terminal", "💻", 20, 60);
    create_desktop_icon("Monitor", "win_sysmon", "📊", 20, 160);
    create_desktop_icon("Doom", "doom", "🕹️", 20, 260);

    // 5. Запуск системного таймера обновления метрик
    lv_timer_create(update_system_stats_cb, 1000, nullptr);
}

// --- Главная точка входа ---
int main(int argc, char **argv) {
    // 1. Инициализация дисплея фреймбуфера
    _syscall(SYS_GET_VESA_INFO, (uint64_t)&screen_w, (uint64_t)&screen_w, (uint64_t)&screen_h, (uint64_t)&screen_pitch, 0);
    uint32_t fb_size = screen_h * screen_pitch;
    fb_vram = (uint32_t *)_syscall(SYS_MAP_PHYS, screen_w, fb_size, 0, 0, 0); // Маппинг VRAM

    // 2. Инициализация LVGL
    lv_init();

    // Задаем callback тиков для LVGL на PIT таймер ядра (сисколл 6)
    lv_tick_set_cb([]() -> uint32_t {
        return sysgui_get_time_ms();
    });

    // 3. Создание виртуального дисплея
    lv_display_t *disp = lv_display_create(screen_w, screen_h);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    // Выделяем 1/10 буфера экрана под частичный программный рендеринг LVGL
    uint32_t draw_buf_size = screen_w * (screen_h / 10);
    uint32_t *buf1 = (uint32_t *)malloc(draw_buf_size * sizeof(uint32_t));
    if (buf1) {
        lv_display_set_buffers(disp, buf1, nullptr, draw_buf_size * sizeof(uint32_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    }

    // 4. Регистрация устройств ввода
    lv_indev_t *mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse_indev, mouse_read_cb);

    lv_indev_t *kb_indev = lv_indev_create();
    lv_indev_set_type(kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb_indev, keyboard_read_cb);

    // 5. Сборка всего интерфейса
    build_desktop_ui();

    // 6. Основной цикл выполнения окружения
    while (true) {
        // Умная пауза: если на переднем плане запущен Doom, не перерисовываем GUI
        // для предотвращения жесткого мерцания фреймбуфера
        if (sysgui_is_fg_app_active()) {
            sysgui_sleep_ms(100);
            continue;
        }

        // Вызов обработчика LVGL
        lv_timer_handler();

        // Уступаем CPU планировщику на 10 мс
        sysgui_sleep_ms(10);
    }

    return 0;
}
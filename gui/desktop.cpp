#include "desktop.h"
#include "apps/terminal_app.h"
#include "apps/sysmon_app.h"
#include "api_gui.h"
#include <string.h>

lv_obj_t *clock_label = nullptr;
lv_obj_t *ram_label = nullptr;
lv_obj_t *start_menu = nullptr;

// Callback для перетаскивания окон в LVGL v9
static void win_drag_event_cb(lv_event_t *e) {
    lv_obj_t *header = (lv_obj_t *)lv_event_get_target_obj(e);
    lv_obj_t *win = lv_obj_get_parent(header);
    if (!win) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);
    int32_t x = lv_obj_get_x_aligned(win) + vect.x;
    int32_t y = lv_obj_get_y_aligned(win) + vect.y;
    lv_obj_set_pos(win, x, y);
}

// Фабрика создания окон (используется всеми приложениями)
lv_obj_t *create_custom_window(const char *title, int w, int h, lv_obj_t **out_win) {
    lv_obj_t *win = lv_obj_create(lv_screen_active());
    *out_win = win;
    lv_obj_set_size(win, w, h);
    lv_obj_set_style_bg_color(win, lv_color_hex(0x1E2127), LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 0, LV_PART_MAIN);
    lv_obj_center(win);
    
    // БЛОКИРУЕМ СКРОЛЛИНГ САМОГО ОКНА (Рамки)
    lv_obj_remove_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(win, LV_OBJ_FLAG_HIDDEN); // Скрыто по умолчанию

    // Заголовок
    lv_obj_t *header = lv_obj_create(win);
    lv_obj_set_size(header, LV_PCT(100), 32);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 5, LV_PART_MAIN);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Регистрируем событие перемещения
    lv_obj_add_event_cb(header, win_drag_event_cb, LV_EVENT_PRESSING, nullptr);

    lv_obj_t *title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    // Улучшение #2: кнопка «Minimize» (сворачивание окна)
    lv_obj_t *minimize_btn = lv_button_create(header);
    lv_obj_set_size(minimize_btn, 22, 22);
    lv_obj_align(minimize_btn, LV_ALIGN_RIGHT_MID, -32, 0); // левее кнопки закрытия
    lv_obj_set_style_bg_color(minimize_btn, lv_color_hex(0xF0C040), LV_PART_MAIN);
    lv_obj_t *minimize_lbl = lv_label_create(minimize_btn);
    lv_label_set_text(minimize_lbl, LV_SYMBOL_MINUS);
    lv_obj_center(minimize_lbl);
    lv_obj_set_style_text_color(minimize_lbl, lv_color_hex(0x1E2127), LV_PART_MAIN);
    lv_obj_add_event_cb(minimize_btn, [](lv_event_t *e) {
        lv_obj_t *w = (lv_obj_t *)lv_event_get_user_data(e);
        // Уменьшаем окно до строки заголовка (высота 32px)
        if (lv_obj_get_height(w) > 32) {
            lv_obj_set_height(w, 32);
        } else {
            // Восстановление: убираем ограничение размера
            lv_obj_set_size(w, lv_pct(0), lv_pct(0)); // триггер пересчёта
            lv_obj_update_layout(w);
        }
    }, LV_EVENT_CLICKED, win);

    // Кнопка "Закрыть"
    lv_obj_t *close_btn = lv_button_create(header);
    lv_obj_set_size(close_btn, 22, 22);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xDF5B5B), LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        lv_obj_t *w = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, win);

    // Контент-область
    lv_obj_t *content = lv_obj_create(win);
    lv_obj_set_size(content, LV_PCT(100), h - 32);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x14161B), LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 10, LV_PART_MAIN);
    
    // Блокируем скроллинг контент-контейнера окна (если внутри будет таблица или текст-ареа — они сами будут скроллиться внутри себя)
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    return content;
}

// Запуск приложений из Пуска
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

// Улучшение #3: tooltip при наведении на иконку рабочего стола
static void desktop_icon_hover_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *icon_btn = (lv_obj_t *)lv_event_get_target_obj(e);

    // Ищем метку подсказки среди дочерних объектов (последний child = tooltip label)
    lv_obj_t *tooltip = nullptr;
    uint32_t child_count = lv_obj_get_child_count(icon_btn);
    if (child_count > 0) {
        lv_obj_t *last = lv_obj_get_child(icon_btn, child_count - 1);
        // tooltip имеет пользовательские данные = 0xTOOLTIP
        if ((uintptr_t)lv_obj_get_user_data(last) == 0xB00B5) {
            tooltip = last;
        }
    }
    if (!tooltip) return;

    if (code == LV_EVENT_HOVER_OVER) {
        lv_obj_remove_flag(tooltip, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_HOVER_LEAVE) {
        lv_obj_add_flag(tooltip, LV_OBJ_FLAG_HIDDEN);
    }
}

// Создание иконки рабочего стола с поддержкой LVGL-символов
static void create_desktop_icon(const char *name, const char *cmd, const char *symbol, int x, int y) {
    lv_obj_t *icon_btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(icon_btn, 80, 80);
    lv_obj_set_pos(icon_btn, x, y);
    lv_obj_set_style_bg_color(icon_btn, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    lv_obj_set_style_border_color(icon_btn, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_btn, 1, LV_PART_MAIN);
    lv_obj_remove_flag(icon_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sym_lbl = lv_label_create(icon_btn);
    lv_label_set_text(sym_lbl, symbol);
    lv_obj_align(sym_lbl, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t *name_lbl = lv_label_create(icon_btn);
    lv_label_set_text(name_lbl, name);
    lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    // Улучшение #3: всплывающая подсказка (tooltip) при наведении
    lv_obj_t *tooltip_lbl = lv_label_create(icon_btn);
    lv_label_set_text(tooltip_lbl, cmd);
    lv_obj_set_style_bg_color(tooltip_lbl, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tooltip_lbl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(tooltip_lbl, lv_color_hex(0x6FA8DC), LV_PART_MAIN);
    lv_obj_set_style_border_width(tooltip_lbl, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tooltip_lbl, 4, LV_PART_MAIN);
    lv_obj_set_style_text_color(tooltip_lbl, lv_color_hex(0xBBBBBB), LV_PART_MAIN);
    lv_obj_align(tooltip_lbl, LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
    lv_obj_add_flag(tooltip_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(tooltip_lbl, (void *)0xB00B5); // маркер tooltip
    lv_obj_add_flag(icon_btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(icon_btn, desktop_icon_hover_cb, LV_EVENT_HOVER_OVER, nullptr);
    lv_obj_add_event_cb(icon_btn, desktop_icon_hover_cb, LV_EVENT_HOVER_LEAVE, nullptr);

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

void desktop_init() {
    // Выключаем скроллинг у самого экрана
    lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x14161B), LV_PART_MAIN);

    // 1. Статус-бар (Top Bar)
    lv_obj_t *top_bar = lv_obj_create(lv_screen_active());
    lv_obj_set_size(top_bar, LV_PCT(100), 38);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x1E2127), LV_PART_MAIN);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 5, LV_PART_MAIN);
    
    // БЛОКИРУЕМ СКРОЛЛИНГ ТОР-БАРА
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Кнопка "Пуск"
    lv_obj_t *start_btn = lv_button_create(top_bar);
    lv_obj_set_size(start_btn, 110, 28);
    lv_obj_align(start_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x2A2D34), LV_PART_MAIN);
    lv_obj_remove_flag(start_btn, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *start_btn_lbl = lv_label_create(start_btn);
    // Используем красивый системный домик вместо эмодзи космоса
    lv_label_set_text(start_btn_lbl, LV_SYMBOL_HOME " EquinoxOS");
    lv_obj_center(start_btn_lbl);
    lv_obj_set_style_text_color(start_btn_lbl, lv_color_hex(0x6FA8DC), LV_PART_MAIN);

    // Текстовые метки панели
    clock_label = lv_label_create(top_bar);
    lv_label_set_text(clock_label, "00:00:00");
    lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(clock_label, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    ram_label = lv_label_create(top_bar);
    lv_label_set_text(ram_label, "RAM: 0MB / 0MB");
    lv_obj_align(ram_label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_color(ram_label, lv_color_hex(0xE6E6E6), LV_PART_MAIN);

    // 2. Выпадающее меню Пуск
    start_menu = lv_obj_create(lv_screen_active());
    lv_obj_set_size(start_menu, 160, 200);
    lv_obj_set_pos(start_menu, 10, 42);
    lv_obj_set_style_bg_color(start_menu, lv_color_hex(0x1E2127), LV_PART_MAIN);
    lv_obj_set_style_border_color(start_menu, lv_color_hex(0x4A505C), LV_PART_MAIN);
    lv_obj_set_style_border_width(start_menu, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(start_menu, 5, LV_PART_MAIN);
    
    // БЛОКИРУЕМ СКРОЛЛИНГ МЕНЮ ПУСК
    lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(start_btn, [](lv_event_t *e) {
        if (lv_obj_has_flag(start_menu, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(start_menu, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Список приложений Пуска
    struct MenuItem {
        const char *name;
        const char *cmd;
    } menu_items[] = {
        {LV_SYMBOL_KEYBOARD " Terminal", "win_terminal"},
        {LV_SYMBOL_SETTINGS " Stats & Tasks", "win_sysmon"},
        {LV_SYMBOL_PLAY " Doom Classic", "doom"},
        {LV_SYMBOL_DIRECTORY " Web Browser", "htmlview"},
        {LV_SYMBOL_LIST " Bash Shell", "bash"},
        {LV_SYMBOL_POWER " Reboot", "reboot"}
    };

    for (size_t i = 0; i < sizeof(menu_items)/sizeof(menu_items[0]); i++) {
        lv_obj_t *btn = lv_button_create(start_menu);
        lv_obj_set_size(btn, LV_PCT(100), 28);
        lv_obj_set_pos(btn, 0, i * 31);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2D34), LV_PART_MAIN);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menu_items[i].name);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE6E6E6), LV_PART_MAIN);
        
        lv_obj_add_event_cb(btn, menu_launch_cb, LV_EVENT_CLICKED, (void *)menu_items[i].cmd);
    }

    // 3. Создание ярлыков (заменяем эмодзи на встроенные символы)
    create_desktop_icon("Terminal", "win_terminal", LV_SYMBOL_KEYBOARD, 20, 60);
    create_desktop_icon("Monitor", "win_sysmon", LV_SYMBOL_SETTINGS, 20, 160);
    create_desktop_icon("Doom", "doom", LV_SYMBOL_PLAY, 20, 260);
}
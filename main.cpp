#include "lvgl.h"
#include "api_gui.h"
#include "gui/desktop.h"
#include "gui/apps/terminal_app.h"
#include "gui/apps/sysmon_app.h"
#include <equos.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Улучшение #1: именованные константы для magic syscall numbers
// Вместо _syscall(88, ...) и _syscall(87, ...) прямо в коде
#define SYS_GET_WALL_TIME   87
#define SYS_BOOT_ANIM_DONE  88

// Параметры фреймбуфера (остаются локальными для main.cpp)
static uint32_t screen_w = 0;
static uint32_t screen_h = 0;
static uint32_t screen_pitch = 0;
static uint32_t *fb_vram = nullptr;
static bool boot_anim_notified = false;

// Утилита для вывода отладочных сообщений в консоль QEMU (COM1)
static void sysgui_log(const char *msg) {
    _syscall(SYS_PRINT, (uint64_t)msg, 0, 0, 0, 0);
}

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
        sysgui_log("sysgui: First frame flushed! Notifying boot anim done...\n");
        boot_anim_notified = true;
        _syscall(SYS_BOOT_ANIM_DONE, 0, 0, 0, 0, 0);
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


// --- Системный таймер обновлений системной статистики (каждую секунду) ---
static void update_system_stats_cb(lv_timer_t *timer) {
    // 1. Время из Unix timestamp
    uint64_t unix_secs = 0;
    _syscall(SYS_GET_WALL_TIME, (uint64_t)&unix_secs, 0, 0, 0, 0);
    char time_str[32];
    
    if (unix_secs != 0) {
        uint32_t seconds_in_day = unix_secs % 86400;
        uint32_t hours = seconds_in_day / 3600;
        uint32_t minutes = (seconds_in_day % 3600) / 60;
        uint32_t seconds = seconds_in_day % 60;
        uint32_t local_hours = (hours + 3) % 24; // UTC+3
        sprintf(time_str, "%02u:%02u:%02u", local_hours, minutes, seconds);
        lv_label_set_text(clock_label, time_str);
    } else {
        uint32_t ticks = sysgui_get_time_ms();
        uint32_t secs = ticks / 1000;
        sprintf(time_str, "UPTIME: %02d:%02d:%02d", (secs / 3600) % 24, (secs / 60) % 60, secs % 60);
        lv_label_set_text(clock_label, time_str);
    }

    // 2. RAM
    uint64_t used = 0, total = 0;
    sysgui_get_mem_info(&used, &total);
    if (total == 0) total = 512 * 1024 * 1024;

    char mem_str[64];
    sprintf(mem_str, "RAM: %d MB / %d MB", (int)(used / (1024 * 1024)), (int)(total / (1024 * 1024)));
    lv_label_set_text(ram_label, mem_str);

    // Улучшение #4: цветовая индикация загрузки RAM
    // Зелёный < 60%, жёлтый 60–85%, красный > 85%
    int ram_pct = (total > 0) ? (int)((used * 100) / total) : 0;
    lv_color_t ram_color;
    if (ram_pct >= 85)
        ram_color = lv_color_hex(0xFF5555); // красный — критично
    else if (ram_pct >= 60)
        ram_color = lv_color_hex(0xFFCC44); // жёлтый — предупреждение
    else
        ram_color = lv_color_hex(0x55CC55); // зелёный — норма
    lv_obj_set_style_text_color(ram_label, ram_color, LV_PART_MAIN);

    if (ram_chart && ram_series) {
        int percent = (int)((used * 100) / total);
        lv_chart_set_next_value(ram_chart, ram_series, percent);
    }

    // 3. Таблица задач
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

int main(int argc, char **argv) {
    sysgui_log("sysgui: Starting main() execution...\n");

    // 1. Получение информации о VESA LFB через SYS_GET_VESA_INFO
    uint64_t phys_fb = 0;
    _syscall(SYS_GET_VESA_INFO, (uint64_t)&phys_fb, (uint64_t)&screen_w, (uint64_t)&screen_h, (uint64_t)&screen_pitch, 0);
    
    char vesa_log[128];
    sprintf(vesa_log, "sysgui: VESA returned w=%d, h=%d, pitch=%d, phys=0x%lx\n", 
            (int)screen_w, (int)screen_h, (int)screen_pitch, phys_fb);
    sysgui_log(vesa_log);

    if (screen_w == 0 || screen_h == 0 || phys_fb == 0) {
        sysgui_log("sysgui: ERROR! VESA params are invalid! Aborting...\n");
        return -1;
    }

    // Маппинг видеопамяти ядра в адресное пространство Ring 3 приложения
    sysgui_log("sysgui: Mapping physical VRAM...\n");
    uint32_t fb_size = screen_h * screen_pitch;
    fb_vram = (uint32_t *)_syscall(SYS_MAP_PHYS, phys_fb, fb_size, 0, 0, 0);

    sprintf(vesa_log, "sysgui: fb_vram mapped successfully at virtual %p\n", fb_vram);
    sysgui_log(vesa_log);

    // 2. Инициализация LVGL
    sysgui_log("sysgui: Initializing LVGL library...\n");
    lv_init();

    lv_tick_set_cb([]() -> uint32_t {
        return sysgui_get_time_ms();
    });

    // 3. Создание дисплея
    sysgui_log("sysgui: Creating virtual display...\n");
    lv_display_t *disp = lv_display_create(screen_w, screen_h);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    uint32_t draw_buf_size = screen_w * (screen_h / 10);
    uint32_t *buf1 = (uint32_t *)malloc(draw_buf_size * sizeof(uint32_t));
    if (buf1) {
        sysgui_log("sysgui: Software render buffer allocated successfully.\n");
        lv_display_set_buffers(disp, buf1, nullptr, draw_buf_size * sizeof(uint32_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
    } else {
        sysgui_log("sysgui: ERROR! Failed to allocate software render buffer!\n");
        return -1;
    }

    // 4. Регистрация устройств ввода
    sysgui_log("sysgui: Registering pointer input...\n");
    lv_indev_t *mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse_indev, mouse_read_cb);

    // Создаем красивую круглую стрелку/точку курсора, независимую от шрифтов
    lv_obj_t *cursor_obj = lv_obj_create(lv_screen_active());
    lv_obj_set_size(cursor_obj, 12, 12);
    lv_obj_set_style_bg_color(cursor_obj, lv_color_hex(0x6FA8DC), LV_PART_MAIN); // Голубой центр
    lv_obj_set_style_border_color(cursor_obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN); // Белый кант
    lv_obj_set_style_border_width(cursor_obj, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(cursor_obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_indev_set_cursor(mouse_indev, cursor_obj); // Назначаем курсор диспетчеру ввода

    // Клавиатура
    sysgui_log("sysgui: Registering keypad input...\n");
    lv_indev_t *kb_indev = lv_indev_create();
    lv_indev_set_type(kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb_indev, keyboard_read_cb);

    lv_group_t *kb_group = lv_group_create();
    lv_group_set_default(kb_group);
    lv_indev_set_group(kb_indev, kb_group);

    // 5. Инициализация UI модулей
    sysgui_log("sysgui: Building Desktop interface elements...\n");
    desktop_init();
    terminal_app_init();
    sysmon_app_init();

    // Запуск системного таймера обновления метрик
    lv_timer_create(update_system_stats_cb, 1000, nullptr);

    sysgui_log("sysgui: Main loop started! Entering execution cycle...\n");

    // 6. Главный цикл
    while (true) {
        if (sysgui_is_fg_app_active()) {
            sysgui_sleep_ms(100);
            continue;
        }

        lv_timer_handler();
        sysgui_sleep_ms(10);
    }

    return 0;
}
#include "sysmon_app.h"
#include "../desktop.h"

lv_obj_t *win_sysmon = nullptr;
lv_obj_t *process_table = nullptr;
lv_obj_t *ram_chart = nullptr;
lv_chart_series_t *ram_series = nullptr;

void sysmon_app_init() {
    lv_obj_t *content = create_custom_window("📊 System Statistics & Task Manager", 540, 320, &win_sysmon);

    // Левая колонка: Таблица задач
    process_table = lv_table_create(content);
    lv_obj_set_size(process_table, LV_PCT(55), LV_PCT(100));
    lv_obj_align(process_table, LV_ALIGN_LEFT_MID, 0, 0);
    lv_table_set_col_width(process_table, 0, 45);  // PID
    lv_table_set_col_width(process_table, 1, 80);  // Status
    lv_table_set_col_width(process_table, 2, 75);  // Heap limit
    lv_table_set_col_width(process_table, 3, 75);  // Page table CR3
    
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
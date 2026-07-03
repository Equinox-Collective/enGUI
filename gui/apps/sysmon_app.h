#ifndef SYSMON_APP_H
#define SYSMON_APP_H

#include "lvgl.h"

extern lv_obj_t *win_sysmon;
extern lv_obj_t *process_table;
extern lv_obj_t *ram_chart;
extern lv_chart_series_t *ram_series;

void sysmon_app_init();

#endif // SYSMON_APP_H
#ifndef DESKTOP_H
#define DESKTOP_H

#include "lvgl.h"

extern lv_obj_t *clock_label;
extern lv_obj_t *ram_label;
extern lv_obj_t *start_menu;

void desktop_init();
lv_obj_t *create_custom_window(const char *title, int w, int h, lv_obj_t **out_win);

#endif // DESKTOP_H
#ifndef API_GUI_H
#define API_GUI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Системные вызовы и инициализация
bool sysgui_is_fg_app_active();
void sysgui_get_mouse(int *x, int *y, bool *left, bool *right);
uint8_t sysgui_get_scancode();
uint32_t sysgui_get_time_ms();
void sysgui_sleep_ms(uint32_t ms);

// Структура для системного монитора
struct TaskInfo {
    uint64_t pid;
    uint64_t cr3;
    uint64_t brk;
    bool running;
};

int sysgui_get_task_list(TaskInfo *list, int max_tasks);
void sysgui_get_mem_info(uint64_t *used, uint64_t *total);
void sysgui_execute_app(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif // API_GUI_H
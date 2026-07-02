#include "api_gui.h"
#include <equos.h>

extern "C" {

// Проверка активности полноэкранного приложения (например, Doom)
bool sysgui_is_fg_app_active() {
    uint64_t fg_pid = _syscall(SYS_GET_FG_APP, 0, 0, 0, 0, 0);
    return (fg_pid != 0);
}

// Прямой опрос мыши ядра (забираем rax, rbx, rcx)
void sysgui_get_mouse(int *x, int *y, bool *left, bool *right) {
    uint64_t mx = 0, my = 0, btns = 0;
    __asm__ volatile(
        "mov $7, %%rax; int $0x80; mov %%rax, %0; mov %%rbx, %1; mov %%rcx, %2"
        : "=r"(mx), "=r"(my), "=r"(btns)::"rax", "rbx", "rcx");
    
    if (x) *x = (int)mx;
    if (y) *y = (int)my;
    if (left) *left = (btns & 1) != 0;
    if (right) *right = (btns & 2) != 0;
}

// Чтение клавиатуры
uint8_t sysgui_get_scancode() {
    return (uint8_t)_syscall(SYS_GET_SCANCODE, 0, 0, 0, 0, 0);
}

// Время системы (мс с момента загрузки)
uint32_t sysgui_get_time_ms() {
    return (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
}

// Сон для планировщика задач
void sysgui_sleep_ms(uint32_t ms) {
    _syscall(SYS_SLEEP, ms, 0, 0, 0, 0);
}

// Получение списка процессов через SYS_TASK_INFO
int sysgui_get_task_list(TaskInfo *list, int max_tasks) {
    int count = 0;
    for (int i = 0; i < max_tasks; i++) {
        sys_task_info_t info;
        uint64_t ret = _syscall(SYS_TASK_INFO, i, (uint64_t)&info, 0, 0, 0);
        if (ret == 1) {
            list[count].pid = info.pid;
            list[count].cr3 = info.cr3;
            list[count].brk = info.brk;
            list[count].running = (info.running != 0);
            count++;
        } else {
            break;
        }
    }
    return count;
}

// Получение информации о системной памяти
void sysgui_get_mem_info(uint64_t *used, uint64_t *total) {
    if (used) *used = sys_get_used_mem();
    if (total) *total = sys_get_total_mem();
}

// Запуск сторонних программ
void sysgui_execute_app(const char *cmd) {
    _syscall(SYS_EXEC, (uint64_t)cmd, 0, 0, 0, 0);
}

}
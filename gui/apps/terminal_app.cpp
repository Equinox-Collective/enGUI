#include "terminal_app.h"
#include "../desktop.h"
#include <equos.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* Minimal poll() wrapper via Linux compatibility gateway int 0x81 (syscall 7). */
struct equos_pollfd {
    int fd;
    short events;
    short revents;
};

#define EQUOS_POLLIN  0x001
#define EQUOS_POLLHUP 0x010
#define EQUOS_POLLERR 0x008
#define EQUOS_POLLNVAL 0x020

static int equos_poll(struct equos_pollfd *fds, int nfds, int timeout_ms) {
    long ret;
    __asm__ volatile("int $0x81"
        : "=a"(ret)
        : "a"(7L), "D"((uint64_t)fds), "S"((long)nfds), "d"((long)timeout_ms)
        : "rcx", "r11", "memory");
    return (int)ret;
}

lv_obj_t *win_terminal = nullptr;
static lv_obj_t *term_history = nullptr;

static pid_t shell_pid = -1;
static int shell_stdin_w = -1;   /* write commands to sh.elf */
static int shell_stdout_r = -1;  /* read output from sh.elf */
static bool shell_running = false;
static lv_timer_t *shell_poll_timer = nullptr;

/* Translate standard ANSI SGR sequences to native LVGL recolor tags, handle Backspaces */
static void term_append_output_with_control(const char *text, size_t len) {
    if (!term_history || !text || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        
        if (c == '\b') {
            // Стираем последний символ перед курсором (стандартный Backspace)
            lv_textarea_delete_char(term_history);
        } else if (c == '\x1b' && i + 1 < len && text[i + 1] == '[') {
            // Парсер ANSI SGR цветов
            size_t seq_start = i;
            i += 2;
            size_t cmd_start = i;
            while (i < len && text[i] != 'm') i++;
            
            if (i < len && text[i] == 'm') {
                // Переводим цвета ANSI в теги реколора LVGL (#rrggbb)
                if (strncmp(&text[cmd_start], "32", 2) == 0) {
                    lv_textarea_add_text(term_history, "#33FF33 "); // Зеленый промпт
                } else if (strncmp(&text[cmd_start], "36", 2) == 0) {
                    lv_textarea_add_text(term_history, "#6FA8DC "); // Голубой баннер
                } else if (strncmp(&text[cmd_start], "34", 2) == 0) {
                    lv_textarea_add_text(term_history, "#5588FF "); // Синий CWD
                } else if (strncmp(&text[cmd_start], "31", 2) == 0) {
                    lv_textarea_add_text(term_history, "#FF5555 "); // Красные ошибки
                } else if (strncmp(&text[cmd_start], "0", 1) == 0) {
                    lv_textarea_add_text(term_history, "#");        // Сброс цвета
                }
            }
        } else {
            // Обычный символ — выводим на экран
            char buf[2] = {c, '\0'};
            lv_textarea_add_text(term_history, buf);
        }
    }
    // Всегда удерживаем скролл в самом низу
    lv_textarea_set_cursor_pos(term_history, LV_TEXTAREA_CURSOR_LAST);
}

static void term_on_shell_exit(void) {
    shell_running = false;
    term_append_output_with_control("\n[sh.elf exited]\n", 17);
    if (shell_stdin_w >= 0) { close(shell_stdin_w); shell_stdin_w = -1; }
    if (shell_stdout_r >= 0) { close(shell_stdout_r); shell_stdout_r = -1; }
    shell_pid = -1;
}

static void shell_poll_cb(lv_timer_t *timer) {
    (void)timer;
    if (!shell_running || shell_stdout_r < 0) return;

    struct equos_pollfd pfd;
    pfd.fd = shell_stdout_r;
    pfd.events = EQUOS_POLLIN;
    pfd.revents = 0;

    if (equos_poll(&pfd, 1, 0) <= 0) return;

    if (pfd.revents & (EQUOS_POLLERR | EQUOS_POLLHUP | EQUOS_POLLNVAL)) {
        term_on_shell_exit();
        return;
    }

    if (!(pfd.revents & EQUOS_POLLIN)) return;

    char raw[512];
    ssize_t n = read(shell_stdout_r, raw, sizeof(raw) - 1);
    if (n < 0) return;
    if (n == 0) {
        term_on_shell_exit();
        return;
    }

    raw[n] = '\0';
    term_append_output_with_control(raw, (size_t)n);
}

static bool spawn_sh_in_terminal(void) {
    if (shell_running) return true;

    int in_fds[2];
    int out_fds[2];
    if (pipe(in_fds) != 0 || pipe(out_fds) != 0) {
        term_append_output_with_control("[terminal] pipe() failed\n", 25);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_fds[0]); close(in_fds[1]);
        close(out_fds[0]); close(out_fds[1]);
        term_append_output_with_control("[terminal] fork() failed\n", 25);
        return false;
    }

    if (pid == 0) {
        dup2(in_fds[0], 0);
        dup2(out_fds[1], 1);
        dup2(out_fds[1], 2);

        close(in_fds[0]);  close(in_fds[1]);
        close(out_fds[0]); close(out_fds[1]);

        char *argv[] = {
            (char *)"sh",
            (char *)"--gui",
            nullptr
        };

        char *envp[] = {
            nullptr
        };

        sys_execve("bin/sh.elf", argv, envp);
        sys_execve("/bin/sh.elf", argv, envp);
        sys_exit(127);
    }

    close(in_fds[0]);
    close(out_fds[1]);

    shell_pid = pid;
    shell_stdin_w = in_fds[1];
    shell_stdout_r = out_fds[0];
    shell_running = true;
    return true;
}

// Перехватчик клавиатуры: перенаправляет нажатия прямо в stdin шелла
// Перехватчик клавиатуры: перенаправляет нажатия прямо в stdin шелла
static void term_key_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_KEY) return;

    if (!shell_running || shell_stdin_w < 0) return;

    uint32_t key = lv_indev_get_key(lv_indev_active());
    char c = 0;

    if (key == LV_KEY_ENTER) {
        c = '\n';
    } else if (key == LV_KEY_BACKSPACE) {
        c = '\b'; // Отправляем Backspace шеллу
    } else if (key >= 32 && key <= 126) {
        c = (char)key; // Любой печатный ASCII символ
    }

    if (c != 0) {
        write(shell_stdin_w, &c, 1);
    }

    // ОСТАНАВЛИВАЕМ ДЕФОЛТНУЮ ОБРАБОТКУ:
    // Текстовое поле само не будет печатать символы и стирать их!
    lv_event_stop_processing(e); 
}

void terminal_app_init() {
    lv_obj_t *content = create_custom_window("Terminal - sh.elf", 520, 380, &win_terminal);

    // Терминал на всю контентную область
    term_history = lv_textarea_create(content);
    lv_obj_set_size(term_history, LV_PCT(100), LV_PCT(100));
    lv_obj_align(term_history, LV_ALIGN_TOP_MID, 0, 0);
    
    // Защита от ручного изменения пользователем, писать можно только через ядро
    lv_textarea_set_cursor_click_pos(term_history, false);
    lv_textarea_set_one_line(term_history, false);

    lv_obj_set_style_bg_color(term_history, lv_color_hex(0x0A0B0E), LV_PART_MAIN);
    lv_obj_set_style_text_color(term_history, lv_color_hex(0x33FF33), LV_PART_MAIN);

    // Включаем поддержку recolor для перевода ANSI-цветов в теги LVGL
    lv_obj_t *label = lv_textarea_get_label(term_history);
    if (label) {
        lv_label_set_recolor(label, true);
    }

    // Регистрируем перехват клавиш
    lv_obj_add_event_cb(term_history, term_key_event_handler, LV_EVENT_KEY, nullptr);

    // Выводим приветствие
    term_append_output_with_control("--- EquinoxOS Terminal (sh.elf interactive) ---\nLaunching /bin/sh.elf...\n", 80);

    shell_poll_timer = lv_timer_create(shell_poll_cb, 50, nullptr);

    spawn_sh_in_terminal();

    // Переводим фокус на терминал, чтобы можно было писать сразу
    lv_group_focus_obj(term_history);
}
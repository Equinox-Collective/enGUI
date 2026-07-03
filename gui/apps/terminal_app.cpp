#include "terminal_app.h"
#include "../desktop.h"
#include <equos.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* Минимальный poll() через Linux-шлюз int 0x81 (syscall 7). */
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
static lv_obj_t *term_input = nullptr;

static pid_t shell_pid = -1;
static int shell_stdin_w = -1;   /* пишем команды sh.elf */
static int shell_stdout_r = -1;  /* читаем вывод sh.elf */
static bool shell_running = false;
static lv_timer_t *shell_poll_timer = nullptr;

/* Убираем базовые ANSI SGR-последовательности — LVGL их не рендерит. */
static size_t strip_ansi(const char *in, size_t in_len, char *out, size_t out_cap) {
    size_t o = 0;
    for (size_t i = 0; i < in_len && o + 1 < out_cap; ) {
        if (in[i] == '\x1b' && i + 1 < in_len && in[i + 1] == '[') {
            i += 2;
            while (i < in_len && in[i] != 'm') i++;
            if (i < in_len) i++;
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = '\0';
    return o;
}

static void term_append_output(const char *text) {
    if (!term_history || !text || !*text) return;
    lv_textarea_add_text(term_history, text);
    lv_textarea_set_cursor_pos(term_history, LV_TEXTAREA_CURSOR_LAST);
}

static void term_on_shell_exit(void) {
    shell_running = false;
    term_append_output("\n[sh.elf завершился]\n");
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
    char clean[512];
    strip_ansi(raw, (size_t)n, clean, sizeof(clean));
    term_append_output(clean);
}

static bool spawn_sh_in_terminal(void) {
    if (shell_running) return true;

    int in_fds[2];
    int out_fds[2];
    if (pipe(in_fds) != 0 || pipe(out_fds) != 0) {
        term_append_output("[terminal] pipe() failed\n");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_fds[0]); close(in_fds[1]);
        close(out_fds[0]); close(out_fds[1]);
        term_append_output("[terminal] fork() failed\n");
        return false;
    }

    if (pid == 0) {
        /* Ребёнок: stdin/stdout/stderr → pipe, не COM1. */
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

        // Гарантированно валидный пустой массив окружения
        char *envp[] = {
            nullptr
        };

        execve("bin/sh.elf", argv, envp);
        execve("/bin/sh.elf", argv, envp);
        sys_exit(127);
    }

    /* Родитель sysgui держит write-конец stdin и read-конец stdout. */
    close(in_fds[0]);
    close(out_fds[1]);

    shell_pid = pid;
    shell_stdin_w = in_fds[1];
    shell_stdout_r = out_fds[0];
    shell_running = true;
    return true;
}

static void term_input_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY) return;

    if (!shell_running) {
        if (!spawn_sh_in_terminal()) return;
    }

    if (shell_stdin_w < 0) return;

    const char *cmd = lv_textarea_get_text(term_input);
    if (!cmd || strlen(cmd) == 0) return;

    size_t len = strlen(cmd);
    write(shell_stdin_w, cmd, len);
    write(shell_stdin_w, "\n", 1);

    lv_textarea_set_text(term_input, "");
}

void terminal_app_init() {
    lv_obj_t *content = create_custom_window("Terminal — sh.elf", 520, 380, &win_terminal);

    term_history = lv_textarea_create(content);
    lv_obj_set_size(term_history, LV_PCT(100), LV_PCT(82));
    lv_obj_align(term_history, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_text(term_history,
        "--- EquinoxOS Terminal (sh.elf via pipe) ---\n"
        "Запуск /bin/sh.elf...\n");
    lv_obj_set_style_bg_color(term_history, lv_color_hex(0x0A0B0E), LV_PART_MAIN);
    lv_obj_set_style_text_color(term_history, lv_color_hex(0x33FF33), LV_PART_MAIN);
    lv_textarea_set_cursor_click_pos(term_history, false);
    lv_textarea_set_one_line(term_history, false);

    term_input = lv_textarea_create(content);
    lv_obj_set_size(term_input, LV_PCT(100), 32);
    lv_obj_align(term_input, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_textarea_set_one_line(term_input, true);
    lv_textarea_set_placeholder_text(term_input, "команда sh.elf...");
    lv_obj_set_style_bg_color(term_input, lv_color_hex(0x14161B), LV_PART_MAIN);
    lv_obj_set_style_text_color(term_input, lv_color_hex(0xE6E6E6), LV_PART_MAIN);
    lv_obj_add_event_cb(term_input, term_input_event_handler, LV_EVENT_READY, nullptr);

    shell_poll_timer = lv_timer_create(shell_poll_cb, 50, nullptr);

    spawn_sh_in_terminal();
}

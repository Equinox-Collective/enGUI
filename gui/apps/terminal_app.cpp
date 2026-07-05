#include "terminal_app.h"
#include "../desktop.h"
#include <equos.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
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
        : "a"((long)7), "D"((uint64_t)fds), "S"((long)nfds), "d"((long)timeout_ms)
        : "rcx", "r11", "memory");
    return (int)ret;
}

lv_obj_t *win_terminal = nullptr;
static lv_obj_t *term_label = nullptr;

static pid_t shell_pid = -1;
static int shell_stdin_w = -1;   /* write commands to sh.elf */
static int shell_stdout_r = -1;  /* read output from sh.elf */
static bool shell_running = false;
static lv_timer_t *shell_poll_timer = nullptr;

// TTY Terminal Buffer Configuration
#define TERM_ROWS 20
#define TERM_COLS 75

struct TerminalCell {
    char c = ' ';
    uint32_t fg = 0x33FF33; // Default matrix green
};

struct TerminalEmulator {
    TerminalCell grid[TERM_ROWS][TERM_COLS];
    int cursor_y = 0;
    int cursor_x = 0;
    uint32_t current_fg = 0x33FF33;

    enum ParserState {
        STATE_NORMAL,
        STATE_ESC,
        STATE_CSI
    } state = STATE_NORMAL;

    char csi_buf[64];
    int csi_len = 0;

    void clear() {
        for (int r = 0; r < TERM_ROWS; r++) {
            for (int c = 0; c < TERM_COLS; c++) {
                grid[r][c].c = ' ';
                grid[r][c].fg = 0x33FF33;
            }
        }
        cursor_y = 0;
        cursor_x = 0;
        state = STATE_NORMAL;
    }

    void scroll() {
        for (int r = 0; r < TERM_ROWS - 1; r++) {
            std::memcpy(grid[r], grid[r + 1], sizeof(TerminalCell) * TERM_COLS);
        }
        for (int c = 0; c < TERM_COLS; c++) {
            grid[TERM_ROWS - 1][c].c = ' ';
            grid[TERM_ROWS - 1][c].fg = 0x33FF33;
        }
        cursor_y = TERM_ROWS - 1;
    }

    void write_char(char c) {
        switch (state) {
            case STATE_NORMAL:
                if (c == '\x1b') {
                    state = STATE_ESC;
                } else if (c == '\n') {
                    cursor_x = 0;
                    cursor_y++;
                    if (cursor_y >= TERM_ROWS) {
                        scroll();
                    }
                } else if (c == '\r') {
                    cursor_x = 0;
                } else if (c == '\b' || c == 127) {
                    if (cursor_x > 0) {
                        cursor_x--;
                    }
                } else if (c == '\t') {
                    int next_tab = (cursor_x + 8) & ~7;
                    while (cursor_x < next_tab && cursor_x < TERM_COLS) {
                        grid[cursor_y][cursor_x].c = ' ';
                        grid[cursor_y][cursor_x].fg = current_fg;
                        cursor_x++;
                    }
                    if (cursor_x >= TERM_COLS) {
                        cursor_x = 0;
                        cursor_y++;
                        if (cursor_y >= TERM_ROWS) scroll();
                    }
                } else if ((unsigned char)c >= 32) {
                    grid[cursor_y][cursor_x].c = c;
                    grid[cursor_y][cursor_x].fg = current_fg;
                    cursor_x++;
                    if (cursor_x >= TERM_COLS) {
                        cursor_x = 0;
                        cursor_y++;
                        if (cursor_y >= TERM_ROWS) {
                            scroll();
                        }
                    }
                }
                break;

            case STATE_ESC:
                if (c == '[') {
                    state = STATE_CSI;
                    csi_len = 0;
                    std::memset(csi_buf, 0, sizeof(csi_buf));
                } else {
                    state = STATE_NORMAL;
                }
                break;

            case STATE_CSI:
                if ((c >= '0' && c <= '9') || c == ';') {
                    if (csi_len < (int)sizeof(csi_buf) - 1) {
                        csi_buf[csi_len++] = c;
                    }
                } else {
                    csi_buf[csi_len] = '\0';
                    process_csi(c);
                    state = STATE_NORMAL;
                }
                break;
        }
    }

    void process_csi(char cmd) {
        if (cmd == 'm') {
            if (csi_len == 0) {
                current_fg = 0x33FF33;
                return;
            }
            char* token = std::strtok(csi_buf, ";");
            while (token != nullptr) {
                int val = std::atoi(token);
                if (val == 0) {
                    current_fg = 0xE6E6E6; // Reset to soft gray-white
                } else if (val == 30) {
                    current_fg = 0x1A1A1A; // Dark Black
                } else if (val == 31) {
                    current_fg = 0xFF5555; // Red
                } else if (val == 32) {
                    current_fg = 0x33FF33; // Green
                } else if (val == 33) {
                    current_fg = 0xFFFF55; // Yellow
                } else if (val == 34) {
                    current_fg = 0x5588FF; // Blue
                } else if (val == 35) {
                    current_fg = 0xFF55FF; // Magenta
                } else if (val == 36) {
                    current_fg = 0x6FA8DC; // Cyan
                } else if (val == 37) {
                    current_fg = 0xE6E6E6; // White
                } else if (val == 39) {
                    current_fg = 0x33FF33; // Reset to green
                }
                token = std::strtok(nullptr, ";");
            }
        } else if (cmd == 'J') {
            int val = std::atoi(csi_buf);
            if (val == 2) {
                clear();
            }
        } else if (cmd == 'H' || cmd == 'f') {
            int r = 1, col = 1;
            char* semi = std::strchr(csi_buf, ';');
            if (semi) {
                *semi = '\0';
                r = std::atoi(csi_buf);
                col = std::atoi(semi + 1);
            } else if (csi_len > 0) {
                r = std::atoi(csi_buf);
            }
            cursor_y = r - 1;
            cursor_x = col - 1;
            if (cursor_y < 0) cursor_y = 0;
            if (cursor_y >= TERM_ROWS) cursor_y = TERM_ROWS - 1;
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_x >= TERM_COLS) cursor_x = TERM_COLS - 1;
        } else if (cmd == 'K') {
            for (int c = cursor_x; c < TERM_COLS; c++) {
                grid[cursor_y][c].c = ' ';
                grid[cursor_y][c].fg = current_fg;
            }
        }
    }
};

static TerminalEmulator g_term;

static void term_render_to_ui() {
    if (!term_label) return;

    std::string out;
    out.reserve(TERM_ROWS * TERM_COLS * 5);

    for (int r = 0; r < TERM_ROWS; r++) {
        uint32_t last_fg = 0xFFFFFFFF;
        for (int c = 0; c < TERM_COLS; c++) {
            bool is_cursor = (r == g_term.cursor_y && c == g_term.cursor_x);
            uint32_t fg = g_term.grid[r][c].fg;
            char ch = g_term.grid[r][c].c;

            if (is_cursor) {
                out += "#FFFFFF █#"; // Character reverse visual block cursor
                last_fg = 0xFFFFFFFF;
            } else {
                if (fg != last_fg) {
                    char color_tag[32];
                    std::snprintf(color_tag, sizeof(color_tag), "#%06X ", fg & 0xFFFFFF);
                    out += color_tag;
                    last_fg = fg;
                }
                if (ch == '#') {
                    out += "##"; // Escape recolor prefix
                } else if (ch == '\0' || ch == ' ') {
                    out += " ";
                } else {
                    out += ch;
                }
            }
        }
        out += "\n";
    }

    lv_label_set_text(term_label, out.c_str());
}

static void term_append_output_with_control(const char *text, size_t len) {
    if (!term_label || !text || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        g_term.write_char(text[i]);
    }
    term_render_to_ui();
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

static void term_key_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_KEY) return;

    if (!shell_running || shell_stdin_w < 0) return;

    uint32_t key = lv_indev_get_key(lv_indev_active());
    char c = 0;

    if (key == LV_KEY_ENTER) {
        c = '\n';
    } else if (key == LV_KEY_BACKSPACE) {
        c = '\b'; 
    } else if (key >= 32 && key <= 126) {
        c = (char)key; 
    }

    if (c != 0) {
        write(shell_stdin_w, &c, 1);
    }

    lv_event_stop_processing(e); 
}

void terminal_app_init() {
    lv_obj_t *content = create_custom_window("Terminal - sh.elf", 520, 380, &win_terminal);

    // Dark terminal canvas styling
    lv_obj_set_style_bg_color(content, lv_color_hex(0x0A0B0E), LV_PART_MAIN);

    g_term.clear();

    // Monospace label setup inside the container
    term_label = lv_label_create(content);
    lv_obj_set_size(term_label, LV_PCT(100), LV_PCT(100));
    lv_obj_align(term_label, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_label_set_recolor(term_label, true);

    // Style the text
    lv_obj_set_style_text_color(term_label, lv_color_hex(0x33FF33), LV_PART_MAIN);

    // Add keyboard hook directly on label
    lv_obj_add_event_cb(term_label, term_key_event_handler, LV_EVENT_KEY, nullptr);
    lv_obj_add_flag(term_label, LV_OBJ_FLAG_CLICKABLE);
    lv_group_focus_obj(term_label);

    shell_poll_timer = lv_timer_create(shell_poll_cb, 50, nullptr);

    spawn_sh_in_terminal();
    term_render_to_ui();
}
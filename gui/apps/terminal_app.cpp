/* =============================================================================
 * EquinoxOS — Terminal Emulator Application (Pure Musl C + LVGL v9)
 * =============================================================================
 */

#include "terminal_app.h"
#include "../desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <equos.h>

/* -----------------------------------------------------------------------------
 * Syscall gateway & Poll definition (Linux compat int 0x81)
 * -----------------------------------------------------------------------------
 */
struct equos_pollfd {
    int fd;
    short events;
    short revents;
};

#define EQUOS_POLLIN   0x001
#define EQUOS_POLLHUP  0x010
#define EQUOS_POLLERR  0x008
#define EQUOS_POLLNVAL 0x020

static int equos_poll(struct equos_pollfd *fds, int nfds, int timeout_ms) {
    long ret;
    __asm__ volatile("int $0x81"
        : "=a"(ret)
        : "a"((long)7), "D"((uint64_t)fds), "S"((long)nfds), "d"((long)timeout_ms)
        : "rcx", "r11", "memory");
    return (int)ret;
}

/* -----------------------------------------------------------------------------
 * Terminal Geometry & Color Palette Configuration
 * -----------------------------------------------------------------------------
 */
#define TERM_ROWS 22
#define TERM_COLS 74

/* Cyberpunk / Matrix Neo Dark Palette */
#define COLOR_DEFAULT_FG 0x50FA7B  /* Matrix Green */
#define COLOR_DEFAULT_BG 0x0C0D11  /* Deep Obsidian */

#define COLOR_BLACK      0x21222C
#define COLOR_RED        0xFF5555
#define COLOR_GREEN      0x50FA7B
#define COLOR_YELLOW     0xF1FA8C
#define COLOR_BLUE       0xBD93F9
#define COLOR_MAGENTA    0xFF79C6
#define COLOR_CYAN       0x8BE9FD
#define COLOR_WHITE      0xF8F8F2

#define COLOR_BRIGHT_BLK 0x6272A4
#define COLOR_BRIGHT_RED 0xFF6E6E
#define COLOR_BRIGHT_GRN 0x69FF94
#define COLOR_BRIGHT_YEL 0xFFFA9E
#define COLOR_BRIGHT_BLU 0xD6ACFF
#define COLOR_BRIGHT_MAG 0xFF92D0
#define COLOR_BRIGHT_CYN 0xA4F0FF
#define COLOR_BRIGHT_WHT 0xFFFFFF

/* -----------------------------------------------------------------------------
 * Terminal Emulator Grid Structure
 * -----------------------------------------------------------------------------
 */
typedef struct {
    char c;
    uint32_t fg;
} term_cell_t;

typedef enum {
    STATE_NORMAL = 0,
    STATE_ESC,
    STATE_CSI
} term_parser_state_t;

typedef struct {
    term_cell_t grid[TERM_ROWS][TERM_COLS];
    int cursor_x;
    int cursor_y;
    uint32_t current_fg;
    term_parser_state_t state;
    char csi_buf[64];
    int csi_len;
    bool cursor_visible;
} term_emulator_t;

/* Global state definitions */
lv_obj_t *win_terminal = NULL;
static lv_obj_t *term_label = NULL;
static lv_obj_t *status_label = NULL;

static pid_t shell_pid = -1;
static int shell_stdin_w = -1;
static int shell_stdout_r = -1;
static bool shell_running = false;

static lv_timer_t *shell_poll_timer = NULL;
static term_emulator_t g_term;

/* Dynamic rendering buffer allocated in Musl memory */
static char render_buf[32768];

/* -----------------------------------------------------------------------------
 * Terminal Core Engine & ANSI Parser
 * -----------------------------------------------------------------------------
 */
static void term_reset(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            g_term.grid[r][c].c = ' ';
            g_term.grid[r][c].fg = COLOR_DEFAULT_FG;
        }
    }
    g_term.cursor_x = 0;
    g_term.cursor_y = 0;
    g_term.current_fg = COLOR_DEFAULT_FG;
    g_term.state = STATE_NORMAL;
    g_term.csi_len = 0;
    g_term.cursor_visible = true;
}

static void term_scroll_up(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        memcpy(g_term.grid[r], g_term.grid[r + 1], sizeof(term_cell_t) * TERM_COLS);
    }
    for (int c = 0; c < TERM_COLS; c++) {
        g_term.grid[TERM_ROWS - 1][c].c = ' ';
        g_term.grid[TERM_ROWS - 1][c].fg = COLOR_DEFAULT_FG;
    }
    g_term.cursor_y = TERM_ROWS - 1;
}

static void term_process_csi_m(const char *buf) {
    if (!buf || *buf == '\0') {
        g_term.current_fg = COLOR_DEFAULT_FG;
        return;
    }

    char tmp[64];
    strncpy(tmp, buf, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *token = strtok(tmp, ";");
    while (token != NULL) {
        int val = atoi(token);
        switch (val) {
            case 0:  g_term.current_fg = COLOR_WHITE; break;
            case 30: g_term.current_fg = COLOR_BLACK; break;
            case 31: g_term.current_fg = COLOR_RED; break;
            case 32: g_term.current_fg = COLOR_GREEN; break;
            case 33: g_term.current_fg = COLOR_YELLOW; break;
            case 34: g_term.current_fg = COLOR_BLUE; break;
            case 35: g_term.current_fg = COLOR_MAGENTA; break;
            case 36: g_term.current_fg = COLOR_CYAN; break;
            case 37: g_term.current_fg = COLOR_WHITE; break;
            case 39: g_term.current_fg = COLOR_DEFAULT_FG; break;

            case 90: g_term.current_fg = COLOR_BRIGHT_BLK; break;
            case 91: g_term.current_fg = COLOR_BRIGHT_RED; break;
            case 92: g_term.current_fg = COLOR_BRIGHT_GRN; break;
            case 93: g_term.current_fg = COLOR_BRIGHT_YEL; break;
            case 94: g_term.current_fg = COLOR_BRIGHT_BLU; break;
            case 95: g_term.current_fg = COLOR_BRIGHT_MAG; break;
            case 96: g_term.current_fg = COLOR_BRIGHT_CYN; break;
            case 97: g_term.current_fg = COLOR_BRIGHT_WHT; break;
            default: break;
        }
        token = strtok(NULL, ";");
    }
}

static void term_process_csi(char cmd) {
    switch (cmd) {
        case 'm':
            term_process_csi_m(g_term.csi_buf);
            break;

        case 'J':
            if (atoi(g_term.csi_buf) == 2) {
                term_reset();
            }
            break;

        case 'K': {
            int mode = atoi(g_term.csi_buf);
            if (mode == 0) { /* Clear from cursor to end of line */
                for (int c = g_term.cursor_x; c < TERM_COLS; c++) {
                    g_term.grid[g_term.cursor_y][c].c = ' ';
                    g_term.grid[g_term.cursor_y][c].fg = g_term.current_fg;
                }
            }
            break;
        }

        case 'H':
        case 'f': {
            int row = 1, col = 1;
            char *semi = strchr(g_term.csi_buf, ';');
            if (semi) {
                *semi = '\0';
                row = atoi(g_term.csi_buf);
                col = atoi(semi + 1);
            } else if (g_term.csi_len > 0) {
                row = atoi(g_term.csi_buf);
            }
            g_term.cursor_y = row - 1;
            g_term.cursor_x = col - 1;
            if (g_term.cursor_y < 0) g_term.cursor_y = 0;
            if (g_term.cursor_y >= TERM_ROWS) g_term.cursor_y = TERM_ROWS - 1;
            if (g_term.cursor_x < 0) g_term.cursor_x = 0;
            if (g_term.cursor_x >= TERM_COLS) g_term.cursor_x = TERM_COLS - 1;
            break;
        }

        default:
            break;
    }
}

static void term_write_char(char c) {
    switch (g_term.state) {
        case STATE_NORMAL:
            if (c == '\x1b') {
                g_term.state = STATE_ESC;
            } else if (c == '\n') {
                g_term.cursor_x = 0;
                g_term.cursor_y++;
                if (g_term.cursor_y >= TERM_ROWS) {
                    term_scroll_up();
                }
            } else if (c == '\r') {
                g_term.cursor_x = 0;
            } else if (c == '\b' || c == 127) {
                if (g_term.cursor_x > 0) {
                    g_term.cursor_x--;
                }
            } else if (c == '\t') {
                int next_tab = (g_term.cursor_x + 8) & ~7;
                while (g_term.cursor_x < next_tab && g_term.cursor_x < TERM_COLS) {
                    g_term.grid[g_term.cursor_y][g_term.cursor_x].c = ' ';
                    g_term.grid[g_term.cursor_y][g_term.cursor_x].fg = g_term.current_fg;
                    g_term.cursor_x++;
                }
                if (g_term.cursor_x >= TERM_COLS) {
                    g_term.cursor_x = 0;
                    g_term.cursor_y++;
                    if (g_term.cursor_y >= TERM_ROWS) term_scroll_up();
                }
            } else if ((unsigned char)c >= 32) {
                g_term.grid[g_term.cursor_y][g_term.cursor_x].c = c;
                g_term.grid[g_term.cursor_y][g_term.cursor_x].fg = g_term.current_fg;
                g_term.cursor_x++;
                if (g_term.cursor_x >= TERM_COLS) {
                    g_term.cursor_x = 0;
                    g_term.cursor_y++;
                    if (g_term.cursor_y >= TERM_ROWS) {
                        term_scroll_up();
                    }
                }
            }
            break;

        case STATE_ESC:
            if (c == '[') {
                g_term.state = STATE_CSI;
                g_term.csi_len = 0;
                memset(g_term.csi_buf, 0, sizeof(g_term.csi_buf));
            } else {
                g_term.state = STATE_NORMAL;
            }
            break;

        case STATE_CSI:
            if ((c >= '0' && c <= '9') || c == ';') {
                if (g_term.csi_len < (int)sizeof(g_term.csi_buf) - 1) {
                    g_term.csi_buf[g_term.csi_len++] = c;
                }
            } else {
                g_term.csi_buf[g_term.csi_len] = '\0';
                term_process_csi(c);
                g_term.state = STATE_NORMAL;
            }
            break;
    }
}

/* -----------------------------------------------------------------------------
 * UI High-Performance Renderer
 * Pack chars into LVGL `#RRGGBB text#` color tags with `#` escaping
 * -----------------------------------------------------------------------------
 */
static void term_render_to_ui(void) {
    if (!term_label) return;

    size_t pos = 0;
    uint32_t active_fg = 0xFFFFFFFF;
    bool in_tag = false;

    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            bool is_cursor = (r == g_term.cursor_y && c == g_term.cursor_x && g_term.cursor_visible);
            term_cell_t cell = g_term.grid[r][c];

            if (is_cursor) {
                if (in_tag) {
                    if (pos + 1 < sizeof(render_buf)) render_buf[pos++] = '#';
                    in_tag = false;
                }
                const char *cursor_str = "#FFFFFF █#";
                size_t clen = strlen(cursor_str);
                if (pos + clen < sizeof(render_buf)) {
                    memcpy(&render_buf[pos], cursor_str, clen);
                    pos += clen;
                }
                active_fg = 0xFFFFFFFF;
            } else {
                if (cell.fg != active_fg) {
                    if (in_tag) {
                        if (pos + 1 < sizeof(render_buf)) render_buf[pos++] = '#';
                        in_tag = false;
                    }
                    int n = snprintf(&render_buf[pos], sizeof(render_buf) - pos, "#%06X ", cell.fg & 0xFFFFFF);
                    if (n > 0) pos += (size_t)n;
                    active_fg = cell.fg;
                    in_tag = true;
                }

                if (cell.c == '#') {
                    /* Escape '#' for LVGL recolor parser */
                    if (pos + 2 < sizeof(render_buf)) {
                        render_buf[pos++] = '#';
                        render_buf[pos++] = '#';
                    }
                } else if (cell.c == '\0' || cell.c == ' ') {
                    if (pos + 1 < sizeof(render_buf)) {
                        render_buf[pos++] = ' ';
                    }
                } else {
                    if (pos + 1 < sizeof(render_buf)) {
                        render_buf[pos++] = cell.c;
                    }
                }
            }
        }

        if (in_tag) {
            if (pos + 1 < sizeof(render_buf)) render_buf[pos++] = '#';
            in_tag = false;
            active_fg = 0xFFFFFFFF;
        }

        if (pos + 1 < sizeof(render_buf)) {
            render_buf[pos++] = '\n';
        }
    }

    render_buf[pos] = '\0';
    lv_label_set_text(term_label, render_buf);
}

/* -----------------------------------------------------------------------------
 * Process & I/O Communication with sh.elf
 * -----------------------------------------------------------------------------
 */
static void term_write_string(const char *str) {
    if (!str) return;
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        term_write_char(str[i]);
    }
}

static void term_on_shell_exit(void) {
    shell_running = false;
    term_write_string("\n\x1b[31m[sh.elf process terminated]\x1b[0m\n");
    term_render_to_ui();

    if (shell_stdin_w >= 0) { close(shell_stdin_w); shell_stdin_w = -1; }
    if (shell_stdout_r >= 0) { close(shell_stdout_r); shell_stdout_r = -1; }
    shell_pid = -1;

    if (status_label) {
        lv_label_set_text(status_label, "Status: EXITED");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF5555), LV_PART_MAIN);
    }
}

static void shell_poll_cb(lv_timer_t *timer) {
    (void)timer;

    /* Handle Cursor Blink Every 500ms */
    static uint32_t blink_counter = 0;
    blink_counter++;
    if (blink_counter % 10 == 0) {
        g_term.cursor_visible = !g_term.cursor_visible;
        term_render_to_ui();
    }

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

    char raw[1024];
    ssize_t n = read(shell_stdout_r, raw, sizeof(raw) - 1);
    if (n <= 0) {
        if (n == 0) term_on_shell_exit();
        return;
    }

    for (ssize_t i = 0; i < n; i++) {
        term_write_char(raw[i]);
    }
    term_render_to_ui();
}

static bool spawn_sh_in_terminal(void) {
    if (shell_running) return true;

    int in_fds[2];
    int out_fds[2];

    if (pipe(in_fds) != 0 || pipe(out_fds) != 0) {
        term_write_string("[terminal_app] pipe() allocation failed\n");
        term_render_to_ui();
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_fds[0]);  close(in_fds[1]);
        close(out_fds[0]); close(out_fds[1]);
        term_write_string("[terminal_app] fork() process failed\n");
        term_render_to_ui();
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
            NULL
        };

        char *envp[] = {
            (char *)"TERM=equos-lvgl",
            (char *)"EQUINOS_GUI=1",
            NULL
        };

        sys_execve("/bin/sh.elf", argv, envp);
        sys_execve("bin/sh.elf",  argv, envp);
        sys_execve("sh.elf",      argv, envp);
        sys_exit(127);
    }

    close(in_fds[0]);
    close(out_fds[1]);

    shell_pid = pid;
    shell_stdin_w = in_fds[1];
    shell_stdout_r = out_fds[0];
    shell_running = true;

    if (status_label) {
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "TTY: /bin/sh.elf (PID: %d)", (int)shell_pid);
        lv_label_set_text(status_label, sbuf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x50FA7B), LV_PART_MAIN);
    }

    return true;
}

/* -----------------------------------------------------------------------------
 * Keyboard Input Event Dispatcher
 * -----------------------------------------------------------------------------
 */
static void term_key_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    if (!shell_running || shell_stdin_w < 0) return;

    uint32_t key = lv_indev_get_key(lv_indev_active());
    const char *seq = NULL;
    char single_c[2] = {0, 0};

    switch (key) {
        case LV_KEY_ENTER:     seq = "\n"; break;
        case LV_KEY_BACKSPACE: seq = "\b"; break;
        case LV_KEY_ESC:       seq = "\x1b"; break;
        case LV_KEY_UP:        seq = "\x1b[A"; break;
        case LV_KEY_DOWN:      seq = "\x1b[B"; break;
        case LV_KEY_RIGHT:     seq = "\x1b[C"; break;
        case LV_KEY_LEFT:      seq = "\x1b[D"; break;
        default:
            if (key >= 32 && key <= 126) {
                single_c[0] = (char)key;
                seq = single_c;
            }
            break;
    }

    if (seq) {
        write(shell_stdin_w, seq, strlen(seq));
    }

    lv_event_stop_processing(e);
}

/* -----------------------------------------------------------------------------
 * Explicit Focus Grabber on Mouse Click/Press
 * -----------------------------------------------------------------------------
 */
static void term_click_focus_handler(lv_event_t *e) {
    (void)e;
    lv_group_t *g = lv_group_get_default();
    if (g && term_label) {
        /* Принудительно передаем фокус ввода клавиатуры на текстовое поле терминала */
        lv_group_focus_obj(term_label);
    }
}

/* -----------------------------------------------------------------------------
 * Terminal UI Construction Entrypoint
 * -----------------------------------------------------------------------------
 */
void terminal_app_init(void) {
    /* Создание кастомного окна через Desktop Factory */
    lv_obj_t *content = create_custom_window("Terminal — sh.elf", 540, 400, &win_terminal);
    if (!content) return;

    /* Стилизация основного фонового контейнера */
    lv_obj_set_style_bg_color(content, lv_color_hex(0x0C0D11), LV_PART_MAIN);
    
    /* Делаем контент кликабельным, чтобы перехватывать клики мыши */
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content, term_click_focus_handler, LV_EVENT_PRESSED, NULL);

    /* Верхняя информационная панель статуса сессии */
    lv_obj_t *top_panel = lv_obj_create(content);
    lv_obj_set_size(top_panel, LV_PCT(100), 24);
    lv_obj_align(top_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_panel, lv_color_hex(0x16181F), LV_PART_MAIN);
    lv_obj_set_style_border_color(top_panel, lv_color_hex(0x282C37), LV_PART_MAIN);
    lv_obj_set_style_border_width(top_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_panel, 2, LV_PART_MAIN);
    lv_obj_remove_flag(top_panel, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(top_panel);
    lv_label_set_text(status_label, "TTY: Initializing...");
    lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x8BE9FD), LV_PART_MAIN);

    /* Внутренний контейнер самой TTY консоли */
    lv_obj_t *term_box = lv_obj_create(content);
    lv_obj_set_size(term_box, LV_PCT(100), 332);
    lv_obj_align(term_box, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(term_box, lv_color_hex(0x060709), LV_PART_MAIN);
    lv_obj_set_style_border_color(term_box, lv_color_hex(0x1F232D), LV_PART_MAIN);
    lv_obj_set_style_border_width(term_box, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(term_box, 4, LV_PART_MAIN);
    lv_obj_remove_flag(term_box, LV_OBJ_FLAG_SCROLLABLE);

    /* Делаем рамку консоли кликабельной */
    lv_obj_add_flag(term_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(term_box, term_click_focus_handler, LV_EVENT_PRESSED, NULL);

    /* Canvas Label с поддержкой тегов раскраски текста */
    term_label = lv_label_create(term_box);
    lv_obj_set_size(term_label, LV_PCT(100), LV_PCT(100));
    lv_obj_align(term_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(term_label, true);

    /* Делаем сам текст кликабельным */
    lv_obj_add_flag(term_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(term_label, term_click_focus_handler, LV_EVENT_PRESSED, NULL);

    /* Перехват событий ввода с клавиатуры */
    lv_obj_add_event_cb(term_label, term_key_event_handler, LV_EVENT_KEY, NULL);

    /* Добавляем в дефолтную группу ввода, чтобы он мог принимать фокус клавиатуры */
    lv_group_t *def_group = lv_group_get_default();
    if (def_group) {
        lv_group_add_obj(def_group, term_label);
        lv_group_focus_obj(term_label);
    }

    /* Сброс эмулятора и запуск процесс-поллера (каждые 50 мс) */
    term_reset();
    shell_poll_timer = lv_timer_create(shell_poll_cb, 50, NULL);

    /* Запуск sh.elf внутри терминала */
    spawn_sh_in_terminal();
    term_render_to_ui();
}
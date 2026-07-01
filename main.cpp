#include "api_gui.h"
#include "gui/desktop.h"
#include "gui/panel.h"
#include "gui/dock.h"
#include "gui/win_manager.h"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <equos.h>
#include <stdio.h>
}

uint32_t *vram = nullptr;
uint32_t *backbuffer = nullptr;
uint32_t *draw_target = nullptr;
uint32_t screen_w = 0, screen_h = 0;

#define TILE_SIZE 32
static uint8_t *dirty_grid = nullptr;
static int grid_cols = 0, grid_rows = 0;

void sysgui_init_dirty_grid() {
    grid_cols = (screen_w + TILE_SIZE - 1) / TILE_SIZE;
    grid_rows = (screen_h + TILE_SIZE - 1) / TILE_SIZE;
    dirty_grid = (uint8_t *)malloc(grid_cols * grid_rows);
    memset(dirty_grid, 1, grid_cols * grid_rows);
}

void sysgui_mark_dirty(int x, int y, int w, int h) {
    if (!dirty_grid) return;
    int x2 = x + w, y2 = y + h;
    int start_r = y / TILE_SIZE;
    int end_r = y2 / TILE_SIZE;
    int start_c = x / TILE_SIZE;
    int end_c = x2 / TILE_SIZE;

    if (start_r < 0) start_r = 0;
    if (start_c < 0) start_c = 0;
    if (end_r >= grid_rows) end_r = grid_rows - 1;
    if (end_c >= grid_cols) end_c = grid_cols - 1;

    for (int r = start_r; r <= end_r; r++) {
        for (int c = start_c; c <= end_c; c++) {
            dirty_grid[r * grid_cols + c] = 1;
        }
    }
}

void copy_dirty_to_vram() {
    if (!dirty_grid) {
        memcpy(vram, backbuffer, screen_w * screen_h * 4);
        return;
    }

    for (int r = 0; r < grid_rows; r++) {
        int y_start = r * TILE_SIZE;
        int y_end = y_start + TILE_SIZE;
        if (y_end > (int)screen_h) y_end = screen_h;
        int tile_h = y_end - y_start;

        int c = 0;
        while (c < grid_cols) {
            if (dirty_grid[r * grid_cols + c]) {
                int c_start = c;
                while (c < grid_cols && dirty_grid[r * grid_cols + c]) {
                    dirty_grid[r * grid_cols + c] = 0;
                    c++;
                }
                int c_end = c;

                int x_start = c_start * TILE_SIZE;
                int x_end = c_end * TILE_SIZE;
                if (x_end > (int)screen_w) x_end = screen_w;
                int copy_w = x_end - x_start;

                for (int i = 0; i < tile_h; i++) {
                    int current_y = y_start + i;
                    uint32_t *dst = &vram[current_y * screen_w + x_start];
                    uint32_t *src = &backbuffer[current_y * screen_w + x_start];
                    memcpy(dst, src, copy_w * 4);
                }
            } else {
                c++;
            }
        }
    }
}

static inline void get_mouse_state(int* mx, int* my, bool* mdown) {
    uint64_t r_mx = 0, r_my = 0, r_btn = 0;
    __asm__ volatile(
        "mov $7, %%rax\n\t"
        "int $0x80\n\t"
        "mov %%rax, %0\n\t"
        "mov %%rbx, %1\n\t"
        "mov %%rcx, %2\n\t"
        : "=r"(r_mx), "=r"(r_my), "=r"(r_btn)
        :
        : "rax", "rbx", "rcx", "memory"
    );
    *mx = (int)r_mx;
    *my = (int)r_my;
    *mdown = (r_btn & 1);
}

static inline uint16_t get_key_state() {
    // Системный вызов получения сканкода/клавиши
    uint64_t key = 0;
    __asm__ volatile(
        "mov $5, %%rax\n\t"
        "int $0x80\n\t"
        "mov %%rax, %0\n\t"
        : "=r"(key)
        :
        : "rax", "memory"
    );
    return (uint16_t)key;
}

static const int CURSOR_W = 19;
static const int CURSOR_H = 27;

static const char* s_CursorSprite[27] = {
    "d..................",
    "Bds................",
    "BWd................",
    "BWWds..............",
    "BWWWd..............",
    "BWWWWB.............",
    "BWWWWd.............",
    "BWWWWWB............",
    "BWWWWWd............",
    "BWWWWWWd...........",
    "BWWWWWWWB..........",
    "BWWWWWWWd..........",
    "BWWWWWWWWd.........",
    "BWWWWWWWWWB........",
    "BWWWWWWWWWd........",
    "BWWWWWWWWWWd.......",
    "BWWWWWWBBBBds......",
    "BWWWWBWd.s.........",
    "BWWWB.Bd.s.........",
    "BWWB..Bd.s.........",
    "BWB....Bd.s........",
    "B......Bd.s........",
    "s.......Bd.s.......",
    "........Bd.s.......",
    ".........ds........",
    "..........s........",
    "..................."
};

static inline uint32_t get_cursor_pixel(char c) {
    switch (c) {
        case 'B': return 0xFF000000;
        case 'd': return 0x99000000;
        case 'W': return 0xFFFFFFFF;
        case 's': return 0x2A000000;
        case '.': default: return 0x00000000;
    }
}

extern bool load_psf_font(const char* path);

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t phys_fb = 0;
    uint32_t w = 0, h = 0, pitch = 0;

    _syscall(32, (uint64_t)&phys_fb, (uint64_t)&w, (uint64_t)&h, (uint64_t)&pitch, 0);

    screen_w = w; 
    screen_h = h;

    char msg[128];
    sprintf(msg, "GUI: Sonoma Interface active %ux%u (NATIVE MODE).\n", screen_w, screen_h);
    _syscall(1, (uint64_t)msg, 0, 0, 0, 0);

    // Загружаем системный PSF шрифт
    if (!load_psf_font("res/sysgui/Inter.ttf")) { // Если Inter.ttf у вас скомпилирован как PSF
        load_psf_font("res/font.psf"); // Фолбек на консольный шрифт
    }

    vram = (uint32_t *)_syscall(30, phys_fb, screen_w * screen_h * 4, 0, 0, 0);
    if (!vram) {
        sys_exit(1);
    }
    backbuffer = (uint32_t *)malloc(screen_w * screen_h * 4);
    if (!backbuffer) {
        sys_exit(1);
    }
    draw_target = backbuffer;
    sysgui_init_dirty_grid();

    GUI::InitDesktop();
    GUI::InitDock();
    GUI::InitWindowManager();
    
    uint32_t last_time = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
    int mx = 0, my = 0;
    bool mdown = false;

    GUI::Painter painter(backbuffer, screen_w, screen_h);

    while (true) {
        uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
        float dt = (now - last_time) / 1000.0f;
        last_time = now;

        // Опрос мыши и клавиатуры
        get_mouse_state(&mx, &my, &mdown);
        uint16_t key = get_key_state();

        // Помечаем старую область курсора и новую грязной
        static int last_mx = 0, last_my = 0;
        sysgui_mark_dirty(last_mx, last_my, CURSOR_W, CURSOR_H);
        sysgui_mark_dirty(mx, my, CURSOR_W, CURSOR_H);
        last_mx = mx;
        last_my = my;

        // 1. Отрисовка рабочего стола (обои + Dashboard виджеты)
        GUI::RenderDesktop(painter);
        
        // 2. Обновление поведения окружения
        GUI::UpdateDesktop(dt, mx, my, mdown, key);

        // 3. Рендеринг панелей, дока и менеджера нативных окон
        GUI::RenderTopPanel(painter);
        GUI::RenderDock(painter, mx, my, mdown);
        GUI::RenderWindows(painter, dt, mx, my, mdown, key);

        // 4. Отрисовка софтварного сглаженного курсора поверх бэкбуфера
        for (int i = 0; i < CURSOR_H; i++) {
            for (int j = 0; j < CURSOR_W; j++) {
                char c = s_CursorSprite[i][j];
                uint32_t col = get_cursor_pixel(c);
                int a = (col >> 24) & 0xFF;
                if (a == 0) continue;
                int px = mx + j;
                int py = my + i;
                if (px >= 0 && px < (int)screen_w && py >= 0 && py < (int)screen_h) {
                    if (a == 255) {
                        backbuffer[py * screen_w + px] = col & 0xFFFFFF;
                    } else {
                        uint32_t bg = backbuffer[py * screen_w + px];
                        int r = (col >> 16) & 0xFF, g = (col >> 8) & 0xFF, b = col & 0xFF;
                        int br = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bb = bg & 0xFF;
                        int res_r = (r * a + br * (255 - a)) >> 8;
                        int res_g = (g * a + bg_g * (255 - a)) >> 8;
                        int res_b = (b * a + bb * (255 - a)) >> 8;
                        backbuffer[py * screen_w + px] = (res_r << 16) | (res_g << 8) | res_b;
                    }
                }
            }
        }

        // 5. Копирование измененных тайлов во VRAM
        copy_dirty_to_vram();
        
        // Ограничение FPS (~60)
        _syscall(13, 16, 0, 0, 0, 0); 
    }

    return 0;
}
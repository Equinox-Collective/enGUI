#include "api_gui.h"
#include "gui/desktop.h"
#include "gui/panel.h"
#include "gui/dock.h"
#include "gui/win_manager.h"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <equos.h>
}

#include "imgui/imgui.h"

// --- ГЛОБАЛЬНЫЕ БУФЕРЫ ---
uint32_t *vram = nullptr;
uint32_t *backbuffer = nullptr;
uint32_t *draw_target = nullptr; // Указывает на backbuffer для api_gui
uint32_t screen_w = 0, screen_h = 0;

// --- СИСТЕМА ОПТИМИЗАЦИИ (Dirty Grid) ---
#define TILE_SIZE 32
static uint8_t *dirty_grid = nullptr;
static int grid_cols = 0, grid_rows = 0;

void sysgui_init_dirty_grid() {
    grid_cols = (screen_w + TILE_SIZE - 1) / TILE_SIZE;
    grid_rows = (screen_h + TILE_SIZE - 1) / TILE_SIZE;
    dirty_grid = (uint8_t *)malloc(grid_cols * grid_rows);
    memset(dirty_grid, 1, grid_cols * grid_rows); // Сначала всё грязное
}

void sysgui_mark_dirty(int x, int y, int w, int h) {
    if (!dirty_grid) return;
    int x2 = x + w, y2 = y + h;
    for (int r = y / TILE_SIZE; r <= y2 / TILE_SIZE && r < grid_rows; r++) {
        for (int c = x / TILE_SIZE; c <= x2 / TILE_SIZE && c < grid_cols; c++) {
            if (r >= 0 && c >= 0) dirty_grid[r * grid_cols + c] = 1;
        }
    }
}

void copy_dirty_to_vram() {
    for (int r = 0; r < grid_rows; r++) {
        for (int c = 0; c < grid_cols; c++) {
            if (dirty_grid[r * grid_cols + c]) {
                int x = c * TILE_SIZE;
                int y = r * TILE_SIZE;
                for (int i = 0; i < TILE_SIZE && (y + i) < screen_h; i++) {
                    int offset = (y + i) * screen_w + x;
                    int line_w = (x + TILE_SIZE > screen_w) ? (screen_w - x) : TILE_SIZE;
                    memcpy(&vram[offset], &backbuffer[offset], line_w * 4);
                }
                dirty_grid[r * grid_cols + c] = 0; // Очищаем флаг
            }
        }
    }
}

// --- ОСНОВНОЙ ВХОД ---
int main(int argc, char **argv) {
    // 1. Получаем инфо о видеорежиме (VESA LFB)
    uint64_t phys_fb, w, h, pitch;
    _syscall(32, (uint64_t)&phys_fb, (uint64_t)&w, (uint64_t)&h, (uint64_t)&pitch, 0);
    screen_w = (uint32_t)w; screen_h = (uint32_t)h;

    // 2. Маппим VRAM и создаем бэкбуфер
    vram = (uint32_t *)_syscall(30, phys_fb, screen_w * screen_h * 4, 0, 0, 0);
    backbuffer = (uint32_t *)malloc(screen_w * screen_h * 4);
    draw_target = backbuffer;
    sysgui_init_dirty_grid();

    // 3. Инициализируем ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)screen_w, (float)screen_h);
    
    // Настройка стиля Sonoma
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = WINDOW_ROUNDING_LARGE;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.0f); // Прозрачные для блюра

    // 4. Инициализируем модули GUI
    GUI::InitDesktop();
    GUI::InitDock();
    GUI::InitWindowManager();
    
    api_preload_boot_sound();
    api_try_boot_sound();

    uint32_t last_time = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
    int mx = 0, my = 0;
    bool mdown = false;

    // ГЛАВНЫЙ ЦИКЛ
    while (true) {
        uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
        float dt = (now - last_time) / 1000.0f;
        last_time = now;

        // Ввод: Мышь
        uint64_t r_mx, r_my, r_btn;
        _syscall(7, (uint64_t)&r_mx, (uint64_t)&r_my, (uint64_t)&r_btn, 0, 0);
        mx = (int)r_mx; my = (int)r_my; mdown = (r_btn & 1);

        io.MousePos = ImVec2((float)mx, (float)my);
        io.MouseDown[0] = mdown;
        io.DeltaTime = (dt > 0) ? dt : 0.001f;

        // РЕНДЕРИНГ
        // Шаг 1: Рабочий стол (обои)
        GUI::RenderDesktop();

        // Шаг 2: ImGui кадр
        ImGui::NewFrame();
        
        GUI::UpdateDesktop(dt, mx, my, mdown, 0);
        GUI::RenderTopPanel();
        GUI::RenderDock(mx, my, mdown);
        GUI::RenderWindows(dt);

        ImGui::Render();

        // Шаг 3: Программный растеризатор ImGui поверх всего
        api_render_imgui_data(ImGui::GetDrawData());

        // Шаг 4: Курсор (простой софтварный треугольник)
        for(int i=0; i<10; i++) {
            for(int j=0; j<i; j++) {
                int px = mx + j, py = my + i;
                if(px < screen_w && py < screen_h) backbuffer[py * screen_w + px] = 0xFFFFFF;
            }
        }

        // Шаг 5: Копирование измененных тайлов на экран
        copy_dirty_to_vram();
        
        api_tick_audio();
        
        // Ограничение FPS (~60)
        _syscall(13, 16, 0, 0, 0, 0); 
    }

    return 0;
}
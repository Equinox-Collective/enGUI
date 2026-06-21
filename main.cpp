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
    // ВРЕМЕННО: Копируем весь кадр целиком каждый раз, чтобы убрать фриз экрана!
    memcpy(vram, backbuffer, screen_w * screen_h * 4);
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

// --- ОСНОВНОЙ ВХОД ---
int main(int argc, char **argv) {
    // 1. Получаем инфо о видеорежиме (VESA LFB)
    uint64_t phys_fb = 0;
    uint32_t w = 0, h = 0, pitch = 0; // w и h должны быть uint32_t!

    _syscall(32, (uint64_t)&phys_fb, (uint64_t)&w, (uint64_t)&h, (uint64_t)&pitch, 0);

    screen_w = w; 
    screen_h = h;

    char msg[128];
    sprintf(msg, "GUI: Screen resolution %ux%u. Requesting %u bytes...\n", screen_w, screen_h, screen_w * screen_h * 4);
    _syscall(1, (uint64_t)msg, 0, 0, 0, 0);

    // 2. Инициализируем ImGui СНАЧАЛА (пока куча абсолютно чистая и не повреждена большими запросами)
    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) {
        _syscall(1, (uint64_t)"FATAL: ImGui CreateContext failed!\n", 0, 0, 0, 0);
        sys_exit(1);
    }
    
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Выключаем imgui.ini, чтобы не дергать сырую VFS на запись
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2((float)screen_w, (float)screen_h);
    
    // Настройка стиля Sonoma
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = WINDOW_ROUNDING_LARGE;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.0f); // Прозрачные для блюра

    // 3. Маппим VRAM и создаем бэкбуфер
    vram = (uint32_t *)_syscall(30, phys_fb, screen_w * screen_h * 4, 0, 0, 0);
    if (!vram) {
        _syscall(1, (uint64_t)"FATAL: Could not map VRAM!\n", 0, 0, 0, 0);
        sys_exit(1);
    }
    backbuffer = (uint32_t *)malloc(screen_w * screen_h * 4);
    if (!backbuffer) {
        _syscall(1, (uint64_t)"FATAL: Out of memory for backbuffer\n", 0, 0, 0, 0);
        sys_exit(1);
    }
    draw_target = backbuffer;
    sysgui_init_dirty_grid();

    // 4. Отладочный вывод адресов для проверки наложения секции BSS и кучи
    char dbg_mem[512];
    sprintf(dbg_mem, "DEBUG: GImGui ptr: %p, Fonts ptr: %p, backbuffer ptr: %p\n", 
            (void*)ImGui::GetCurrentContext(), (void*)io.Fonts, (void*)backbuffer);
    _syscall(1, (uint64_t)dbg_mem, 0, 0, 0, 0);

    // 5. Инициализируем модули GUI
    GUI::InitDesktop();
    GUI::InitDock();
    GUI::InitWindowManager();
    
    // api_preload_boot_sound();
    // api_try_boot_sound();

    uint32_t last_time = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
    int mx = 0, my = 0;
    bool mdown = false;

    // ГЛАВНЫЙ ЦИКЛ
    while (true) {
        uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
        float dt = (now - last_time) / 1000.0f;
        last_time = now;

        // Ввод: Мышь
        get_mouse_state(&mx, &my, &mdown);

        io.MousePos = ImVec2((float)mx, (float)my);
        io.MouseDown[0] = mdown;
        io.DeltaTime = (dt > 0) ? dt : 0.001f;

        GUI::RenderDesktop();

        // --- ДОБАВЬ ЭТОТ ДЕБАГ ---
        // char dbg_loop[256];
        // sprintf(dbg_loop, "LOOP: GImGui: %p, Fonts: %p\n", 
        //         (void*)ImGui::GetCurrentContext(), (void*)io.Fonts);
        // _syscall(1, (uint64_t)dbg_loop, 0, 0, 0, 0);
        // -------------------------

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
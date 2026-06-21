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
    for (int r = y / TILE_SIZE; r <= y2 / TILE_SIZE && r < grid_rows; r++) {
        for (int c = x / TILE_SIZE; c <= x2 / TILE_SIZE && c < grid_cols; c++) {
            if (r >= 0 && c >= 0) dirty_grid[r * grid_cols + c] = 1;
        }
    }
}

void copy_dirty_to_vram() {
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

int main(int argc, char **argv) {
    uint64_t phys_fb = 0;
    uint32_t w = 0, h = 0, pitch = 0;

    _syscall(32, (uint64_t)&phys_fb, (uint64_t)&w, (uint64_t)&h, (uint64_t)&pitch, 0);

    screen_w = w; 
    screen_h = h;

    char msg[128];
    sprintf(msg, "GUI: Sonoma Interface active %ux%u.\n", screen_w, screen_h);
    _syscall(1, (uint64_t)msg, 0, 0, 0, 0);

    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) {
        sys_exit(1);
    }
    
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; 
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2((float)screen_w, (float)screen_h);

    // Увеличили размер шрифта до 18px для идеальной читаемости и четкости
    ImFont* font = io.Fonts->AddFontFromFileTTF("res/sysgui/Inter.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
    if (!font) {
        _syscall(1, (uint64_t)"WARNING: Could not load Inter.ttf, fallback\n", 0, 0, 0, 0);
    }

    // --- НАСТРОЙКА СТИЛЯ MACOS SONOMA SYSTEM ---
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = WINDOW_ROUNDING_LARGE;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 10.0f;
    style.ScrollbarSize = 8.0f; // Сделали скроллбар узким и аккуратным
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    // Цвета с отличным контрастом
    style.Colors[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.09f, 0.15f, 0.0f); // Прозрачный под блюр
    style.Colors[ImGuiCol_Text]                 = ImVec4(0.96f, 0.96f, 0.97f, 1.00f); // Ярко-белый/светло-серый
    style.Colors[ImGuiCol_TextDisabled]         = ImVec4(0.55f, 0.55f, 0.57f, 1.00f);
    style.Colors[ImGuiCol_Header]               = ImVec4(0.00f, 0.48f, 1.00f, 0.40f); // Акцентный синий
    style.Colors[ImGuiCol_HeaderActive]         = ImVec4(0.00f, 0.48f, 1.00f, 0.80f);
    style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.00f, 0.48f, 1.00f, 0.60f);
    style.Colors[ImGuiCol_Button]               = ImVec4(0.18f, 0.20f, 0.28f, 0.80f); // Контрастные кнопки
    style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.00f, 0.48f, 1.00f, 0.85f);
    style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.00f, 0.38f, 0.85f, 1.00f);
    style.Colors[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.14f, 0.22f, 0.80f);
    style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f, 0.21f, 0.32f, 0.80f);
    style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.27f, 0.40f, 0.80f);
    style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.10f, 0.20f);
    style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.35f, 0.35f, 0.45f, 0.50f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.45f, 0.55f, 0.70f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.00f, 0.48f, 1.00f, 0.80f);

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

    while (true) {
        uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
        float dt = (now - last_time) / 1000.0f;
        last_time = now;

        // Ввод: Мышь
        get_mouse_state(&mx, &my, &mdown);

        io.MousePos = ImVec2((float)mx, (float)my);
        io.MouseDown[0] = mdown;
        io.DeltaTime = (dt > 0) ? dt : 0.001f;

        // 1. Инициализируем новый кадр Dear ImGui в самом начале цикла.
        // Это критически важно, так как фоновые виджеты рабочего стола используют контекст ImGui.
        ImGui::NewFrame();

        // 2. Отрисовка подложки рабочего стола и виджетов (теперь вызовы ImGui безопасны)
        GUI::RenderDesktop();
        
        // 3. Обновление состояния и рендеринг системных элементов интерфейса
        GUI::UpdateDesktop(dt, mx, my, mdown, 0);
        GUI::RenderTopPanel();
        GUI::RenderDock(mx, my, mdown);
        GUI::RenderWindows(dt);

        // 4. Финализация кадра ImGui
        ImGui::Render();

        // 5. Программный растеризатор ImGui поверх всего кадра
        api_render_imgui_data(ImGui::GetDrawData());

        // 6. Отрисовка софтварного сглаженного курсора
        for(int i = 0; i < 16; i++) {
            for(int j = 0; j < i; j++) {
                if (j < i - 1) {
                    int px = mx + j, py = my + i;
                    if(px < (int)screen_w && py < (int)screen_h) {
                        backbuffer[py * screen_w + px] = 0xFFFFFF; // Белое тело
                    }
                } else {
                    int px = mx + j, py = my + i;
                    if(px < (int)screen_w && py < (int)screen_h) {
                        backbuffer[py * screen_w + px] = 0x000000; // Черная кайма
                    }
                }
            }
        }

        // 7. Копирование измененных тайлов на экран
        copy_dirty_to_vram();
        
        api_tick_audio();
        
        // Ограничение FPS (~60)
        _syscall(13, 16, 0, 0, 0, 0); 
    }

    return 0;
}
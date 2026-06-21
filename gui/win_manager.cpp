#include "win_manager.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

// Заглушки для будущих файлов приложений
#include "apps/terminal.h"
#include "apps/monitor.h"
#include "apps/paint.h"

extern "C" {
#include <stdlib.h>
#include <string.h>
}

namespace GUI {

    // Максимальное количество одновременно открытых окон
    static const int MAX_WINDOWS = 16;
    static App* g_ActiveWindows[MAX_WINDOWS];
    static int g_WindowCount = 0;

    void InitWindowManager() {
        for (int i = 0; i < MAX_WINDOWS; i++) g_ActiveWindows[i] = nullptr;
    }

    // Поиск индекса окна в массиве
    static int find_window_idx(const char* title) {
        for (int i = 0; i < g_WindowCount; i++) {
            if (strcmp(g_ActiveWindows[i]->title, title) == 0) return i;
        }
        return -1;
    }

    void OpenAppWindow(const char* title) {
        int idx = find_window_idx(title);
        
        // Если окно уже открыто — просто фокусим его
        if (idx != -1) {
            App* win = g_ActiveWindows[idx];
            // Перемещаем в конец (Z-order: top)
            for (int i = idx; i < g_WindowCount - 1; i++) g_ActiveWindows[i] = g_ActiveWindows[i+1];
            g_ActiveWindows[g_WindowCount - 1] = win;
            win->is_open = true;
            return;
        }

        // Если окно новое — создаем экземпляр (App Factory)
        if (g_WindowCount >= MAX_WINDOWS) return;

        App* new_app = nullptr;
        if (strcmp(title, "Terminal") == 0) new_app = new TerminalApp();
        else if (strcmp(title, "Monitor") == 0) new_app = new MonitorApp();
        else if (strcmp(title, "Paint") == 0)   new_app = new PaintApp();
        // ... другие приложения ...

        if (new_app) {
            new_app->is_open = true;
            new_app->OnOpen();
            g_ActiveWindows[g_WindowCount++] = new_app;
        }
    }

    bool IsAppActive(const char* title) {
        int idx = find_window_idx(title);
        return (idx != -1 && g_ActiveWindows[idx]->is_open);
    }

    void RenderWindows(float dt) {
        for (int i = 0; i < g_WindowCount; i++) {
            App* win = g_ActiveWindows[i];
            if (!win || !win->is_open) continue;

            // Настройка стиля Sonoma для конкретного окна
            ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
            
            // Флаги: убираем стандартный фон ImGui, чтобы работал наш Acrylic Blur
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;

            if (ImGui::Begin(win->title, &win->is_open, flags)) {
                // 1. Получаем реальные координаты окна после перемещения пользователем
                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();

                // 2. Рендерим Акриловое стекло под окном
                draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.4f, 12, 0x1A1C29);

                // 3. Рисуем кастомный TitleBar (кнопки управления)
                ImDrawList* draw = ImGui::GetWindowDrawList();
                float btn_r = 6.0f;
                ImVec2 btn_p = ImVec2(pos.x + 15, pos.y + 16);
                
                draw->AddCircleFilled(btn_p, btn_r, 0xFF5C5CFF); // Red (Close)
                draw->AddCircleFilled(ImVec2(btn_p.x + 20, btn_p.y), btn_r, 0xFF5CE6FF); // Yellow
                draw->AddCircleFilled(ImVec2(btn_p.x + 40, btn_p.y), btn_r, 0xFF5CFF5C); // Green

                // 4. Логика фокуса (если кликнули — на передний план)
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
                    if (i != g_WindowCount - 1) {
                        // Перемещаем в топ Z-order
                        for (int j = i; j < g_WindowCount - 1; j++) g_ActiveWindows[j] = g_ActiveWindows[j+1];
                        g_ActiveWindows[g_WindowCount - 1] = win;
                    }
                }

                // 5. Отрисовка контента приложения
                ImGui::SetCursorPosY(30); // Отступ под TitleBar
                ImGui::BeginChild("Content");
                win->OnRender(dt);
                ImGui::EndChild();
            }
            ImGui::End();

            // Если окно закрыли (крестик ImGui)
            if (!win->is_open) {
                win->OnClose();
                // Тут можно либо удалять объект, либо просто скрыть. Пока скроем.
            }
        }
    }
}
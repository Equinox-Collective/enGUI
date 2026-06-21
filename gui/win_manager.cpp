#include "win_manager.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

#include "apps/terminal.h"
#include "apps/monitor.h"
#include "apps/paint.h"

extern "C" {
#include <stdlib.h>
#include <string.h>
}

namespace GUI {

    static const int MAX_WINDOWS = 16;
    static App* g_ActiveWindows[MAX_WINDOWS];
    static int g_WindowCount = 0;
    static uint32_t g_NextInstanceID = 100; // Генератор уникальных ID для исключения конфликтов

    void InitWindowManager() {
        for (int i = 0; i < MAX_WINDOWS; i++) g_ActiveWindows[i] = nullptr;
    }

    static int find_window_idx_by_id(uint32_t id) {
        for (int i = 0; i < g_WindowCount; i++) {
            if (g_ActiveWindows[i] && g_ActiveWindows[i]->instance_id == id) return i;
        }
        return -1;
    }

    void OpenAppWindow(const char* title) {
        if (g_WindowCount >= MAX_WINDOWS) return;

        App* new_app = nullptr;
        uint32_t uid = g_NextInstanceID++;

        // Создаем СОВЕРШЕННО НОВЫЙ независимый экземпляр программы
        if (strcmp(title, "Terminal") == 0)     new_app = new TerminalApp(uid);
        else if (strcmp(title, "Monitor") == 0) new_app = new MonitorApp(uid);
        else if (strcmp(title, "Paint") == 0)   new_app = new PaintApp(uid);

        if (new_app) {
            new_app->is_open = true;
            new_app->OnOpen();
            g_ActiveWindows[g_WindowCount++] = new_app;
        }
    }

    bool IsAppActive(const char* title) {
        for (int i = 0; i < g_WindowCount; i++) {
            if (g_ActiveWindows[i] && strcmp(g_ActiveWindows[i]->title, title) == 0 && g_ActiveWindows[i]->is_open) {
                return true;
            }
        }
        return false;
    }

    void RenderWindows(float dt) {
        for (int i = 0; i < g_WindowCount; i++) {
            App* win = g_ActiveWindows[i];
            if (!win || !win->is_open) continue;

            // ГАРАНТИЯ ИСКЛЮЧЕНИЯ КОНФЛИКТОВ С КУЧЕЙ КОНТРОЛЛЕРОВ И ОКОН IMGUI
            ImGui::PushID(win->instance_id);

            // Генерируем уникальное имя для ImGui окна
            char uniq_title[128];
            sprintf(uniq_title, "%s##Instance_%u", win->title, win->instance_id);

            ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;

            if (ImGui::Begin(uniq_title, &win->is_open, flags)) {
                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();

                // 1. Сверхгладкое Акриловое размытие подложки окна
                draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.42f, WINDOW_ROUNDING_LARGE, 0x0A0C16);

                ImDrawList* draw = ImGui::GetWindowDrawList();

                // 2. Красивая стеклянная рамка (бордер) вокруг окна
                draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), 0x2EFFFFFF, WINDOW_ROUNDING_LARGE, 0, 1.5f);

                // 3. Кастомные яркие кнопки управления в стиле macOS (Close, Minimize, Expand)
                float btn_r = 6.0f;
                ImVec2 btn_p = ImVec2(pos.x + 18, pos.y + 18);
                
                draw->AddCircleFilled(btn_p, btn_r, 0xFFFF5F56); // Red
                draw->AddCircleFilled(ImVec2(btn_p.x + 18, btn_p.y), btn_r, 0xFFFFBD2E); // Yellow
                draw->AddCircleFilled(ImVec2(btn_p.x + 36, btn_p.y), btn_r, 0xFF27C93F); // Green

                // Фокусировка при клике
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
                    if (i != g_WindowCount - 1) {
                        for (int j = i; j < g_WindowCount - 1; j++) g_ActiveWindows[j] = g_ActiveWindows[j+1];
                        g_ActiveWindows[g_WindowCount - 1] = win;
                    }
                }

                // 4. Отрисовка контента
                ImGui::SetCursorPosY(35); 
                ImGui::BeginChild("ContentArea", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                win->OnRender(dt);
                ImGui::EndChild();
            }
            ImGui::End();

            ImGui::PopID();

            // Удаление закрытых окон
            if (!win->is_open) {
                win->OnClose();
                delete win;
                for (int j = i; j < g_WindowCount - 1; j++) {
                    g_ActiveWindows[j] = g_ActiveWindows[j+1];
                }
                g_ActiveWindows[--g_WindowCount] = nullptr;
                i--;
            }
        }
    }
}
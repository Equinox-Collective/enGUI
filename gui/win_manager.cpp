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
#include <equos.h>
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

    App* GetActiveApp() {
        if (g_WindowCount > 0) return g_ActiveWindows[g_WindowCount - 1];
        return nullptr;
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

            if (ImGui::Begin(uniq_title, nullptr, flags)) {
                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();

                // Soft drop shadow for Window (radius=4, shadow_radius=24, alpha=0.55f, offsets=(0, 10))
                draw_soft_shadow((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, WINDOW_ROUNDING_LARGE, 24, 0.55f, 0, 10);

                // 1. Сверхгладкое Акриловое размытие подложки окна (космическая темная бездна)
                draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.50f, WINDOW_ROUNDING_LARGE, 0x030206);

                ImDrawList* draw = ImGui::GetWindowDrawList();

                // Проверяем активность окна
                bool is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

                // 2. Неоновая рамка с эффектом свечения активного окна (фиолетовый / приглушенный фиолетовый)
                uint32_t border_col = is_focused ? 0xFFBD00FF : 0x44BD00FF;
                draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), border_col, WINDOW_ROUNDING_LARGE, 0, 1.5f);

                // Тонкий неоновый разделитель заголовка (циан, на высоте 30px)
                uint32_t sep_col = is_focused ? 0x8800E5FF : 0x3300E5FF;
                draw->AddLine(ImVec2(pos.x, pos.y + 30), ImVec2(pos.x + size.x, pos.y + 30), sep_col, 1.0f);

                // 3. Кастомные неоновые кнопки управления окном в стиле EquinoxOS (вверху справа)
                float btn_w = 12.0f;
                float btn_h = 12.0f;
                float right_pad = 20.0f;
                
                ImGuiIO& io = ImGui::GetIO();
                float mx = io.MousePos.x;
                float my = io.MousePos.y;
                bool mdown = io.MouseDown[0];

                // Кнопка закрытия [X] (неоновый красный/оранжевый крестик)
                ImVec2 close_p = ImVec2(pos.x + size.x - right_pad - btn_w, pos.y + 10);
                draw->AddRectFilled(close_p, ImVec2(close_p.x + btn_w, close_p.y + btn_h), 0x33FF0055, 2.0f);
                draw->AddRect(close_p, ImVec2(close_p.x + btn_w, close_p.y + btn_h), 0xFFFF0055, 2.0f, 0, 1.0f);
                draw->AddLine(ImVec2(close_p.x + 3, close_p.y + 3), ImVec2(close_p.x + btn_w - 3, close_p.y + btn_h - 3), 0xFFFF0055, 1.5f);
                draw->AddLine(ImVec2(close_p.x + btn_w - 3, close_p.y + 3), ImVec2(close_p.x + 3, close_p.y + btn_h - 3), 0xFFFF0055, 1.5f);

                // Кнопка разворачивания [+] (неоновый фиолетовый)
                ImVec2 max_p = ImVec2(close_p.x - 18, pos.y + 10);
                draw->AddRectFilled(max_p, ImVec2(max_p.x + btn_w, max_p.y + btn_h), 0x33BD00FF, 2.0f);
                draw->AddRect(max_p, ImVec2(max_p.x + btn_w, max_p.y + btn_h), 0xFFBD00FF, 2.0f, 0, 1.0f);
                draw->AddRect(ImVec2(max_p.x + 3, max_p.y + 3), ImVec2(max_p.x + btn_w - 3, max_p.y + btn_h - 3), 0xFFBD00FF, 1.0f);

                // Кнопка сворачивания [-] (неоновый голубой)
                ImVec2 min_p = ImVec2(max_p.x - 18, pos.y + 10);
                draw->AddRectFilled(min_p, ImVec2(min_p.x + btn_w, min_p.y + btn_h), 0x3300E5FF, 2.0f);
                draw->AddRect(min_p, ImVec2(min_p.x + btn_w, min_p.y + btn_h), 0xFF00E5FF, 2.0f, 0, 1.0f);
                draw->AddLine(ImVec2(min_p.x + 3, min_p.y + 6), ImVec2(min_p.x + btn_w - 3, min_p.y + 6), 0xFF00E5FF, 1.5f);

                // Логика закрытия при клике по кнопке закрытия [X]
                if (mdown && mx >= close_p.x && mx <= close_p.x + btn_w && my >= close_p.y && my <= close_p.y + btn_h) {
                    static uint32_t last_close_tick = 0;
                    uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
                    if (now - last_close_tick > 300) {
                        win->is_open = false;
                        last_close_tick = now;
                        play_wav_file("res/sysgui/click.wav");
                    }
                }

                // Фокусировка при клике
                if (is_focused) {
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
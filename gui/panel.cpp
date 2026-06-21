#include "panel.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include "desktop.h"

extern "C" {
#include <equos.h>
#include <stdio.h>
}

extern uint32_t screen_w;

namespace GUI {

    void RenderTopPanel() {
        // 1. Акриловая полоска сверху (24px высота)
        draw_acrylic_blur(0, 0, screen_w, 24, 0.4f, 0, 0x1A1C29);

        // Используем ImGui для элементов меню
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)screen_w, 24));
    
        // ИСПРАВЛЕНО: ImGuiWindowFlags_NoScrollbar вместо ImGuiWindowFlags_ScrollbarNone
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                                 ImGuiWindowFlags_NoBackground | 
                                 ImGuiWindowFlags_NoMove | 
                                 ImGuiWindowFlags_NoScrollbar;

        ImGui::Begin("##TopPanel", nullptr, flags);
            {
            ImDrawList* draw = ImGui::GetWindowDrawList();

            // Кнопка "EQ Menu"
            ImGui::SetCursorPos(ImVec2(10, 0));
            if (ImGui::BeginMenu(" EQ ")) {
                if (ImGui::MenuItem("About EquinoxOS...")) { /* Показать окно */ }
                ImGui::Separator();
                if (ImGui::MenuItem("System Settings...")) { /* Открыть настройки */ }
                if (ImGui::MenuItem("Next Theme")) { NextTheme(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Sleep", "Alt+S")) { /* Логика сна */ }
                if (ImGui::MenuItem("Restart...")) { _syscall(10, 0, 0, 0, 0, 0); } // syscall exit
                ImGui::EndMenu();
            }

            // Активное приложение (динамический текст)
            ImGui::SameLine(60);
            ImGui::TextColored(ImVec4(1,1,1,0.8f), "Finder");

            // --- ПРАВАЯ ЧАСТЬ (Индикаторы) ---
            
            // Часы
            uint64_t ms = _syscall(6, 0, 0, 0, 0, 0);
            int secs = (ms / 1000) % 60;
            int mins = (ms / 60000) % 60;
            int hours = (ms / 3600000) % 24;
            char time_str[16];
            sprintf(time_str, "%02d:%02d:%02d", hours, mins, secs);

            float time_width = ImGui::CalcTextSize(time_str).x;
            ImGui::SameLine(screen_w - time_width - 15);
            ImGui::Text("%s", time_str);

            // Индикатор батареи/питания (фейковый для красоты)
            ImGui::SameLine(screen_w - time_width - 60);
            draw->AddRectFilled(ImVec2(ImGui::GetCursorScreenPos().x, 6), 
                               ImVec2(ImGui::GetCursorScreenPos().x + 20, 18), 0xFF48BB78, 2.0f);
            draw->AddRectFilled(ImVec2(ImGui::GetCursorScreenPos().x + 20, 9), 
                               ImVec2(ImGui::GetCursorScreenPos().x + 22, 15), 0xFF48BB78, 1.0f);
        }
        ImGui::End();
    }
}
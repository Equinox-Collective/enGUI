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
        // Увеличен размер верхней строки до 28px для идеальной разметки
        draw_acrylic_blur(0, 0, screen_w, 28, 0.45f, 0, 0x0A0C16);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)screen_w, 28));
    
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                                 ImGuiWindowFlags_NoBackground | 
                                 ImGuiWindowFlags_NoMove | 
                                 ImGuiWindowFlags_NoScrollbar;

        ImGui::Begin("##TopPanel", nullptr, flags);
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();

            // EQ Меню ("Яблоко" системы)
            ImGui::SetCursorPos(ImVec2(15, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            if (ImGui::BeginMenu("  EQ  ")) {
                if (ImGui::MenuItem("About EquinoxOS...")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Sonoma Settings...")) {}
                if (ImGui::MenuItem("Next Desktop Theme")) { NextTheme(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Sleep System", "Alt+S")) {}
                if (ImGui::MenuItem("Restart Kernel")) { _syscall(10, 0, 0, 0, 0, 0); }
                ImGui::EndMenu();
            }
            ImGui::PopStyleVar();

            // Активное приложение в фокусе (крупно и сочно)
            ImGui::SameLine(75);
            ImGui::SetCursorPosY(4);
            ImGui::TextColored(ImVec4(1, 1, 1, 0.95f), "Finder");

            // Информационное меню системы
            ImGui::SameLine(145);
            if (ImGui::BeginMenu("File")) { ImGui::EndMenu(); }
            ImGui::SameLine(190);
            if (ImGui::BeginMenu("Edit")) { ImGui::EndMenu(); }
            ImGui::SameLine(235);
            if (ImGui::BeginMenu("View")) { ImGui::EndMenu(); }

            // --- ПРАВАЯ ЧАСТЬ (Индикаторы состояния с высоким контрастом) ---
            
            // Время и дата
            uint64_t ms = _syscall(6, 0, 0, 0, 0, 0);
            int secs = (ms / 1000) % 60;
            int mins = (ms / 60000) % 60;
            int hours = (ms / 3600000) % 24;
            char time_str[32];
            sprintf(time_str, "%02d:%02d:%02d", hours, mins, secs);

            float time_width = ImGui::CalcTextSize(time_str).x;
            ImGui::SameLine(screen_w - time_width - 20);
            ImGui::SetCursorPosY(4);
            ImGui::TextColored(ImVec4(0.96f, 0.96f, 0.97f, 1.00f), "%s", time_str);

            // Контрастный индикатор заряда батарейки
            float battery_x = screen_w - time_width - 70;
            ImGui::SameLine(battery_x);
            
            ImVec2 cur_pos = ImGui::GetCursorScreenPos();
            draw->AddRectFilled(ImVec2(cur_pos.x, 8), ImVec2(cur_pos.x + 22, 20), 0xFF28C76F, 3.0f);
            draw->AddRectFilled(ImVec2(cur_pos.x + 22, 11), ImVec2(cur_pos.x + 24, 17), 0xFF28C76F, 1.0f);
            
            // Тонкая стильная линия разделения внизу панели
            draw->AddLine(ImVec2(0, 27), ImVec2((float)screen_w, 27), 0x22FFFFFF, 1.0f);
        }
        ImGui::End();
    }
}
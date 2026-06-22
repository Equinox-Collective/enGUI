#include "panel.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include "desktop.h"
#include "win_manager.h"

extern "C" {
#include <equos.h>
#include <stdio.h>
}

extern uint32_t screen_w;

namespace GUI {

    void RenderTopPanel() {
        // Soft drop shadow for Top Panel (radius=0, shadow_radius=8, alpha=0.3f, offsets=(0, 2))
        draw_soft_shadow(0, 0, screen_w, 28, 0, 8, 0.3f, 0, 2);

        // Увеличен размер верхней строки до 28px для идеальной разметки (в космо-темном стиле)
        draw_acrylic_blur(0, 0, screen_w, 28, 0.50f, 0, 0x030206);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)screen_w, 28));
    
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                                 ImGuiWindowFlags_NoBackground | 
                                 ImGuiWindowFlags_NoMove | 
                                 ImGuiWindowFlags_NoScrollbar;

        ImGui::Begin("##TopPanel", nullptr, flags);
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();

            // EQ Меню (Главный неоновый логотип системы)
            ImGui::SetCursorPos(ImVec2(15, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            if (ImGui::BeginMenu(" ʘ EQUINOX ")) {
                if (ImGui::MenuItem("About EquinoxOS...")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Equinox Settings...")) {}
                if (ImGui::MenuItem("Next Desktop Theme")) { NextTheme(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Sleep System", "Alt+S")) {}
                if (ImGui::MenuItem("Restart Kernel")) { _syscall(10, 0, 0, 0, 0, 0); }
                ImGui::EndMenu();
            }
            ImGui::PopStyleVar();

            // Активное приложение в фокусе (динамическое и неоновое)
            const char* active_title = "Equinox Desktop";
            App* active_app = GetActiveApp();
            if (active_app && active_app->is_open) {
                active_title = active_app->title;
            }

            ImGui::SameLine(135);
            ImGui::SetCursorPosY(4);
            ImGui::TextColored(ImVec4(0.00f, 0.90f, 1.00f, 1.00f), "%s", active_title);

            // Информационное меню системы (сдвинуто вправо под динамический заголовок)
            ImGui::SameLine(280);
            if (ImGui::BeginMenu("File")) { ImGui::EndMenu(); }
            ImGui::SameLine(325);
            if (ImGui::BeginMenu("Edit")) { ImGui::EndMenu(); }
            ImGui::SameLine(370);
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
            ImGui::TextColored(ImVec4(0.92f, 0.96f, 1.00f, 1.00f), "%s", time_str);

            // Контрастный индикатор заряда батарейки (неоновый циан)
            float battery_x = screen_w - time_width - 70;
            ImGui::SameLine(battery_x);
            
            ImVec2 cur_pos = ImGui::GetCursorScreenPos();
            draw->AddRectFilled(ImVec2(cur_pos.x, 8), ImVec2(cur_pos.x + 22, 20), 0xFF00E5FF, 2.0f);
            draw->AddRectFilled(ImVec2(cur_pos.x + 22, 11), ImVec2(cur_pos.x + 24, 17), 0xFF00E5FF, 1.0f);
            
            // Тонкая стильная линия разделения внизу панели (неоновый циан)
            draw->AddLine(ImVec2(0, 27), ImVec2((float)screen_w, 27), 0x3300E5FF, 1.0f);
        }
        ImGui::End();
    }
}
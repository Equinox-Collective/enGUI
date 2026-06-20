#include "panel.h"
#include "desktop.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include <equos.h>
#include <stdio.h>

extern uint32_t screen_w, screen_h;
extern const char* g_ActiveWindowTitle;

namespace GUI {
    void RenderTopPanel(bool& start_menu_open) {
        // Рисуем акриловую плашку статус-бара
        draw_acrylic_blur(0, 0, screen_w, 24, 0.35f, 0, 0x1E222B);

        // ImGui невидимое оверлейное окно для кнопок статус-бара
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)screen_w, 24));
        ImGui::Begin("##TopMenuBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
        {
            ImGui::SetCursorPos(ImVec2(10, 3));
            if (ImGui::Button("EQ", ImVec2(35, 18))) {
                start_menu_open = !start_menu_open;
            }

            ImGui::SameLine();
            ImGui::SetCursorPosY(4);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), "%s", g_ActiveWindowTitle ? g_ActiveWindowTitle : "Desktop");

            // Отрисовка памяти
            uint64_t used_ram = sys_get_used_mem();
            uint64_t total_ram = sys_get_total_mem();
            char ram_str[64];
            sprintf(ram_str, "Memory: %llu/%llu MB", used_ram / (1024 * 1024), total_ram / (1024 * 1024));
            
            float ram_text_width = ImGui::CalcTextSize(ram_str).x;
            ImGui::SameLine((float)screen_w - ram_text_width - 100);
            ImGui::SetCursorPosY(4);
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.5f, 1.0f), "%s", ram_str);

            // Отрисовка часов CMOS
            uint64_t ut = _syscall(6, 0, 0, 0, 0, 0) / 1000;
            int hours = (ut / 3600) % 24;
            int minutes = (ut / 60) % 60;
            int seconds = ut % 60;
            char clock_str[32];
            sprintf(clock_str, "%02d:%02d:%02d", hours, minutes, seconds);

            ImGui::SameLine((float)screen_w - 70);
            ImGui::SetCursorPosY(4);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", clock_str);
        }
        ImGui::End();
    }
}
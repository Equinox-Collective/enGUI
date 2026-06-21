#include "terminal.h"
#include "../../api_gui.h"
#include "../../imgui/imgui.h"

extern "C" {
#include <equos.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
}

namespace GUI {

    TerminalApp::TerminalApp() : App("Terminal") {
        memset(input_buffer, 0, sizeof(input_buffer));
        log_size = 0;
        scroll_to_bottom = false;
        
        // Приветственное сообщение
        AddLog("EquinoxOS Sonoma Terminal v1.2");
        AddLog("Type 'help' to see available commands.");
        AddLog("");
    }

    void TerminalApp::AddLog(const char* fmt, ...) {
        if (log_size >= MAX_LOG_LINES) {
            // Сдвигаем лог вверх при переполнении
            free(log_lines[0]);
            for (int i = 1; i < MAX_LOG_LINES; i++) {
                log_lines[i-1] = log_lines[i];
            }
            log_size--;
        }

        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsprintf(buf, fmt, args);
        va_end(args);

        log_lines[log_size++] = strdup(buf);
        scroll_to_bottom = true;
    }

    void TerminalApp::ExecuteCommand(const char* cmd) {
        AddLog("> %s", cmd);

        if (strcmp(cmd, "help") == 0) {
            AddLog("Commands: help, clear, neofetch, ls, killall, whoami");
        } 
        else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < log_size; i++) free(log_lines[i]);
            log_size = 0;
        } 
        else if (strcmp(cmd, "neofetch") == 0) {
            AddLog("  .-.    OS: EquinoxOS x86_64");
            AddLog("  oo|    Kernel: Sonoma-Ring3-v1");
            AddLog(" /` _\\   Shell: Eqsh v1.0");
            AddLog(" \\_\\     Memory: %llu MB used", sys_get_used_mem() / (1024*1024));
        } 
        else if (strcmp(cmd, "whoami") == 0) {
            AddLog("root (Ring 3 Privileged)");
        }
        else if (strncmp(cmd, "exec ", 5) == 0) {
            const char* path = cmd + 5;
            AddLog("Launching: %s...", path);
            sys_exec(path);
        }
        else {
            AddLog("Command not found: %s", cmd);
        }
    }

    void TerminalApp::OnRender(float dt) {
        // Устанавливаем стиль текста (зеленый хакерский или белый Sonoma)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        
        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);
        
        for (int i = 0; i < log_size; i++) {
            ImGui::TextUnformatted(log_lines[i]);
        }

        if (scroll_to_bottom) {
            ImGui::SetScrollHereY(1.0f);
            scroll_to_bottom = false;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Поле ввода
        bool reclaim_focus = false;
        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
        
        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##Input", input_buffer, sizeof(input_buffer), input_flags)) {
            if (input_buffer[0] != '\0') {
                ExecuteCommand(input_buffer);
                input_buffer[0] = '\0';
            }
            reclaim_focus = true;
        }
        ImGui::PopItemWidth();

        // Автофокус на поле ввода
        ImGui::SetItemDefaultFocus();
        if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);
    }

} // namespace GUI
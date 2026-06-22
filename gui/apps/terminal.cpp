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

    TerminalApp::TerminalApp(uint32_t id) : App("Terminal", id) {
        memset(input_buffer, 0, sizeof(input_buffer));
        log_size = 0;
        scroll_to_bottom = false;
        
        AddLog("EquinoxOS Terminal Shell v1.5");
        AddLog("Type 'help' for system instructions.");
        AddLog("");
    }

    void TerminalApp::AddLog(const char* fmt, ...) {
        if (log_size >= MAX_LOG_LINES) {
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
        AddLog("root@equinox ~ $ %s", cmd);

        if (strcmp(cmd, "help") == 0) {
            AddLog("System utilities: help, clear, neofetch, whoami, exec <binary>");
        } 
        else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < log_size; i++) free(log_lines[i]);
            log_size = 0;
        } 
        else if (strcmp(cmd, "neofetch") == 0) {
            AddLog("  .-.    OS: EquinoxOS x86_64");
            AddLog("  oo|    Kernel: Equinox-Core-v1.5");
            AddLog(" /` _\\   Shell: Eqsh v1.5");
            AddLog(" \\_\\     Memory: %llu MB used", sys_get_used_mem() / (1024*1024));
        } 
        else if (strcmp(cmd, "whoami") == 0) {
            AddLog("root (System Administrator)");
        }
        else if (strncmp(cmd, "exec ", 5) == 0) {
            const char* path = cmd + 5;
            AddLog("Spawning sub-process: %s...", path);
            sys_exec(path);
        }
        else {
            AddLog("Error: terminal command not found: %s", cmd);
        }
    }

    void TerminalApp::OnRender(float dt) {
        // Красивый высококонтрастный изумрудный цвет текста (классический Terminal Pro)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.98f, 0.45f, 1.0f));
        
        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("LogRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);
        
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

        // СТИЛИЗОВАННОЕ БЕСШОВНОЕ ПОЛЕ ВВОДА (Никаких чужеродных синих полос)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0)); // Полная прозрачность
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        
        // Префикс строки
        ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "root@equinox ~ $");
        ImGui::SameLine();

        bool reclaim_focus = false;
        char uniq_input_label[64];
        sprintf(uniq_input_label, "##Input_%u", instance_id);

        ImGui::PushItemWidth(-1);
        if (ImGui::InputText(uniq_input_label, input_buffer, sizeof(input_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (input_buffer[0] != '\0') {
                ExecuteCommand(input_buffer);
                input_buffer[0] = '\0';
            }
            reclaim_focus = true;
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SetItemDefaultFocus();
        if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);
    }

}
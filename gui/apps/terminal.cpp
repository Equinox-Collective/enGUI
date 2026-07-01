// app/sysgui/gui/apps/terminal.cpp
#include "terminal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <equos.h>

namespace GUI {

    TerminalApp::TerminalApp(uint32_t id, int start_x, int start_y) 
        : App("Terminal", id, start_x, start_y, 450, 320) {
        memset(input_buffer, 0, sizeof(input_buffer));
        input_len = 0;
        log_size = 0;

        AddLog("EquinoxOS Terminal Shell v1.5 [NATIVE]");
        AddLog("Type 'help' for instructions.");
        AddLog("");
    }

    TerminalApp::~TerminalApp() {
        for (int i = 0; i < log_size; i++) if (log_lines[i]) free(log_lines[i]);
    }

    void TerminalApp::AddLog(const char* fmt, ...) {
        if (log_size >= MAX_LOG_LINES) {
            free(log_lines[0]);
            for (int i = 1; i < MAX_LOG_LINES; i++) log_lines[i-1] = log_lines[i];
            log_size--;
        }

        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsprintf(buf, fmt, args);
        va_end(args);

        log_lines[log_size++] = strdup(buf);
    }

    void TerminalApp::ExecuteCommand(const char* cmd) {
        AddLog("root@equinox ~ $ %s", cmd);

        if (strcmp(cmd, "help") == 0) {
            AddLog("Commands: help, clear, neofetch, whoami, exec <bin>");
        } 
        else if (strcmp(cmd, "clear") == 0) {
            for (int i = 0; i < log_size; i++) free(log_lines[i]);
            log_size = 0;
        } 
        else if (strcmp(cmd, "neofetch") == 0) {
            AddLog("OS: EquinoxOS x86_64");
            AddLog("Kernel: Equinox-Core-v1.5");
            AddLog("Graphics: ENGUI Native Glass");
        } 
        else if (strcmp(cmd, "whoami") == 0) {
            AddLog("root (Administrator)");
        }
        else if (strncmp(cmd, "exec ", 5) == 0) {
            const char* path = cmd + 5;
            AddLog("Executing %s...", path);
            sys_exec(path);
        }
        else {
            AddLog("Error: command not found: %s", cmd);
        }
    }

    void TerminalApp::OnRender(Painter& p, float dt) {
        (void)dt;
        // Отрисовка логов терминала (классический изумрудный цвет текста)
        int start_draw_y = y + 40;
        for (int i = 0; i < log_size; i++) {
            p.Text(log_lines[i], x + 15, start_draw_y, 0xFF35F645);
            start_draw_y += 16;
        }

        // Отрисовка строки ввода
        char prompt[128];
        sprintf(prompt, "root@equinox ~ $ %s_", input_buffer);
        p.Text(prompt, x + 15, y + h - 30, COLOR_ACCENT_CYAN);
    }

    void TerminalApp::OnKeyEvent(uint16_t key) {
        if (key == '\n') { // ENTER
            if (input_len > 0) {
                input_buffer[input_len] = '\0';
                ExecuteCommand(input_buffer);
                input_buffer[0] = '\0';
                input_len = 0;
            }
        } 
        else if (key == '\b') { // BACKSPACE
            if (input_len > 0) {
                input_buffer[--input_len] = '\0';
            }
        } 
        else if (key >= 32 && key < 127) { // Обычные символы
            if (input_len < 60) {
                input_buffer[input_len++] = (char)key;
                input_buffer[input_len] = '\0';
            }
        }
    }
}
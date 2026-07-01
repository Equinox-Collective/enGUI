// app/sysgui/gui/apps/terminal.h
#ifndef GUI_TERMINAL_H
#define GUI_TERMINAL_H

#include "../../api_gui.h"

namespace GUI {
    class TerminalApp : public App {
    private:
        static const int MAX_LOG_LINES = 16;
        char* log_lines[MAX_LOG_LINES];
        int log_size;
        char input_buffer[64];
        int input_len;

        void AddLog(const char* fmt, ...);
        void ExecuteCommand(const char* cmd);

    public:
        TerminalApp(uint32_t id, int start_x, int start_y);
        virtual ~TerminalApp();

        void OnRender(Painter& p, float dt) override;
        void OnKeyEvent(uint16_t key) override;
    };
}

#endif
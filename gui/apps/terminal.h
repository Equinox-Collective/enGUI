#ifndef GUI_TERMINAL_H
#define GUI_TERMINAL_H

#include "../api_gui.h"

namespace GUI {
    class TerminalApp : public App {
    private:
        static const int MAX_LOG_LINES = 128;
        char* log_lines[MAX_LOG_LINES];
        int log_size;
        char input_buffer[128];
        bool scroll_to_bottom;

        void AddLog(const char* fmt, ...);
        void ExecuteCommand(const char* cmd);

    public:
        TerminalApp();
        virtual ~TerminalApp() {
            for (int i = 0; i < log_size; i++) if (log_lines[i]) free(log_lines[i]);
        }

        void OnRender(float dt) override;
    };
}

#endif
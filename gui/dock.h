#ifndef GUI_DOCK_H
#define GUI_DOCK_H

#include <stdint.h>

namespace GUI {
    struct DockItem {
        const char* label;
        const char* win_title;
        uint32_t color;
        const char* text_glyph;
        const char* exec_cmd;
    };

    void InitDock();
    void RenderDock(int mx, int my, bool mdown);
}

#endif
// app/sysgui/gui/desktop.h
#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

#include "../api_gui.h"

namespace GUI {
    struct Theme {
        uint32_t c1;
        uint32_t c2;
        const char* name;
    };

    void InitDesktop();
    void UpdateDesktop(float dt, int mx, int my, bool mdown, uint16_t key);
    void RenderDesktop(Painter& p);
    void NextTheme();
    bool IsScreensaverActive();
}

#endif
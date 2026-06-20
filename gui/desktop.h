#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

#include <stdint.h>

namespace GUI {
    struct Theme {
        uint32_t c1;
        uint32_t c2;
        const char* name;
    };

    void InitDesktop();
    void UpdateDesktop(float dt, int mx, int my, bool mdown, uint16_t key);
    void RenderDesktop();
    void NextTheme();
    bool IsScreensaverActive();
}

#endif
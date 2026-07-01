// app/sysgui/gui/win_manager.h
#ifndef GUI_WIN_MANAGER_H
#define GUI_WIN_MANAGER_H

#include "../api_gui.h"

namespace GUI {
    void InitWindowManager();
    void RenderWindows(Painter& p, float dt, int mx, int my, bool mdown, uint16_t key);
    void OpenAppWindow(const char* title);
    bool IsAppActive(const char* title);
    App* GetActiveApp();
}

#endif
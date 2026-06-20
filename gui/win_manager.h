#ifndef GUI_WIN_MANAGER_H
#define GUI_WIN_MANAGER_H

namespace GUI {
    struct WindowState {
        const char* title;
        bool active;
        bool minimized;
    };

    void InitWindowManager();
    void OpenAppWindow(const char* title);
    bool IsAppActive(const char* title);
    void RenderWindows(int mx, int my, bool mdown, float dt);
}

#endif
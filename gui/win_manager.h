#ifndef GUI_WIN_MANAGER_H
#define GUI_WIN_MANAGER_H

namespace GUI {
    void InitWindowManager();
    void RenderWindows(float dt);
    
    // Публичное API для открытия приложений по имени
    void OpenAppWindow(const char* title);
    
    // Проверка, запущено ли приложение (для индикаторов в доке)
    bool IsAppActive(const char* title);
}

#endif
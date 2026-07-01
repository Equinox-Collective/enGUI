// app/sysgui/gui/win_manager.cpp
#include "win_manager.h"
#include "apps/terminal.h"
#include "apps/monitor.h"
#include "apps/paint.h"
#include <string.h>
#include <equos.h>
#include <stdio.h>

namespace GUI {

    static const int MAX_WINDOWS = 16;
    static App* g_ActiveWindows[MAX_WINDOWS];
    static int g_WindowCount = 0;
    static uint32_t g_NextInstanceID = 100;

    void InitWindowManager() {
        for (int i = 0; i < MAX_WINDOWS; i++) g_ActiveWindows[i] = nullptr;
    }

    void OpenAppWindow(const char* title) {
        if (g_WindowCount >= MAX_WINDOWS) return;

        App* new_app = nullptr;
        uint32_t uid = g_NextInstanceID++;
        int start_x = 100 + (g_WindowCount * 30);
        int start_y = 100 + (g_WindowCount * 20);

        if (strcmp(title, "Terminal") == 0)     new_app = new TerminalApp(uid, start_x, start_y);
        else if (strcmp(title, "Monitor") == 0) new_app = new MonitorApp(uid, start_x, start_y);
        else if (strcmp(title, "Paint") == 0)   new_app = new PaintApp(uid, start_x, start_y);

        if (new_app) {
            new_app->is_open = true;
            new_app->OnOpen();
            g_ActiveWindows[g_WindowCount++] = new_app;
        }
    }

    bool IsAppActive(const char* title) {
        for (int i = 0; i < g_WindowCount; i++) {
            if (g_ActiveWindows[i] && strcmp(g_ActiveWindows[i]->title, title) == 0 && g_ActiveWindows[i]->is_open) {
                return true;
            }
        }
        return false;
    }

    App* GetActiveApp() {
        if (g_WindowCount > 0) return g_ActiveWindows[g_WindowCount - 1];
        return nullptr;
    }

    void RenderWindows(Painter& p, float dt, int mx, int my, bool mdown, uint16_t key) {
        for (int i = 0; i < g_WindowCount; i++) {
            App* win = g_ActiveWindows[i];
            if (!win || !win->is_open) continue;

            // Рендеринг тени и стеклянного размытия
            draw_soft_shadow(win->x, win->y, win->w, win->h, WINDOW_ROUNDING_LARGE, 24, 0.55f, 0, 10);
            draw_acrylic_blur(win->x, win->y, win->w, win->h, 0.50f, WINDOW_ROUNDING_LARGE, 0x030206);

            // Отрисовка декорации окна (неоновая рамка)
            bool is_focused = (i == g_WindowCount - 1);
            uint32_t border_col = is_focused ? COLOR_ACCENT : 0x44BD00FF;
            p.DrawRoundedRect(win->x, win->y, win->w, win->h, WINDOW_ROUNDING_LARGE, border_col);

            // Заголовок окна
            p.Line(win->x, win->y + 30, win->x + win->w, win->y + 30, is_focused ? 0x8800E5FF : 0x3300E5FF);
            p.Text(win->title, win->x + 15, win->y + 7, 0xFFFFFFFF);

            // Кнопка закрытия [X] (неоновый красный)
            int close_x = win->x + win->w - 25;
            int close_y = win->y + 8;
            p.RoundedRect(close_x, close_y, 14, 14, 3, 0x33FF0055);
            p.DrawRect(close_x, close_y, 14, 14, 0xFFFF0055);
            p.Line(close_x + 4, close_y + 4, close_x + 10, close_y + 10, 0xFFFF0055);
            p.Line(close_x + 10, close_y + 4, close_x + 4, close_y + 10, 0xFFFF0055);

            // Обработка перемещения окна (Drag-and-Drop)
            if (mdown) {
                if (!win->dragging && mx >= win->x && mx <= win->x + win->w - 30 && my >= win->y && my <= win->y + 30) {
                    // Клик на заголовок переводит фокус
                    if (!is_focused) {
                        for (int j = i; j < g_WindowCount - 1; j++) g_ActiveWindows[j] = g_ActiveWindows[j+1];
                        g_ActiveWindows[g_WindowCount - 1] = win;
                    }
                    win->dragging = true;
                    win->drag_off_x = mx - win->x;
                    win->drag_off_y = my - win->y;
                }
                
                if (win->dragging) {
                    win->x = mx - win->drag_off_x;
                    win->y = my - win->drag_off_y;
                    sysgui_mark_dirty(win->x - 30, win->y - 30, win->w + 60, win->h + 60);
                }
            } else {
                win->dragging = false;
            }

            // Обработка закрытия окна
            if (mdown && mx >= close_x && mx <= close_x + 14 && my >= close_y && my <= close_y + 14) {
                win->is_open = false;
            }

            // Диспетчеризация событий ввода приложению
            if (is_focused && key != 0) {
                win->OnKeyEvent(key);
            }

            // Рендеринг внутренней области приложения
            Painter client_p(p.target, p.width, p.height);
            // Клиппинг нативного вывода
            win->OnRender(client_p, dt);

            // Удаление закрытого окна
            if (!win->is_open) {
                win->OnClose();
                delete win;
                for (int j = i; j < g_WindowCount - 1; j++) {
                    g_ActiveWindows[j] = g_ActiveWindows[j+1];
                }
                g_ActiveWindows[--g_WindowCount] = nullptr;
                i--;
            }
        }
    }
}
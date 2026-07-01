// app/sysgui/gui/panel.cpp
#include "panel.h"
#include "desktop.h"
#include "win_manager.h"
#include <equos.h>
#include <stdio.h>

extern uint32_t screen_w;

namespace GUI {

    void RenderTopPanel(Painter& p) {
        draw_soft_shadow(0, 0, screen_w, 28, 0, 8, 0.3f, 0, 2);
        draw_acrylic_blur(0, 0, screen_w, 28, 0.50f, 0, 0x030206);

        // Разделительная линия статус бара (Cosmic Cyan)
        p.Line(0, 27, screen_w, 27, 0x3300E5FF);

        // Системный логотип Equinox
        p.Text("ʘ EQUINOX", 15, 6, COLOR_ACCENT);

        // Динамический заголовок активного окна
        const char* active_title = "Equinox Desktop";
        App* active_app = GetActiveApp();
        if (active_app && active_app->is_open) {
            active_title = active_app->title;
        }
        p.Text(active_title, 130, 6, COLOR_ACCENT_CYAN);

        // Системные часы сверху справа
        uint64_t ms = _syscall(6, 0, 0, 0, 0, 0);
        int secs = (ms / 1000) % 60;
        int mins = (ms / 60000) % 60;
        int hours = (ms / 3600000) % 24;
        char time_str[32];
        sprintf(time_str, "%02d:%02d:%02d", hours, mins, secs);

        p.Text(time_str, screen_w - 90, 6, 0xFFFFFFFF);

        // Стилизованный неоновый значок батареи
        int bat_x = screen_w - 130;
        p.FillRect(bat_x, 8, 20, 12, COLOR_ACCENT_CYAN);
        p.FillRect(bat_x + 20, 11, 2, 6, COLOR_ACCENT_CYAN);
    }
}
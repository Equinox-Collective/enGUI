// app/sysgui/gui/desktop.cpp
#include "desktop.h"
#include <stdlib.h>
#include <string.h>
#include <equos.h>
#include <stdio.h>

extern uint32_t screen_w, screen_h;

namespace GUI {

    static const Theme g_Themes[] = {
        { 0x050510, 0x020205, "Cosmic Void" },
        { 0x1A082C, 0x080210, "Nebula Dust" },
        { 0x051E28, 0x02070D, "Solar Flare" }
    };

    static int g_CurrentThemeIdx = 0;
    static float g_IdleTime = 0.0f;
    static bool g_ScreensaverActive = false;

    struct Star { float x, y, z; };
    static const int MAX_STARS = 80;
    static Star g_Stars[MAX_STARS];

    void InitDesktop() {
        for (int i = 0; i < MAX_STARS; i++) {
            g_Stars[i].x = (float)(rand() % 1000 - 500);
            g_Stars[i].y = (float)(rand() % 1000 - 500);
            g_Stars[i].z = (float)(rand() % 1000 + 1);
        }
    }

    void UpdateDesktop(float dt, int mx, int my, bool mdown, uint16_t key) {
        static int last_mx = -1, last_my = -1;
        if (mx != last_mx || my != last_my || mdown || key != 0) {
            g_IdleTime = 0.0f;
            g_ScreensaverActive = false;
        } else {
            g_IdleTime += dt;
        }
        last_mx = mx; last_my = my;

        if (g_IdleTime > 60.0f) g_ScreensaverActive = true;

        if (g_ScreensaverActive) {
            for (int i = 0; i < MAX_STARS; i++) {
                g_Stars[i].z -= 300.0f * dt;
                if (g_Stars[i].z <= 1) {
                    g_Stars[i].z = 1000.0f;
                }
            }
        }
    }

    void RenderDesktop(Painter& p) {
        if (g_ScreensaverActive) {
            p.FillRect(0, 0, screen_w, screen_h, 0x000000);
            float cx = (float)screen_w / 2.0f;
            float cy = (float)screen_h / 2.0f;
            for (int i = 0; i < MAX_STARS; i++) {
                float x = (g_Stars[i].x / g_Stars[i].z) * 100.0f + cx;
                float y = (g_Stars[i].y / g_Stars[i].z) * 100.0f + cy;

                if (x >= 0 && x < screen_w && y >= 0 && y < screen_h) {
                    uint8_t bright = (uint8_t)(255.0f * (1.0f - g_Stars[i].z / 1000.0f));
                    uint32_t color = (bright << 16) | (bright << 8) | bright;
                    p.CircleFilled((int)x, (int)y, 2, color);
                }
            }
        } else {
            Theme t = g_Themes[g_CurrentThemeIdx];
            p.GradientRect(0, 0, screen_w, screen_h, t.c1, t.c2, true);

            // Отрисовка нативных Sonoma виджетов
            int widget_w = 260;
            int widget_h = 130;
            int start_x = screen_w - widget_w - 40;
            int start_y = 60;

            // 1. ВИДЖЕТ ЧАСОВ (Акриловое стекло + тень)
            draw_soft_shadow(start_x, start_y, widget_w, widget_h, WIDGET_ROUNDING, 12, 0.25f, 0, 4);
            draw_acrylic_blur(start_x, start_y, widget_w, widget_h, 0.4f, WIDGET_ROUNDING, 0x1E2235);
            p.Circle(start_x + 60, start_y + widget_h / 2, 40, 0x88FFFFFF, 2);
            p.Line(start_x + 60, start_y + widget_h / 2, start_x + 80, start_y + widget_h / 2 - 10, COLOR_ACCENT_CYAN, 2); // Стрелка
            
            p.Text("Monday", start_x + 120, start_y + 35, 0xFFFFFFFF);
            p.Text("JUNE 21", start_x + 120, start_y + 55, COLOR_ACCENT);
            p.Text("EquinoxOS", start_x + 120, start_y + 75, 0x88FFFFFF);

            // 2. ВИДЖЕТ РЕСУРСОВ
            start_y += widget_h + 30;
            draw_soft_shadow(start_x, start_y, widget_w, widget_h, WIDGET_ROUNDING, 12, 0.25f, 0, 4);
            draw_acrylic_blur(start_x, start_y, widget_w, widget_h, 0.4f, WIDGET_ROUNDING, 0x1E2235);
            
            p.Text("SYSTEM STATS", start_x + 20, start_y + 15, COLOR_ACCENT_CYAN);
            p.Line(start_x + 20, start_y + 35, start_x + widget_w - 20, start_y + 35, 0x33FFFFFF);

            uint64_t total = sys_get_total_mem() / (1024 * 1024);
            uint64_t used = sys_get_used_mem() / (1024 * 1024);
            float ratio = total > 0 ? (float)used / (float)total : 0.0f;

            char ram_str[48];
            sprintf(ram_str, "RAM: %llu / %llu MB", used, total);
            p.Text(ram_str, start_x + 20, start_y + 50, 0xFFFFFFFF);

            // Прогресс бар
            int bar_w = widget_w - 40;
            p.RoundedRect(start_x + 20, start_y + 80, bar_w, 10, 3, 0xFF1C1D26);
            p.RoundedRect(start_x + 20, start_y + 80, (int)(bar_w * ratio), 10, 3, COLOR_ACCENT_CYAN);
        }
    }

    void NextTheme() { g_CurrentThemeIdx = (g_CurrentThemeIdx + 1) % 3; }
    bool IsScreensaverActive() { return g_ScreensaverActive; }
}
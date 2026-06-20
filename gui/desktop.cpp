#include "desktop.h"
#include "../api_gui.h"
#include <equos.h>
#include <stdlib.h>

extern uint32_t *backbuffer;
extern uint32_t screen_w, screen_h;

namespace GUI {
    static const Theme g_Themes[] = {
        { 0x1A1C29, 0x0E1017, "Sonoma Dark" },
        { 0x1E102F, 0x0A0510, "Nebula Purple" },
        { 0x0B1D20, 0x04090A, "Aqua Marin" }
    };
    static int g_CurrentThemeIdx = 0;

    // Скринсейвер "Starfield"
    struct Star {
        float x, y, z;
    };
    static const int MAX_STARS = 60;
    static Star g_Stars[MAX_STARS];
    static float g_LastInputTime = 0.0f;
    static bool g_ScreensaverActive = false;

    void InitDesktop() {
        g_LastInputTime = (float)_syscall(6, 0, 0, 0, 0, 0) / 1000.0f;
        for (int i = 0; i < MAX_STARS; i++) {
            g_Stars[i].x = (float)(rand() % 600 - 300);
            g_Stars[i].y = (float)(rand() % 600 - 300);
            g_Stars[i].z = (float)(rand() % 400 + 1);
        }
    }

    void UpdateDesktop(float dt, int mx, int my, bool mdown, uint16_t key) {
        static int last_mx = -1, last_my = -1;
        static bool last_mdown = false;
        
        float now = (float)_syscall(6, 0, 0, 0, 0, 0) / 1000.0f;
        
        if (mx != last_mx || my != last_my || mdown != last_mdown || key != 0) {
            g_LastInputTime = now;
            g_ScreensaverActive = false;
        }
        
        last_mx = mx; last_my = my; last_mdown = mdown;

        if (now - g_LastInputTime > 15.0f) {
            g_ScreensaverActive = true;
        }

        if (g_ScreensaverActive) {
            for (int i = 0; i < MAX_STARS; i++) {
                g_Stars[i].z -= 150.0f * dt;
                if (g_Stars[i].z <= 0) {
                    g_Stars[i].x = (float)(rand() % 600 - 300);
                    g_Stars[i].y = (float)(rand() % 600 - 300);
                    g_Stars[i].z = 400.0f;
                }
            }
        }
    }

    void RenderDesktop() {
        if (g_ScreensaverActive) {
            // Рисуем космос
            eid_draw_rect(backbuffer, screen_w, screen_h, 0, 0, screen_w, screen_h, 0x000000);
            float sw_f = (float)screen_w;
            float sh_f = (float)screen_h;
            for (int i = 0; i < MAX_STARS; i++) {
                float k = 120.0f / g_Stars[i].z;
                int sx = (int)(sw_f / 2.0f + g_Stars[i].x * k);
                int sy = (int)(sh_f / 2.0f + g_Stars[i].y * k);
                if (sx >= 0 && sx < (int)screen_w && sy >= 0 && sy < (int)screen_h) {
                    int bright = (int)((1.0f - (g_Stars[i].z / 400.0f)) * 255.0f);
                    if (bright < 0) bright = 0; if (bright > 255) bright = 255;
                    uint32_t col = (bright << 16) | (bright << 8) | bright;
                    eid_draw_rect(backbuffer, screen_w, screen_h, sx, sy, 2, 2, col);
                }
            }
        } else {
            // Рисуем градиент темы
            Theme t = g_Themes[g_CurrentThemeIdx];
            eid_draw_gradient_rect(backbuffer, screen_w, screen_h, 0, 0, screen_w, screen_h, t.c1, t.c2, true);
        }
    }

    void NextTheme() {
        g_CurrentThemeIdx = (g_CurrentThemeIdx + 1) % 3;
    }

    bool IsScreensaverActive() {
        return g_ScreensaverActive;
    }
}
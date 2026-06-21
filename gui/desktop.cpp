#include "desktop.h"
#include "../api_gui.h"

extern "C" {
#include <stdlib.h>
#include <string.h>  // Для memset
#include <equos.h>
#include <eid.h> 
}

extern uint32_t *backbuffer;
extern uint32_t screen_w, screen_h;

namespace GUI {

    // struct Theme удален, так как он уже в desktop.h

    static const Theme g_Themes[] = {
        { 0x1A1C29, 0x0E1017, "Sonoma Dark" },
        { 0x1E102F, 0x0A0510, "Nebula Purple" },
        { 0x0B1D20, 0x04090A, "Aqua Marin" }
    };

    static int g_CurrentThemeIdx = 0;
    static float g_IdleTime = 0.0f;
    static bool g_ScreensaverActive = false;

    struct Star {
        float x, y, z;
        float prev_z;
    };
    static const int MAX_STARS = 150;
    static Star g_Stars[MAX_STARS];

    void InitDesktop() {
        for (int i = 0; i < MAX_STARS; i++) {
            g_Stars[i].x = (float)(rand() % 1000 - 500);
            g_Stars[i].y = (float)(rand() % 1000 - 500);
            g_Stars[i].z = (float)(rand() % 1000 + 1);
            g_Stars[i].prev_z = g_Stars[i].z;
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

        if (g_IdleTime > 30.0f) g_ScreensaverActive = true;

        if (g_ScreensaverActive) {
            for (int i = 0; i < MAX_STARS; i++) {
                g_Stars[i].prev_z = g_Stars[i].z;
                g_Stars[i].z -= 400.0f * dt;
                if (g_Stars[i].z <= 1) {
                    g_Stars[i].z = 1000.0f;
                    g_Stars[i].prev_z = g_Stars[i].z;
                }
            }
        }
    }

    void RenderDesktop() {
        if (g_ScreensaverActive) {
            memset(backbuffer, 0, screen_w * screen_h * 4);
            float cx = (float)screen_w / 2.0f;
            float cy = (float)screen_h / 2.0f;
            for (int i = 0; i < MAX_STARS; i++) {
                float x = (g_Stars[i].x / g_Stars[i].z) * 100.0f + cx;
                float y = (g_Stars[i].y / g_Stars[i].z) * 100.0f + cy;
                float px = (g_Stars[i].x / g_Stars[i].prev_z) * 100.0f + cx;
                float py = (g_Stars[i].y / g_Stars[i].prev_z) * 100.0f + cy;

                if (x >= 0 && x < screen_w && y >= 0 && y < screen_h) {
                    uint8_t bright = (uint8_t)(255.0f * (1.0f - g_Stars[i].z / 1000.0f));
                    uint32_t color = (bright << 16) | (bright << 8) | bright;
                    eid_draw_line(backbuffer, screen_w, screen_h, (int)px, (int)py, (int)x, (int)y, color);
                }
            }
        } else {
            Theme t = g_Themes[g_CurrentThemeIdx];
            // Используем t.c1 и t.c2 вместо top_color/bottom_color
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
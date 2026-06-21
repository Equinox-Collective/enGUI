#include "desktop.h"
#include "../api_gui.h"

extern "C" {
#include <stdlib.h>
#include <equos.h>
#include <eid.h> // Используем системные примитивы рисования
}

extern uint32_t *backbuffer;
extern uint32_t screen_w, screen_h;

namespace GUI {

    // Структура темы оформления
    struct Theme {
        uint32_t top_color;
        uint32_t bottom_color;
        const char* name;
    };

    static const Theme g_Themes[] = {
        { 0x5C4B9B, 0x1A1C29, "Sonoma Night" }, // Глубокий фиолетовый
        { 0xFF8C42, 0x6B2D5C, "Solar Flare" },  // Оранжево-розовый
        { 0x4FACFE, 0x00F2FE, "Aqua Flow" }     // Лазурный
    };

    static int g_CurrentThemeIdx = 0;
    static float g_IdleTime = 0.0f;
    static bool g_ScreensaverActive = false;

    // Параметры звезд для скринсейвера
    struct Star {
        float x, y, z;
        float prev_z;
    };
    static const int MAX_STARS = 150;
    static Star g_Stars[MAX_STARS];

    void InitDesktop() {
        // Инициализация звезд в 3D пространстве
        for (int i = 0; i < MAX_STARS; i++) {
            g_Stars[i].x = (float)(rand() % 1000 - 500);
            g_Stars[i].y = (float)(rand() % 1000 - 500);
            g_Stars[i].z = (float)(rand() % 1000);
            g_Stars[i].prev_z = g_Stars[i].z;
        }
    }

    void UpdateDesktop(float dt, int mx, int my, bool mdown, uint16_t key) {
        static int last_mx = -1, last_my = -1;
        
        // Проверка активности пользователя
        if (mx != last_mx || my != last_my || mdown || key != 0) {
            g_IdleTime = 0.0f;
            g_ScreensaverActive = false;
        } else {
            g_IdleTime += dt;
        }
        last_mx = mx; last_my = my;

        // Активация скринсейвера через 30 секунд бездействия
        if (g_IdleTime > 30.0f) {
            g_ScreensaverActive = true;
        }

        if (g_ScreensaverActive) {
            for (int i = 0; i < MAX_STARS; i++) {
                g_Stars[i].prev_z = g_Stars[i].z;
                g_Stars[i].z -= 400.0f * dt; // Скорость полета
                if (g_Stars[i].z <= 1) {
                    g_Stars[i].z = 1000.0f;
                    g_Stars[i].prev_z = g_Stars[i].z;
                }
            }
        }
    }

    void RenderDesktop() {
        if (g_ScreensaverActive) {
            // Очистка черным для космоса
            memset(backbuffer, 0, screen_w * screen_h * 4);
            
            float cx = (float)screen_w / 2.0f;
            float cy = (float)screen_h / 2.0f;

            for (int i = 0; i < MAX_STARS; i++) {
                // Проекция 3D -> 2D
                float x = (g_Stars[i].x / g_Stars[i].z) * 100.0f + cx;
                float y = (g_Stars[i].y / g_Stars[i].z) * 100.0f + cy;
                
                float px = (g_Stars[i].x / g_Stars[i].prev_z) * 100.0f + cx;
                float py = (g_Stars[i].y / g_Stars[i].prev_z) * 100.0f + cy;

                if (x >= 0 && x < screen_w && y >= 0 && y < screen_h) {
                    // Яркость зависит от расстояния (Z)
                    uint8_t bright = (uint8_t)(255.0f * (1.0f - g_Stars[i].z / 1000.0f));
                    uint32_t color = (bright << 16) | (bright << 8) | bright;
                    
                    // Рисуем "луч" (line) от предыдущей позиции к текущей для эффекта скорости
                    eid_draw_line(backbuffer, screen_w, screen_h, (int)px, (int)py, (int)x, (int)y, color);
                }
            }
        } else {
            // Рисуем Sonoma-градиент
            Theme t = g_Themes[g_CurrentThemeIdx];
            eid_draw_gradient_rect(backbuffer, screen_w, screen_h, 0, 0, screen_w, screen_h, t.top_color, t.bottom_color, true);
            
            // Добавляем легкое акриловое пятно в углу для глубины
            draw_acrylic_blur(screen_w - 400, -100, 500, 500, 0.2f, 250, 0xFFFFFF);
        }
    }

    void NextTheme() {
        g_CurrentThemeIdx = (g_CurrentThemeIdx + 1) % 3;
        sysgui_mark_dirty(0, 0, screen_w, screen_h);
    }

    bool IsScreensaverActive() {
        return g_ScreensaverActive;
    }
}
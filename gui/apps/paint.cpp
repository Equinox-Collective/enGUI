// app/sysgui/gui/apps/paint.cpp
#include "paint.h"
#include <equos.h>

namespace GUI {

    PaintApp::PaintApp(uint32_t id, int start_x, int start_y) 
        : App("Vector Paint", id, start_x, start_y, 450, 320) {
        point_count = 0;
        current_color = 0xFFFFFFFF; // Белый
    }

    void PaintApp::OnRender(Painter& p, float dt) {
        (void)dt;
        // 1. Отрисовка палитры цветов слева
        int pal_x = x + 15;
        int pal_y = y + 45;
        
        uint32_t colors[] = { 0xFFFFFFFF, 0xFFFF3B30, 0xFFFFCC00, 0xFF34C759, 0xFF007AFF, 0xFFBD00FF };
        int color_count = sizeof(colors) / sizeof(uint32_t);

        p.Text("PALETTE", pal_x, pal_y, 0x88FFFFFF);
        pal_y += 20;

        uint64_t r_mx = 0, r_my = 0, r_btn = 0;
        __asm__ volatile("mov $7, %%rax\n\tint $0x80" : "=r"(r_mx), "=r"(r_my), "=r"(r_btn));
        int mx = (int)r_mx, my = (int)r_my;
        bool mdown = (r_btn & 1);

        for (int i = 0; i < color_count; i++) {
            p.RoundedRect(pal_x, pal_y + i * 30, 24, 24, 6, colors[i]);
            if (current_color == colors[i]) {
                p.Circle(pal_x + 12, pal_y + i * 30 + 12, 14, 0xFFFFFFFF);
            }

            // Клик по палитре
            if (mdown && mx >= pal_x && mx <= pal_x + 24 && my >= (pal_y + i * 30) && my <= (pal_y + i * 30 + 24)) {
                current_color = colors[i];
            }
        }

        // Кнопка очистки
        int clear_y = pal_y + color_count * 30 + 10;
        p.RoundedRect(pal_x, clear_y, 70, 26, 4, 0x33FF3B30);
        p.DrawRoundedRect(pal_x, clear_y, 70, 26, 4, 0xFFFF3B30);
        p.Text("CLEAR", pal_x + 14, clear_y + 5, 0xFFFF3B30);

        if (mdown && mx >= pal_x && mx <= pal_x + 70 && my >= clear_y && my <= clear_y + 26) {
            point_count = 0;
            sysgui_mark_dirty(x, y, w, h);
        }

        // 2. Отрисовка холста
        int canvas_x = x + 100;
        int canvas_y = y + 45;
        int canvas_w = w - 115;
        int canvas_h = h - 60;

        p.RoundedRect(canvas_x, canvas_y, canvas_w, canvas_h, 6, 0xFF0A0C16);
        p.DrawRoundedRect(canvas_x, canvas_y, canvas_w, canvas_h, 6, 0x33FFFFFF);

        // Рисование мыслью на холсте
        if (mdown && mx >= canvas_x + 5 && mx <= canvas_x + canvas_w - 5 &&
            my >= canvas_y + 5 && my <= canvas_y + canvas_h - 5) {
            if (point_count < MAX_POINTS) {
                points[point_count].x = mx;
                points[point_count].y = my;
                points[point_count].color = current_color;
                point_count++;
                sysgui_mark_dirty(canvas_x, canvas_y, canvas_w, canvas_h);
            }
        }

        // Отрисовка точек на холсте
        for (int i = 0; i < point_count; i++) {
            p.CircleFilled(points[i].x, points[i].y, 3, points[i].color);
        }
    }
}
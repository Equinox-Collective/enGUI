// app/sysgui/gui/dock.cpp
#include "dock.h"
#include "win_manager.h"
#include <math.h>
#include <string.h>
#include <equos.h>

extern uint32_t screen_w, screen_h;

namespace GUI {

    struct DockApp {
        const char* name;
        uint32_t color;
    };

    static DockApp g_DockApps[] = {
        { "Terminal",  0xFF1C1D22 },
        { "Monitor",   0xFF00E5FF },
        { "Paint",     0xFFFF007F }
    };
    static const int DOCK_COUNT = sizeof(g_DockApps) / sizeof(DockApp);
    static float g_IconScales[DOCK_COUNT];

    void InitDock() {
        for (int i = 0; i < DOCK_COUNT; i++) g_IconScales[i] = 1.0f;
    }

    static void DrawVectorIcon(Painter& p, int cx, int cy, int size, const char* name, float scale) {
        int r = size / 2;
        int x1 = cx - r, y1 = cy - r;
        p.RoundedRect(x1, y1, size, size, 10, 0xFF14161F);
        p.DrawRoundedRect(x1, y1, size, size, 10, 0x44FFFFFF);

        if (strcmp(name, "Terminal") == 0) {
            int offset = (int)(10.0f * scale);
            p.Line(cx - offset, cy - offset, cx - offset + (int)(6*scale), cy - offset + (int)(6*scale), 0xFF4AF626, 2);
            p.Line(cx - offset + (int)(6*scale), cy - offset + (int)(6*scale), cx - offset, cy - offset + (int)(12*scale), 0xFF4AF626, 2);
            p.Line(cx - (int)(2*scale), cy + (int)(2*scale), cx + (int)(8*scale), cy + (int)(2*scale), 0xFF4AF626, 3);
        }
        else if (strcmp(name, "Monitor") == 0) {
            int hw = (int)(12.0f * scale);
            p.DrawRect(cx - hw, cy - (int)(8*scale), hw * 2, (int)(16*scale), 0xFF8E8E93, 2);
            p.Line(cx - (int)(8*scale), cy + (int)(2*scale), cx - (int)(3*scale), cy - (int)(5*scale), 0xFF28C76F, 2);
            p.Line(cx - (int)(3*scale), cy - (int)(5*scale), cx + (int)(3*scale), cy + (int)(5*scale), 0xFF28C76F, 2);
            p.Line(cx + (int)(3*scale), cy + (int)(5*scale), cx + (int)(8*scale), cy - (int)(3*scale), 0xFF28C76F, 2);
        }
        else if (strcmp(name, "Paint") == 0) {
            p.CircleFilled(cx, cy, (int)(12.0f * scale), 0xFFFFFFFF);
            p.CircleFilled(cx - (int)(4*scale), cy - (int)(4*scale), (int)(3*scale), 0xFFFF3B30);
            p.CircleFilled(cx + (int)(4*scale), cy - (int)(4*scale), (int)(3*scale), 0xFFFFCC00);
            p.CircleFilled(cx, cy + (int)(4*scale), (int)(3*scale), 0xFF34C759);
        }
    }

    void RenderDock(Painter& p, int mx, int my, bool mdown) {
        const int base_icon_size = 52;
        const int pad = 14;
        const int dock_h = base_icon_size + pad * 2;
        
        float total_w = pad;
        for(int i = 0; i < DOCK_COUNT; i++) total_w += (base_icon_size * g_IconScales[i]) + pad;
        
        int dock_x = (screen_w - (int)total_w) / 2;
        int dock_y = screen_h - dock_h - 15;

        draw_soft_shadow(dock_x, dock_y, (int)total_w, dock_h, DOCK_ROUNDING, 16, 0.45f, 0, 6);
        draw_acrylic_blur(dock_x, dock_y, (int)total_w, dock_h, 0.55f, DOCK_ROUNDING, 0x030206);
        
        // Окантовка Дока
        p.DrawRoundedRect(dock_x, dock_y, (int)total_w, dock_h, DOCK_ROUNDING, 0x5500E5FF);

        float current_x = (float)dock_x + pad;

        for (int i = 0; i < DOCK_COUNT; i++) {
            float icon_center_x = current_x + (base_icon_size * g_IconScales[i]) / 2.0f;
            float dist = fabsf((float)mx - icon_center_x);
            
            float target_scale = 1.0f;
            if (my > dock_y - 80 && my < dock_y + dock_h + 80 && mx > dock_x && mx < dock_x + (int)total_w) {
                float factor = 1.0f - (dist / 180.0f);
                if (factor < 0) factor = 0;
                target_scale = 1.0f + (factor * 0.45f);
            }
            
            g_IconScales[i] += (target_scale - g_IconScales[i]) * 0.25f;

            float sz = base_icon_size * g_IconScales[i];
            float yy = (float)dock_y + (dock_h - sz) / 2.0f;

            DrawVectorIcon(p, (int)icon_center_x, (int)(yy + sz/2.0f), (int)sz, g_DockApps[i].name, g_IconScales[i]);

            // Индикатор запуска процесса
            if (IsAppActive(g_DockApps[i].name)) {
                p.CircleFilled((int)icon_center_x, dock_y + dock_h - 7, 3, COLOR_ACCENT_CYAN);
            }

            // Обработка клика
            if (mdown && mx >= current_x && mx <= current_x + sz && my >= yy && my <= yy + sz) {
                static uint32_t last_click_tick = 0;
                uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
                if (now - last_click_tick > 400) { 
                    OpenAppWindow(g_DockApps[i].name);
                    last_click_tick = now;
                }
            }

            current_x += sz + pad;
        }
    }
}
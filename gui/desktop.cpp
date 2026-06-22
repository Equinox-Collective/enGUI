#include "desktop.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <equos.h>
#include <eid.h> 
#include <stdio.h>
}

extern uint32_t *backbuffer;
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

        if (g_IdleTime > 90.0f) g_ScreensaverActive = true; // Увеличили порог скринсейвера

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

    // Рендеринг красивых виджетов на рабочий стол в стиле macOS Sonoma Dashboard
    static void DrawDesktopWidgets() {
        if (g_ScreensaverActive) return;

        // Позиции виджетов в правой стороне экрана
        int widget_w = 260;
        int widget_h = 160;
        int start_x = screen_w - widget_w - 40;
        int start_y = 60;

        // 1. ВИДЖЕТ ЧАСОВ (Стеклянная подложка + красивый дизайн)
        draw_soft_shadow(start_x, start_y, widget_w, widget_h, WIDGET_ROUNDING, 12, 0.25f, 0, 4);
        draw_acrylic_blur(start_x, start_y, widget_w, widget_h, 0.5f, WIDGET_ROUNDING, 0x1E2235);
        
        ImGui::SetNextWindowPos(ImVec2((float)start_x, (float)start_y));
        ImGui::SetNextWindowSize(ImVec2((float)widget_w, (float)widget_h));
        ImGui::Begin("##WidgetClock", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 center(start_x + 70, start_y + widget_h / 2);
            float radius = 50.0f;

            // Циферблат часов
            draw->AddCircleFilled(center, radius, 0x22121420, 64);
            draw->AddCircle(center, radius, 0x558E8E93, 64, 1.5f);

            // Фейковые стрелочки для красоты
            draw->AddLine(center, ImVec2(center.x + 25, center.y - 15), 0xFFFFFFFF, 2.5f); // Часовая
            draw->AddLine(center, ImVec2(center.x + 5, center.y + 35), 0xFF007AFF, 1.5f);  // Минутная
            draw->AddCircleFilled(center, 4.0f, 0xFF007AFF);

            // Текстовая дата рядом со стрелками
            ImGui::SetCursorPos(ImVec2(140, 45));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
            ImGui::Text("Monday");
            ImGui::SetCursorPos(ImVec2(140, 65));
            ImGui::TextColored(ImVec4(0.0f, 0.48f, 1.0f, 1.0f), "JUNE 21");
            ImGui::SetCursorPos(ImVec2(140, 85));
            ImGui::TextColored(ImVec4(1,1,1,0.5f), "EquinoxOS");
            ImGui::PopStyleColor();
        }
        ImGui::End();

        // 2. ВИДЖЕТ СТАТИСТИКИ РЕСУРСОВ
        start_y += widget_h + 30;
        draw_soft_shadow(start_x, start_y, widget_w, widget_h, WIDGET_ROUNDING, 12, 0.25f, 0, 4);
        draw_acrylic_blur(start_x, start_y, widget_w, widget_h, 0.5f, WIDGET_ROUNDING, 0x1E2235);

        ImGui::SetNextWindowPos(ImVec2((float)start_x, (float)start_y));
        ImGui::SetNextWindowSize(ImVec2((float)widget_w, (float)widget_h));
        ImGui::Begin("##WidgetSysInfo", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        {
            ImGui::SetCursorPos(ImVec2(20, 20));
            ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "SYSTEM HEALTH");
            ImGui::Separator();
            
            uint64_t total = sys_get_total_mem() / (1024 * 1024);
            uint64_t used = sys_get_used_mem() / (1024 * 1024);
            float ratio = total > 0 ? (float)used / (float)total : 0.0f;

            ImGui::SetCursorPos(ImVec2(20, 60));
            ImGui::Text("RAM Occupied: %llu%%", (uint64_t)(ratio * 100));
            
            // Красивый сглаженный прогресс-бар памяти
            ImDrawList* dlist = ImGui::GetWindowDrawList();
            ImVec2 p_min(start_x + 20, start_y + 90);
            ImVec2 p_max(start_x + widget_w - 20, start_y + 104);
            dlist->AddRectFilled(p_min, p_max, 0xFF1C1D26, 6.0f);
            
            float width = (widget_w - 40) * ratio;
            if (width > 0) {
                ImVec2 p_act_max(start_x + 20 + width, start_y + 104);
                dlist->AddRectFilled(p_min, p_act_max, 0xFF007AFF, 6.0f);
            }

            ImGui::SetCursorPos(ImVec2(20, 120));
            ImGui::TextColored(ImVec4(1,1,1,0.4f), "Uptime: Active");
        }
        ImGui::End();
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
            // Плавный высококачественный градиент для обоев
            eid_draw_gradient_rect(backbuffer, screen_w, screen_h, 0, 0, screen_w, screen_h, t.c1, t.c2, true);
            DrawDesktopWidgets();
        }
    }

    void NextTheme() {
        g_CurrentThemeIdx = (g_CurrentThemeIdx + 1) % 3;
    }

    bool IsScreensaverActive() {
        return g_ScreensaverActive;
    }
}
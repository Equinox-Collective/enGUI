#include "dock.h"
#include "win_manager.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include <math.h>

extern "C" {
#include <equos.h>
#include <stdlib.h>
#include <string.h>
}

extern uint32_t screen_w, screen_h;

namespace GUI {

    struct DockApp {
        const char* name;
        uint32_t color;
        bool is_internal;
    };

    static DockApp g_DockApps[] = {
        { "Finder",    0x007AFF, true },
        { "Terminal",  0x1C1D22, true },
        { "Monitor",   0x28C76F, true },
        { "Paint",     0xFF9F43, true }
    };
    static const int DOCK_COUNT = sizeof(g_DockApps) / sizeof(DockApp);
    static float g_IconScales[DOCK_COUNT];

    void InitDock() {
        for (int i = 0; i < DOCK_COUNT; i++) g_IconScales[i] = 1.0f;
    }

    // Рендеринг красивых детализированных векторных иконок приложений
    static void DrawVectorIcon(ImDrawList* draw, ImVec2 p1, ImVec2 p2, const char* name, float scale) {
        ImVec2 center((p1.x + p2.x) / 2.0f, (p1.y + p2.y) / 2.0f);
        float size = p2.x - p1.x;

        // Отрисовка подложки иконки с объемным скруглением
        draw->AddRectFilled(p1, p2, 0xFF14161F, 14.0f);
        draw->AddRect(p1, p2, 0x44FFFFFF, 14.0f, 0, 1.5f);

        if (strcmp(name, "Finder") == 0) {
            // macOS Finder Style Face
            float eye_offset = 8.0f * scale;
            float eye_size = 3.0f * scale;
            
            // Смайлик-лицо разделенное линией посередине
            draw->AddLine(ImVec2(center.x, p1.y + 8), ImVec2(center.x, p2.y - 8), 0xFFFFFFFF, 2.0f);
            draw->AddCircleFilled(ImVec2(center.x - eye_offset, center.y - eye_offset), eye_size, 0xFF007AFF);
            draw->AddCircleFilled(ImVec2(center.x + eye_offset, center.y - eye_offset), eye_size, 0xFF007AFF);
            
            // Улыбка
            draw->AddLine(ImVec2(center.x - 10 * scale, center.y + 8 * scale), 
                          ImVec2(center.x + 10 * scale, center.y + 8 * scale), 0xFF007AFF, 2.0f);
        }
        else if (strcmp(name, "Terminal") == 0) {
            // Иконка консоли '>_'
            float offset = 10.0f * scale;
            // Символ '>'
            draw->AddLine(ImVec2(center.x - offset, center.y - offset), ImVec2(center.x - offset + 6 * scale, center.y - offset + 6 * scale), 0xFF4AF626, 2.0f);
            draw->AddLine(ImVec2(center.x - offset + 6 * scale, center.y - offset + 6 * scale), ImVec2(center.x - offset, center.y - offset + 12 * scale), 0xFF4AF626, 2.0f);
            // Курсор '_'
            draw->AddLine(ImVec2(center.x - 2 * scale, center.y + 2 * scale), ImVec2(center.x + 8 * scale, center.y + 2 * scale), 0xFF4AF626, 2.5f);
        }
        else if (strcmp(name, "Monitor") == 0) {
            // Монитор активности (сетка + синусоида биения сердца)
            float h_w = 14.0f * scale;
            draw->AddRect(ImVec2(center.x - h_w, center.y - 10 * scale), ImVec2(center.x + h_w, center.y + 10 * scale), 0xFF8E8E93, 4.0f, 0, 2.0f);
            
            // Волна пульса
            draw->AddLine(ImVec2(center.x - 10 * scale, center.y + 2 * scale), ImVec2(center.x - 4 * scale, center.y - 6 * scale), 0xFF28C76F, 2.0f);
            draw->AddLine(ImVec2(center.x - 4 * scale, center.y - 6 * scale), ImVec2(center.x + 2 * scale, center.y + 6 * scale), 0xFF28C76F, 2.0f);
            draw->AddLine(ImVec2(center.x + 2 * scale, center.y + 6 * scale), ImVec2(center.x + 10 * scale, center.y - 4 * scale), 0xFF28C76F, 2.0f);
        }
        else if (strcmp(name, "Paint") == 0) {
            // Палитра художника с цветными красками
            draw->AddCircleFilled(center, 12.0f * scale, 0xFFFFFFFF, 32);
            // Пятна цвета
            draw->AddCircleFilled(ImVec2(center.x - 4 * scale, center.y - 4 * scale), 3.0f * scale, 0xFFFF3B30);
            draw->AddCircleFilled(ImVec2(center.x + 4 * scale, center.y - 4 * scale), 3.0f * scale, 0xFFFFCC00);
            draw->AddCircleFilled(ImVec2(center.x, center.y + 4 * scale), 3.0f * scale, 0xFF34C759);
        }
    }

    void RenderDock(int mx, int my, bool mdown) {
        const int base_icon_size = 52;
        const int pad = 14;
        const int dock_h = base_icon_size + pad * 2;
        
        float total_w = pad;
        for(int i = 0; i < DOCK_COUNT; i++) total_w += (base_icon_size * g_IconScales[i]) + pad;
        
        int dock_x = (screen_w - (int)total_w) / 2;
        int dock_y = screen_h - dock_h - 15; 

        // Акриловая стеклянная подложка
        draw_acrylic_blur(dock_x, dock_y, (int)total_w, dock_h, 0.45f, DOCK_ROUNDING, 0x14161F);
        
        ImGui::SetNextWindowPos(ImVec2((float)dock_x, (float)dock_y));
        ImGui::SetNextWindowSize(ImVec2(total_w, (float)dock_h));
        ImGui::Begin("##DockUI", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            
            // Тонкая стильная окантовка дока сверху
            draw->AddLine(ImVec2((float)dock_x, (float)dock_y), ImVec2((float)dock_x + total_w, (float)dock_y), 0x33FFFFFF, 1.5f);

            float current_x = (float)dock_x + pad;

            for (int i = 0; i < DOCK_COUNT; i++) {
                float icon_center_x = current_x + (base_icon_size * g_IconScales[i]) / 2.0f;
                float dist = fabsf((float)mx - icon_center_x);
                
                float target_scale = 1.0f;
                if (my > dock_y - 80 && my < dock_y + dock_h + 80 && mx > dock_x && mx < dock_x + total_w) {
                    float factor = 1.0f - (dist / 180.0f);
                    if (factor < 0) factor = 0;
                    target_scale = 1.0f + (factor * 0.45f); // Плавное увеличение на 45%
                }
                
                g_IconScales[i] += (target_scale - g_IconScales[i]) * 0.25f;

                float sz = base_icon_size * g_IconScales[i];
                float yy = (float)dock_y + (dock_h - sz) / 2.0f;

                ImVec2 p1(current_x, yy);
                ImVec2 p2(current_x + sz, yy + sz);
                
                // Рендерим иконку векторами
                DrawVectorIcon(draw, p1, p2, g_DockApps[i].name, g_IconScales[i]);

                // Индикатор того, что приложение запущено (точка снизу)
                if (IsAppActive(g_DockApps[i].name)) {
                    draw->AddCircleFilled(ImVec2(icon_center_x, (float)dock_y + dock_h - 7), 3.0f, 0xFF007AFF);
                }

                // ПОДСКАЗКА С ИМЕНЕМ ПРИЛОЖЕНИЯ (Эстетичный стеклянный тултип)
                if (mx >= current_x && mx <= current_x + sz && my >= yy && my <= yy + sz) {
                    ImGui::SetNextWindowPos(ImVec2(icon_center_x - 40, (float)dock_y - 45));
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", g_DockApps[i].name);
                    ImGui::EndTooltip();
                }

                // Логика нажатия
                if (mdown && mx >= current_x && mx <= current_x + sz && my >= yy && my <= yy + sz) {
                    static uint32_t last_click_tick = 0;
                    uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
                    if (now - last_click_tick > 400) { 
                        OpenAppWindow(g_DockApps[i].name);
                        last_click_tick = now;
                        play_wav_file("res/sysgui/click.wav");
                    }
                }

                current_x += sz + pad;
            }
        }
        ImGui::End();
    }
}
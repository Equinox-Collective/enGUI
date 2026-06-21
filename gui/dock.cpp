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
        const char* icon_text;
        uint32_t color;
        bool is_internal; // true - функция в коде, false - бинарник на диске
    };

    static DockApp g_DockApps[] = {
        { "Finder",    "F", 0x4FACFE, true },
        { "Terminal",  ">", 0x21252B, true },
        { "Monitor",   "M", 0x48BB78, true },
        { "Paint",     "P", 0xED8936, true },
        { "Notepad",   "N", 0xA0AEC0, true },
        { "Settings",  "S", 0x718096, true }
    };
    static const int DOCK_COUNT = sizeof(g_DockApps) / sizeof(DockApp);

    // Параметры анимации
    static float g_IconScales[DOCK_COUNT];

    void InitDock() {
        for (int i = 0; i < DOCK_COUNT; i++) g_IconScales[i] = 1.0f;
    }

    void RenderDock(int mx, int my, bool mdown) {
        // Константы дока
        const int base_icon_size = 48;
        const int pad = 12;
        const int dock_h = base_icon_size + pad * 2;
        
        // Динамический расчет ширины дока
        float total_w = pad;
        for(int i=0; i<DOCK_COUNT; i++) total_w += (base_icon_size * g_IconScales[i]) + pad;
        
        int dock_x = (screen_w - (int)total_w) / 2;
        int dock_y = screen_h - dock_h - 15; // 15px отступ снизу

        // 1. Отрисовка Акриловой подложки
        draw_acrylic_blur(dock_x, dock_y, (int)total_w, dock_h, 0.5f, 20, 0x1A1C29);
        
        // Добавляем тонкую рамку (бордер) для красоты
        // (Реализуем через ImGui Overlay для простоты)
        ImGui::SetNextWindowPos(ImVec2((float)dock_x, (float)dock_y));
        ImGui::SetNextWindowSize(ImVec2(total_w, (float)dock_h));
        ImGui::Begin("##DockUI", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            float current_x = (float)dock_x + pad;

            for (int i = 0; i < DOCK_COUNT; i++) {
                // 2. Расчет Magnification (увеличения)
                float icon_center_x = current_x + (base_icon_size * g_IconScales[i]) / 2.0f;
                float dist = fabsf((float)mx - icon_center_x);
                
                // Если мышь близко к доку по вертикали
                float target_scale = 1.0f;
                if (my > dock_y - 100) {
                    float factor = 1.0f - (dist / 200.0f);
                    if (factor < 0) factor = 0;
                    target_scale = 1.0f + (factor * 0.6f); // Макс увеличение +60%
                }
                
                // Плавная интерполяция размера (Lerp)
                g_IconScales[i] += (target_scale - g_IconScales[i]) * 0.2f;

                float sz = base_icon_size * g_IconScales[i];
                float yy = (float)dock_y + (dock_h - sz) / 2.0f;

                // 3. Отрисовка Иконки
                ImVec2 p1(current_x, yy);
                ImVec2 p2(current_x + sz, yy + sz);
                
                // Фон иконки (Скругленный квадрат)
                draw->AddRectFilled(p1, p2, g_DockApps[i].color | 0xFF000000, 12.0f);
                
                // Текст-глиф по центру
                ImGui::SetWindowFontScale(g_IconScales[i] * 1.5f);
                ImVec2 txt_sz = ImGui::CalcTextSize(g_DockApps[i].icon_text);
                draw->AddText(ImVec2(current_x + (sz - txt_sz.x)/2, yy + (sz - txt_sz.y)/2), 0xFFFFFFFF, g_DockApps[i].icon_text);

                // 4. Индикатор запущенного приложения (точка снизу)
                if (IsAppActive(g_DockApps[i].name)) {
                    draw->AddCircleFilled(ImVec2(icon_center_x, (float)dock_y + dock_h - 6), 2.5f, 0xFFFFFFFF);
                }

                // 5. Обработка нажатия
                if (mdown && mx >= current_x && mx <= current_x + sz && my >= yy && my <= yy + sz) {
                    static uint32_t last_click_tick = 0;
                    uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
                    if (now - last_click_tick > 500) { // Anti-spam
                        OpenAppWindow(g_DockApps[i].name);
                        last_click_tick = now;
                        // Звук клика (Sonoma style)
                        play_wav_file("res/sysgui/click.wav");
                    }
                }

                current_x += sz + pad;
            }
        }
        ImGui::End();
    }
}
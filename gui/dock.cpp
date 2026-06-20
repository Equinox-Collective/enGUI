#include "dock.h"
#include "win_manager.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include <equos.h>
#include <math.h>

extern uint32_t screen_w, screen_h;

namespace GUI {
    static const DockItem g_DockItems[] = {
        { "Terminal", "Terminal Console",   0x21252B, ">_", nullptr },
        { "Monitor",  "System Monitor",     0x4B5263, "M",  nullptr },
        { "Paint",    "Vector Paint Brush", 0xFF8700, "P",  nullptr },
        { "Explorer", "VFS File Explorer",  0xE5C07B, "E",  nullptr },
        { "Notepad",  "Notepad Editor",     0x61AFEF, "N",  nullptr },
        { "Doom",     "Doom (Ring 3)",      0xE06C75, "D",  "bin/doom.elf -iwad res/doom1.wad" }
    };
    static const int DOCK_ITEMS_COUNT = sizeof(g_DockItems) / sizeof(DockItem);

    void InitDock() {}

    void RenderDock(int mx, int my, bool mdown) {
        static bool last_mdown = false;
        
        int dock_h = 54;
        int icon_size_base = 40;
        int gap = 12;

        int dock_w = DOCK_ITEMS_COUNT * (icon_size_base + gap) + gap;
        int dock_x = (screen_w - dock_w) / 2;
        int dock_y = screen_h - dock_h - 12;

        // Рисуем красивый размытый док с круглыми углами 14px
        draw_acrylic_blur(dock_x, dock_y, dock_w, dock_h, 0.45f, 14, 0x1E222B);

        ImGui::SetNextWindowPos(ImVec2((float)dock_x, (float)dock_y));
        ImGui::SetNextWindowSize(ImVec2((float)dock_w, (float)dock_h));
        ImGui::Begin("##DockOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            for (int i = 0; i < DOCK_ITEMS_COUNT; i++) {
                const DockItem& item = g_DockItems[i];

                int base_cx = dock_x + gap + i * (icon_size_base + gap) + icon_size_base / 2;
                float dist_x = fabsf((float)mx - (float)base_cx);

                float scale = 1.0f;
                if (my >= dock_y - 15 && my <= (int)screen_h && dist_x < 80.0f) {
                    scale = 1.0f + (1.0f - (dist_x / 80.0f)) * 0.35f; // Интерактивное увеличение иконок
                }

                int size = (int)((float)icon_size_base * scale);
                int ix = base_cx - size / 2;
                int iy = dock_y + (dock_h - size) / 2;

                bool hover = (mx >= ix && mx < ix + size && my >= iy && my < iy + size);

                // Отрисовка скругленного фона иконки дока
                ImU32 col_bg = hover ? ImGui::GetColorU32(ImVec4(0.3f, 0.35f, 0.4f, 0.9f)) 
                                     : ImGui::GetColorU32(ImVec4(0.13f, 0.15f, 0.17f, 0.80f));
                
                draw_list->AddRectFilled(ImVec2((float)ix, (float)iy), ImVec2((float)(ix + size), (float)(iy + size)), col_bg, 8.0f);

                // Иконка-глиф
                ImU32 col_glyph = item.color | 0xFF000000;
                draw_list->AddRectFilled(ImVec2((float)(ix + 4), (float)(iy + 4)), ImVec2((float)(ix + size - 4), (float)(iy + size - 4)), col_glyph, 6.0f);
                
                if (item.text_glyph) {
                    ImGui::SetWindowFontScale(scale);
                    ImVec2 t_size = ImGui::CalcTextSize(item.text_glyph);
                    draw_list->AddText(ImVec2((float)ix + ((float)size - t_size.x)/2.0f, (float)iy + ((float)size - t_size.y)/2.0f), 0xFFFFFFFF, item.text_glyph);
                }

                // Индикатор того, что приложение запущено (маленькая белая точка под иконкой)
                if (IsAppActive(item.win_title)) {
                    draw_list->AddCircleFilled(ImVec2((float)base_cx, (float)(screen_h - 16)), 2.5f, 0xFFFFFFFF);
                }

                // Всплывающая подсказка над иконкой
                if (hover) {
                    ImGui::SetTooltip("%s", item.label);
                    
                    if (mdown && !last_mdown) {
                        if (item.exec_cmd) {
                            sys_exec(item.exec_cmd);
                            OpenAppWindow(item.win_title);
                        } else {
                            OpenAppWindow(item.win_title);
                        }
                    }
                }
            }
        }
        ImGui::End();
        
        last_mdown = mdown;
    }
}
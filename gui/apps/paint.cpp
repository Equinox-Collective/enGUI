#include "paint.h"
#include "../../imgui/imgui.h"

namespace GUI {

    PaintApp::PaintApp(uint32_t id) : App("Vector Paint", id) {
        brush_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        brush_size = 5.0f;
        point_count = 0;
    }

    void PaintApp::OnRender(float dt) {
        ImGui::BeginGroup();
        {
            ImGui::TextColored(ImVec4(1,1,1,0.6f), "PALETTE");
            ImGui::Separator();
            
            char uniq_picker[64];
            sprintf(uniq_picker, "##Picker_%u", instance_id);
            ImGui::ColorEdit4(uniq_picker, (float*)&brush_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            
            ImGui::Spacing();
            ImGui::Text("Size");
            
            char uniq_slider[64];
            sprintf(uniq_slider, "##Slider_%u", instance_id);
            ImGui::SliderFloat(uniq_slider, &brush_size, 1.0f, 25.0f, "%.0f px");
            
            ImGui::Spacing();
            if (ImGui::Button("Clear All", ImVec2(80, 30))) point_count = 0;
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // Отрисовка холста для рисования
        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      
        ImVec2 canvas_sz = ImGui::GetContentRegionAvail();   
        if (canvas_sz.x < 100.0f) canvas_sz.x = 100.0f;
        if (canvas_sz.y < 100.0f) canvas_sz.y = 100.0f;
        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(10, 12, 22, 255)); 
        draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 30), 0.0f, 0, 1.5f);   

        ImGuiIO& io = ImGui::GetIO();
        
        char uniq_btn[64];
        sprintf(uniq_btn, "canvas_area_%u", instance_id);
        ImGui::InvisibleButton(uniq_btn, canvas_sz);

        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 mouse_pos_in_canvas = ImVec2(io.MousePos.x, io.MousePos.y);
            
            if (mouse_pos_in_canvas.x >= canvas_p0.x && mouse_pos_in_canvas.x <= canvas_p1.x &&
                mouse_pos_in_canvas.y >= canvas_p0.y && mouse_pos_in_canvas.y <= canvas_p1.y) {
                
                if (point_count < MAX_POINTS) {
                    points[point_count] = mouse_pos_in_canvas;
                    colors[point_count] = ImGui::ColorConvertFloat4ToU32(brush_color);
                    sizes[point_count] = brush_size;
                    point_count++;
                }
            }
        }

        // Рендеринг всех векторных точек
        for (int n = 0; n < point_count; n++) {
            draw_list->AddCircleFilled(points[n], sizes[n], colors[n]);
        }
    }

}
#include "paint.h"
#include "../../imgui/imgui.h"

namespace GUI {

    PaintApp::PaintApp() : App("Vector Paint") {
        brush_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        brush_size = 4.0f;
        point_count = 0;
    }

    void PaintApp::OnRender(float dt) {
        // Панель инструментов слева
        ImGui::BeginGroup();
        {
            ImGui::Text("Tools");
            ImGui::Separator();
            ImGui::ColorEdit4("Color", (float*)&brush_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            ImGui::SliderFloat("Size", &brush_size, 1.0f, 20.0f, "%.0f");
            if (ImGui::Button("Clear", ImVec2(50, 0))) point_count = 0;
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // Холст
        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // Начало холста
        ImVec2 canvas_sz = ImGui::GetContentRegionAvail();   // Размер доступной области
        if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
        if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(30, 30, 35, 255)); // Фон холста
        draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 50));   // Рамка

        // Логика рисования
        ImGuiIO& io = ImGui::GetIO();
        bool is_hovered = ImGui::IsItemHovered(); // Проверка наведения на невидимую кнопку ниже
        
        // Создаем невидимый интерактивный элемент для захвата мыши
        ImGui::InvisibleButton("canvas", canvas_sz);

        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 mouse_pos_in_canvas = ImVec2(io.MousePos.x, io.MousePos.y);
            
            // Если мы в границах холста
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

        // Отрисовка всех точек (линиями для плавности)
        for (int n = 0; n < point_count; n++) {
            draw_list->AddCircleFilled(points[n], sizes[n], colors[n]);
        }
    }

} // namespace GUI
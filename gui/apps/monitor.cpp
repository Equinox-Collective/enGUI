#include "monitor.h"
#include "../../imgui/imgui.h"

extern "C" {
#include <equos.h>
#include <stdio.h>
}

namespace GUI {

    MonitorApp::MonitorApp() : App("System Monitor") {
        for (int i = 0; i < 60; i++) mem_history[i] = 0.0f;
        timer = 0.0f;
    }

    void MonitorApp::OnRender(float dt) {
        timer += dt;
        
        // Обновляем данные раз в 0.5 сек
        if (timer >= 0.5f) {
            uint64_t used = sys_get_used_mem();
            // Сдвигаем историю
            for (int i = 0; i < 59; i++) mem_history[i] = mem_history[i+1];
            mem_history[59] = (float)(used / (1024 * 1024)); // в мегабайтах
            timer = 0.0f;
        }

        uint64_t total = sys_get_total_mem();
        uint64_t used = sys_get_used_mem();

        ImGui::Text("Hardware Resources:");
        ImGui::Separator();
        
        ImGui::Columns(2, "stats");
        ImGui::Text("CPU Architecture:"); ImGui::NextColumn(); ImGui::Text("x86_64 (Ring 3)"); ImGui::NextColumn();
        ImGui::Text("Kernel Version:");   ImGui::NextColumn(); ImGui::Text("Equinox v1.2"); ImGui::NextColumn();
        ImGui::Text("Total RAM:");        ImGui::NextColumn(); ImGui::Text("%llu MB", total / (1024*1024)); ImGui::NextColumn();
        ImGui::Text("Used RAM:");         ImGui::NextColumn(); ImGui::Text("%llu MB", used / (1024*1024)); ImGui::NextColumn();
        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::Text("Memory Usage History (60s):");
        
        // Рисуем красивый график
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.7f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
        
        char overlay[32];
        sprintf(overlay, "%d MB", (int)mem_history[59]);
        ImGui::PlotLines("##MemGraph", mem_history, 60, 0, overlay, 0.0f, (float)(total/(1024*1024)), ImVec2(0, 80));
        
        ImGui::PopStyleColor(2);

        if (ImGui::Button("Collect Garbage (Trim)")) {
            // Тут мог бы быть syscall для очистки кэшей
        }
        ImGui::SameLine();
        if (ImGui::Button("Export Log")) {
            // Заглушка
        }
    }

} // namespace GUI
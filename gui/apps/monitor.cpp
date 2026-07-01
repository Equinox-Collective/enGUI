// app/sysgui/gui/apps/monitor.cpp
#include "monitor.h"
#include <equos.h>
#include <stdio.h>

namespace GUI {

    MonitorApp::MonitorApp(uint32_t id, int start_x, int start_y) 
        : App("System Monitor", id, start_x, start_y, 420, 340) {
        for (int i = 0; i < 60; i++) mem_history[i] = 0.0f;
        timer = 0.0f;
    }

    void MonitorApp::OnRender(Painter& p, float dt) {
        timer += dt;
        if (timer >= 0.5f) {
            uint64_t used = sys_get_used_mem();
            for (int i = 0; i < 59; i++) mem_history[i] = mem_history[i+1];
            mem_history[59] = (float)(used / (1024 * 1024)); 
            timer = 0.0f;
        }

        uint64_t total = sys_get_total_mem();
        uint64_t used = sys_get_used_mem();

        p.Text("SYSTEM MONITOR STATISTICS", x + 15, y + 45, COLOR_ACCENT_CYAN);
        p.Line(x + 15, y + 65, x + w - 15, y + 65, 0x44FFFFFF);

        char total_str[64], used_str[64];
        sprintf(total_str, "Total physical RAM: %llu MB", total / (1024*1024));
        sprintf(used_str,  "Used physical RAM:  %llu MB", used / (1024*1024));
        p.Text(total_str, x + 20, y + 80, 0xFFFFFFFF);
        p.Text(used_str,  x + 20, y + 100, 0xFFFFFFFF);

        // Отрисовка нативного высококонтрастного графика
        int graph_x = x + 20;
        int graph_y = y + 140;
        int graph_w = w - 40;
        int graph_h = 130;

        p.DrawRect(graph_x, graph_y, graph_w, graph_h, 0x55FFFFFF);
        
        // Рисуем линии сетки графика
        for (int i = 1; i < 4; i++) {
            p.Line(graph_x, graph_y + i * (graph_h / 4), graph_x + graph_w, graph_y + i * (graph_h / 4), 0x22FFFFFF);
        }

        float max_val = (float)(total / (1024*1024));
        if (max_val <= 0) max_val = 1.0f;

        // Рендерим кривую истории выделения памяти
        for (int i = 0; i < 59; i++) {
            float val1 = mem_history[i];
            float val2 = mem_history[i+1];

            int lx1 = graph_x + (int)((float)i * ((float)graph_w / 60.0f));
            int ly1 = graph_y + graph_h - (int)((val1 / max_val) * (float)graph_h);
            int lx2 = graph_x + (int)((float)(i+1) * ((float)graph_w / 60.0f));
            int ly2 = graph_y + graph_h - (int)((val2 / max_val) * (float)graph_h);

            if (ly1 >= graph_y && ly1 <= graph_y + graph_h && ly2 >= graph_y && ly2 <= graph_y + graph_h) {
                p.Line(lx1, ly1, lx2, ly2, COLOR_ACCENT, 2);
            }
        }
    }
}
#ifndef GUI_PAINT_H
#define GUI_PAINT_H

#include "../api_gui.h"
#include "../../imgui/imgui.h"

namespace GUI {
    class PaintApp : public App {
    private:
        static const int MAX_POINTS = 2048;
        ImVec2   points[MAX_POINTS];
        uint32_t colors[MAX_POINTS];
        float    sizes[MAX_POINTS];
        int      point_count;

        ImVec4   brush_color;
        float    brush_size;

    public:
        PaintApp();
        virtual ~PaintApp() {}

        void OnRender(float dt) override;
    };
}

#endif
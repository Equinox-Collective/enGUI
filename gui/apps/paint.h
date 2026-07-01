// app/sysgui/gui/apps/paint.h
#ifndef GUI_PAINT_H
#define GUI_PAINT_H

#include "../../api_gui.h"

namespace GUI {
    struct PaintPoint {
        int x, y;
        uint32_t color;
    };

    class PaintApp : public App {
    private:
        static const int MAX_POINTS = 1024;
        PaintPoint points[MAX_POINTS];
        int point_count;
        uint32_t current_color;

    public:
        PaintApp(uint32_t id, int start_x, int start_y);
        virtual ~PaintApp() {}

        void OnRender(Painter& p, float dt) override;
    };
}

#endif
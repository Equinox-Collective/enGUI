// app/sysgui/gui/apps/monitor.h
#ifndef GUI_MONITOR_H
#define GUI_MONITOR_H

#include "../../api_gui.h"

namespace GUI {
    class App;
    class MonitorApp : public App {
    private:
        float mem_history[60];
        float timer;

    public:
        MonitorApp(uint32_t id, int start_x, int start_y);
        virtual ~MonitorApp() {}

        void OnRender(Painter& p, float dt) override;
    };
}

#endif
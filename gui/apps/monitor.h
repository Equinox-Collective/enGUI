#ifndef GUI_MONITOR_H
#define GUI_MONITOR_H

#include "../api_gui.h"

namespace GUI {
    class MonitorApp : public App {
    private:
        float mem_history[60];
        float timer;

    public:
        MonitorApp();
        virtual ~MonitorApp() {}

        void OnRender(float dt) override;
    };
}

#endif
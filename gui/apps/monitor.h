#ifndef GUI_MONITOR_H
#define GUI_MONITOR_H

#include "../api_gui.h"

namespace GUI {
    class MonitorApp : public App {
    private:
        float mem_history[60];
        float timer;

    public:
        MonitorApp(uint32_t id);
        virtual ~MonitorApp() {}

        void OnRender(float dt) override;
    };
}

#endif
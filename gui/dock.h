// app/sysgui/gui/dock.h
#ifndef GUI_DOCK_H
#define GUI_DOCK_H

#include "../api_gui.h"

namespace GUI {
    void InitDock();
    void RenderDock(Painter& p, int mx, int my, bool mdown);
}

#endif
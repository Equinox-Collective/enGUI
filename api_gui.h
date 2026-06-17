#ifndef API_GUI_H
#define API_GUI_H

#include "lua/lua.h"

void register_gui_api(lua_State *L);
void api_preload_boot_sound(void);
void api_try_boot_sound(void);

#endif
#ifndef API_GUI_H
#define API_GUI_H

#include "lua/lua.h"

// Объявляем функцию, чтобы main.c её видел
void register_gui_api(lua_State *L);

// Однократно проигрывает звук запуска ОС, как только готова звуковая карта.
// Вызывается каждый кадр из главного цикла main.c (сама себя гейтит).
void api_try_boot_sound(void);

#endif
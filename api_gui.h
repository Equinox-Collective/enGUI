#ifndef API_GUI_H
#define API_GUI_H

#include "lua/lua.h"

// Объявляем функцию, чтобы main.c её видел
void register_gui_api(lua_State *L);

// Грузит звук запуска ОС в память. Вызвать ОДИН раз ДО главного цикла (пока
// крутится kernel-сплэш), т.к. чтение WAV по ATA-PIO блокирующее и медленное.
void api_preload_boot_sound(void);

// Однократно запускает заранее загруженный звук запуска, как только готова
// звуковая карта. Вызывается каждый кадр из главного цикла main.c (дёшево).
void api_try_boot_sound(void);

#endif
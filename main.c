#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
#include <equos.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

uint32_t *vram;
uint32_t screen_w, screen_h;

int main(int argc, char **argv) {
  // 1. Получаем инфу о VESA от ядра
  uint64_t phys_fb;
  uint32_t pitch;
  // Используем твой сисколл 32
  phys_fb = _syscall(32, 0, 0, 0, 0, 0);
  // Предположим, ядро вернуло ширину/высоту в регистрах rbx, rcx
  // Для примера захардкодим или пробросим через структуру
  screen_w = 1024; // Возьми из возврата сисколла
  screen_h = 768;

  // 2. Мапим видеопамять в наше пространство (Ring 3)
  vram = (uint32_t *)_syscall(30, phys_fb, screen_w * screen_h * 4, 0, 0, 0);

  // 3. Инициализация Lua
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  // Регистрируем наши "ахуенные" функции рисования
  register_gui_api(L);

  // 4. Запускаем главный скрипт
  if (luaL_dofile(L, "/res/sysgui/init.lua")) {
    printf("GUI Error: %s\n", lua_tostring(L, -1));
    return 1;
  }

  // 5. Цикл обработки событий
  while (1) {
    // Вызываем функцию on_tick в Lua
    lua_getglobal(L, "on_tick");
    lua_pcall(L, 0, 0, 0);

    sys_yield();
  }

  lua_close(L);
  return 0;
}
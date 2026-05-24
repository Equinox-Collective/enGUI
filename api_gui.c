#include "api_gui.h"
#include <stdint.h>

// Копия функции рисования строки, но теперь для Ring 3
static int l_draw_text(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  uint32_t color = (uint32_t)luaL_checknumber(L, 4);

  // Вызываем твой отрисовщик (который мы скопируем из ядра в этот файл)
  draw_string_ring3(vram, str, x, y, color);
  return 0;
}

// Эффект блюра прямо в Ring 3! (Пиздим из gui.c)
static int l_apply_blur(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);

  // Твоя функция блюра теперь работает в юзерспейсе
  for (int i = y; i < y + h; i++) {
    for (int j = x; j < x + w; j++) {
      // ... логика блюра ...
    }
  }
  return 0;
}

void register_gui_api(lua_State *L) {
  lua_register(L, "drawText", l_draw_text);
  lua_register(L, "applyBlur", l_apply_blur);
  // Добавь сюда drawRect, drawLine, drawIcon и т.д.
}
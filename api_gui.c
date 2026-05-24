#include "api_gui.h"
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
#include <equos.h>
#include <stdint.h>

// Эти переменные мы объявим в main.c, а здесь просто используем
extern uint32_t *vram;
extern uint32_t screen_w, screen_h;

// Функция очистки экрана: clearScreen(0xHEXCOLOR)
static int l_clear_screen(lua_State *L) {
  uint32_t color = (uint32_t)luaL_checknumber(L, 1);

  if (vram) {
    for (uint32_t i = 0; i < screen_w * screen_h; i++) {
      vram[i] = color;
    }
  }
  return 0;
}

static int l_draw_text(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  int x = (int)luaL_checkinteger(L, 2);
  int y = (int)luaL_checkinteger(L, 3);
  uint32_t color = (uint32_t)luaL_checknumber(L, 4);

  // TODO: Здесь будет вызов отрисовки текста в vram
  return 0;
}

static int l_apply_blur(lua_State *L) {
  int x = (int)luaL_checkinteger(L, 1);
  int y = (int)luaL_checkinteger(L, 2);
  int w = (int)luaL_checkinteger(L, 3);
  int h = (int)luaL_checkinteger(L, 4);

  // TODO: Здесь будет логика блюра в vram
  return 0;
}

void register_gui_api(lua_State *L) {
  lua_register(L, "clearScreen", l_clear_screen);
  lua_register(L, "drawText", l_draw_text);
  lua_register(L, "applyBlur", l_apply_blur);
}
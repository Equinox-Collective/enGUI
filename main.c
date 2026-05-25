#include "api_gui.h"
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
#include <equos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <eid.h>
#include <eid_ext.h>

uint32_t *vram = NULL;
uint32_t screen_w = 1024;
uint32_t screen_h = 768;

eid_ctx_t eid_ctx;

// Объявляем функцию проверки активности анимаций из api_gui.c
extern bool is_any_anim_active(void);

int main(int argc, char **argv) {
  uint64_t phys_fb = 0;
  uint64_t width = 0;
  uint64_t height = 0;
  uint64_t pitch = 0;

  // Забираем параметры VESA фреймбуфера
  __asm__ volatile("mov $32, %%rax\n"
                   "int $0x80\n"
                   : "=a"(phys_fb), "=b"(width), "=c"(height), "=d"(pitch));

  screen_w = (uint32_t)width;
  screen_h = (uint32_t)height;

  vram = (uint32_t *)_syscall(SYS_MAP_PHYS, phys_fb, screen_w * screen_h * 4, 0,
                              0, 0);

  eid_init();
  memset(&eid_ctx, 0, sizeof(eid_ctx));

  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  register_gui_api(L);

  if (luaL_dofile(L, "res/sysgui/init.lua")) {
    printf("enGUI Lua Error: %s\n", lua_tostring(L, -1));
    return 1;
  }

  const uint32_t TICK_MS = 10;
  int last_mx = -9999, last_my = -9999;
  int last_mdown = -1;
  uint8_t last_key = 0;
  uint32_t force_frames = 4;

  uint32_t last_tick = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);

  while (1) {
    uint64_t mx = 0, my = 0, m_btn = 0;
    __asm__ volatile("mov $7, %%rax\n int $0x80"
                     : "=a"(mx), "=b"(my), "=c"(m_btn));
    int cur_mx = (int)mx;
    int cur_my = (int)my;
    int cur_mdown = (int)((m_btn & 1) != 0);

    uint8_t cur_key = (uint8_t)_syscall(SYS_GET_SCANCODE, 0, 0, 0, 0, 0);

    // Добавляем проверку активности анимаций в условие перерисовки экрана
    int need_redraw = (force_frames > 0) || (cur_mx != last_mx) ||
                      (cur_my != last_my) || (cur_mdown != last_mdown) ||
                      (cur_key != 0 && cur_key != last_key) ||
                      is_any_anim_active();

    uint32_t now = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);

    if (need_redraw) {
      uint32_t elapsed = now - last_tick;
      float dt = (float)(elapsed * TICK_MS);
      if (dt > 200.0f)
        dt = 200.0f; // Ограничитель прыжков во времени

      eid_begin(&eid_ctx, vram, screen_w, screen_h);
      eid_ctx.mx = cur_mx;
      eid_ctx.my = cur_my;
      eid_ctx.m_down = cur_mdown;
      eid_ctx.last_key = cur_key;

      lua_getglobal(L, "on_tick");
      if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, dt);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
          printf("Lua Tick Error: %s\n", lua_tostring(L, -1));
          break;
        }
      } else {
        lua_pop(L, 1);
      }

      eid_end(&eid_ctx, 0, 0);

      last_mx = cur_mx;
      last_my = cur_my;
      last_mdown = cur_mdown;
      last_key = cur_key;
      if (force_frames > 0)
        force_frames--;
    }

    // Всегда обновляем tick, чтобы предотвратить временные скачки после простоя
    last_tick = now;

    sys_sleep(16);
  }

  lua_close(L);
  return 0;
}
#include "api_gui.h"
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
#include <eid.h>
#include <eid_ext.h>
#include <equos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

uint32_t *vram = NULL;
uint32_t *backbuffer = NULL;
uint32_t *draw_target = NULL; // Наша цель для всех функций отрисовки в Lua API
uint32_t screen_w = 1024;
uint32_t screen_h = 768;

eid_ctx_t eid_ctx;

extern bool is_any_anim_active(void);

// Отрисовка курсора в пользовательском пространстве поверх бэкбуфера
void draw_cursor_user(uint32_t *fb, int x, int y, int w, int h) {
  static const int cursor_map[8][8] = {
      {2, 0, 0, 0, 0, 0, 0, 0}, {2, 2, 0, 0, 0, 0, 0, 0},
      {2, 1, 2, 0, 0, 0, 0, 0}, {2, 1, 1, 2, 0, 0, 0, 0},
      {2, 1, 1, 1, 2, 0, 0, 0}, {2, 1, 1, 1, 1, 2, 0, 0},
      {2, 2, 2, 2, 2, 2, 2, 0}, {0, 0, 2, 2, 2, 0, 0, 0}};
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      int px = x + j;
      int py = y + i;
      if (px >= 0 && px < w && py >= 0 && py < h) {
        if (cursor_map[i][j] == 1)
          fb[py * w + px] = 0xFFFFFF;
        else if (cursor_map[i][j] == 2)
          fb[py * w + px] = 0x000000;
      }
    }
  }
}

int main(int argc, char **argv) {
  uint64_t phys_fb = 0;
  uint64_t width = 0;
  uint64_t height = 0;
  uint64_t pitch = 0;

  // Опрос физического фреймбуфера
  __asm__ volatile("mov $32, %%rax\n"
                   "int $0x80\n"
                   : "=a"(phys_fb), "=b"(width), "=c"(height), "=d"(pitch));

  screen_w = (uint32_t)width;
  screen_h = (uint32_t)height;

  // Мапим физическую видеопамять (фронтбуфер)
  vram = (uint32_t *)_syscall(SYS_MAP_PHYS, phys_fb, screen_w * screen_h * 4, 0,
                              0, 0);

  // Выделяем локальный бэкбуфер для двойной буферизации
  backbuffer = (uint32_t *)malloc(screen_w * screen_h * 4);
  memset(backbuffer, 0, screen_w * screen_h * 4);

  // Присваиваем бэкбуфер как цель для Lua-отрисовки
  draw_target = backbuffer;

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

    int need_redraw = (force_frames > 0) || (cur_mx != last_mx) ||
                      (cur_my != last_my) || (cur_mdown != last_mdown) ||
                      (cur_key != 0 && cur_key != last_key) ||
                      is_any_anim_active();

    uint32_t now = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);

    if (need_redraw) {
      uint32_t elapsed = now - last_tick;
      float dt = (float)(elapsed * TICK_MS);
      if (dt > 200.0f)
        dt = 200.0f;

      // Отрисовываем всё в бэкбуфер
      eid_begin(&eid_ctx, backbuffer, screen_w, screen_h);
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

      // Накладываем курсор поверх бэкбуфера прямо перед отправкой кадра
      draw_cursor_user(backbuffer, cur_mx, cur_my, screen_w, screen_h);

      // Копируем готовый бэкбуфер во фронтбуфер
      memcpy(vram, backbuffer, screen_w * screen_h * 4);

      last_mx = cur_mx;
      last_my = cur_my;
      last_mdown = cur_mdown;
      last_key = cur_key;
      if (force_frames > 0)
        force_frames--;
    }

    last_tick = now;
    sys_sleep(16);
  }

  lua_close(L);
  free(backbuffer);
  return 0;
}
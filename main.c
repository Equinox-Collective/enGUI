#include "api_gui.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include <equos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


// Используем только то, что реально есть в SDK
#include <eid.h>
#include <eid_ext.h>

uint32_t *vram = NULL;
uint32_t screen_w = 1024;
uint32_t screen_h = 768;

// Глобальный контекст для виджетов EID
eid_ctx_t eid_ctx;

int main(int argc, char **argv) {
  // 1. Получаем инфу о VESA от ядра
  uint64_t phys_fb = _syscall(SYS_GET_VESA_INFO, 0, 0, 0, 0, 0);

  // 2. Мапим видеопамять в юзерспейс
  vram = (uint32_t *)_syscall(SYS_MAP_PHYS, phys_fb, screen_w * screen_h * 4, 0,
                              0, 0);

  // 3. Инициализация EID
  eid_init();
  memset(&eid_ctx, 0, sizeof(eid_ctx));

  // 4. Инициализация Lua
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  register_gui_api(L);

  // 5. Запускаем главный скрипт
  if (luaL_dofile(L, "res/sysgui/init.lua")) {
    printf("enGUI Lua Error: %s\n", lua_tostring(L, -1));
    return 1;
  }

  uint32_t last_tick = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);

  while (1) {
    uint32_t now = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
    float dt = (float)(now - last_tick);
    if (dt < 0.0f)
      dt = 0.0f;
    last_tick = now;

    // Начинаем кадр Immediate Mode
    eid_begin(&eid_ctx, vram, screen_w, screen_h);

    // Получаем координаты мыши и транслируем в контекст EID
    uint64_t mx = 0, my = 0, m_btn = 0;
    __asm__ volatile("mov $7, %%rax\n int $0x80"
                     : "=a"(mx), "=b"(my), "=c"(m_btn));

    eid_ctx.mx = (int)mx;
    eid_ctx.my = (int)my;
    eid_ctx.m_down = (m_btn & 1) != 0;

    // Передаем дельту времени (dt) и обновляем кадр в Lua
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

    // Завершаем кадр
    eid_end(&eid_ctx, 0, 0);
    sys_sleep(16); // ~60 FPS
  }

  lua_close(L);
  return 0;
}
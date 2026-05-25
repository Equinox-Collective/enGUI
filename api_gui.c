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

extern uint32_t *vram;
extern uint32_t screen_w, screen_h;
extern eid_ctx_t eid_ctx;

// Простой менеджер анимаций на Си
#define MAX_ANIMS 32
static eid_anim_t anims[MAX_ANIMS];
static int anim_count = 0;

bool is_any_anim_active(void) {
  for (int i = 0; i < anim_count; i++) {
    if (anims[i].active) {
      return true;
    }
  }
  return false;
}

static int l_anim_create(lua_State *L) {
  float duration = (float)luaL_checknumber(L, 1);
  int ease = luaL_checkinteger(L, 2);

  if (anim_count >= MAX_ANIMS) {
    lua_pushinteger(L, -1);
    return 1;
  }

  int id = anim_count++;
  eid_anim_init(&anims[id], duration, (eid_ease_t)ease);
  lua_pushinteger(L, id);
  return 1;
}

static int l_anim_to(lua_State *L) {
  int id = luaL_checkinteger(L, 1);
  float target = (float)luaL_checknumber(L, 2);
  if (id >= 0 && id < anim_count) {
    eid_anim_to(&anims[id], target);
  }
  return 0;
}

static int l_anim_step(lua_State *L) {
  int id = luaL_checkinteger(L, 1);
  float dt = (float)luaL_checknumber(L, 2);
  if (id >= 0 && id < anim_count) {
    eid_anim_step(&anims[id], dt);
  }
  return 0;
}

static int l_anim_eval(lua_State *L) {
  int id = luaL_checkinteger(L, 1);
  if (id >= 0 && id < anim_count) {
    lua_pushnumber(L, eid_anim_eval(&anims[id]));
  } else {
    lua_pushnumber(L, 0.0f);
  }
  return 1;
}

static int l_draw_text(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  uint32_t color = (uint32_t)luaL_checknumber(L, 4);

  eid_draw_text(vram, screen_w, screen_h, x, y, str, color);
  return 0;
}

static int l_draw_rect(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  uint32_t color = (uint32_t)luaL_checknumber(L, 5);

  eid_draw_rect(vram, screen_w, screen_h, x, y, w, h, color);
  return 0;
}

static int l_draw_gradient(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  uint32_t c1 = (uint32_t)luaL_checknumber(L, 5);
  uint32_t c2 = (uint32_t)luaL_checknumber(L, 6);
  bool vertical = lua_toboolean(L, 7);

  eid_draw_gradient_rect(vram, screen_w, screen_h, x, y, w, h, c1, c2,
                         vertical);
  return 0;
}

static int l_button(lua_State *L) {
  const char *label = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  int w = luaL_checkinteger(L, 4);
  int h = luaL_checkinteger(L, 5);

  uint32_t state = eid_button(&eid_ctx, label, x, y, w, h);
  lua_pushboolean(L, (state & EID_STATE_CLICKED) != 0);
  return 1;
}

static int l_checkbox(lua_State *L) {
  const char *label = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  bool val = lua_toboolean(L, 4);

  eid_checkbox(&eid_ctx, label, x, y, &val);
  lua_pushboolean(L, val);
  return 1;
}

static int l_slider(lua_State *L) {
  const char *label = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  int w = luaL_checkinteger(L, 4);
  float val = (float)luaL_checknumber(L, 5);
  float min = (float)luaL_checknumber(L, 6);
  float max = (float)luaL_checknumber(L, 7);

  eid_slider(&eid_ctx, label, x, y, w, &val, min, max);
  lua_pushnumber(L, val);
  return 1;
}

static int l_text_input(lua_State *L) {
  const char *label = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  int w = luaL_checkinteger(L, 4);
  int h = luaL_checkinteger(L, 5);
  const char *curr_val = luaL_checkstring(L, 6);
  int max_len = luaL_checkinteger(L, 7);

  char buf[256];
  strncpy(buf, curr_val, sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  // Вызываем виджет
  uint32_t state =
      eid_text_input(&eid_ctx, label, x, y, w, h, buf,
                     (max_len < sizeof(buf)) ? max_len : sizeof(buf));

  // Возвращаем измененную строку и флаг фокуса
  lua_pushstring(L, buf);
  lua_pushboolean(L, (state & EID_STATE_FOCUSED) != 0);
  return 2;
}

static int l_exec(lua_State *L) {
  const char *cmd = luaL_checkstring(L, 1);
  int ret = sys_exec(cmd);
  lua_pushinteger(L, ret);
  return 1;
}

// Получить системное время (uptime в секундах)
static int l_get_uptime(lua_State *L) {
  uint32_t ms = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
  lua_pushnumber(L, (double)ms / 1000.0);
  return 1;
}

// Получить системную память ядра (used, total)
static int l_get_mem_info(lua_State *L) {
  uint64_t used = sys_get_used_mem();
  uint64_t total = sys_get_total_mem();
  lua_pushnumber(L, (double)used);
  lua_pushnumber(L, (double)total);
  return 2;
}

void register_gui_api(lua_State *L) {
  lua_register(L, "drawText", l_draw_text);
  lua_register(L, "drawRect", l_draw_rect);
  lua_register(L, "drawGradient", l_draw_gradient);

  lua_register(L, "animCreate", l_anim_create);
  lua_register(L, "animTo", l_anim_to);
  lua_register(L, "animStep", l_anim_step);
  lua_register(L, "animEval", l_anim_eval);
  lua_register(L, "textInput", l_text_input);
  lua_register(L, "button", l_button);
  lua_register(L, "checkbox", l_checkbox);
  lua_register(L, "slider", l_slider);
  lua_register(L, "exec", l_exec);
  lua_register(L, "getUptime", l_get_uptime);
  lua_register(L, "getMemInfo", l_get_mem_info);
}
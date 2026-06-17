#include "api_gui.h"
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
#include <eid.h>
#include <eid_ext.h>
#include <equos.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t *draw_target;
extern uint32_t screen_w, screen_h;
extern eid_ctx_t eid_ctx;
extern int k_app_win_x, k_app_win_y, k_app_win_w, k_app_win_h;
extern bool k_app_win_active;

extern void sysgui_mark_dirty(int x, int y, int w, int h);

#define MAX_ANIMS 32
static eid_anim_t anims[MAX_ANIMS];
static int anim_count = 0;

// --- СТАТИЧЕСКИЕ БУФЕРЫ КЭША ДЛЯ БЛЮРА (Исключают фрагментацию кучи) ---
static uint32_t *scratch_buf_1 = NULL;
static uint32_t *scratch_buf_2 = NULL;
static int scratch_allocated_w = 0;
static int scratch_allocated_h = 0;

static void ensure_scratch_buffers(int w, int h) {
    if (w <= scratch_allocated_w && h <= scratch_allocated_h && scratch_buf_1 && scratch_buf_2) {
        return;
    }
    if (scratch_buf_1) free(scratch_buf_1);
    if (scratch_buf_2) free(scratch_buf_2);
    
    scratch_buf_1 = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    scratch_buf_2 = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    scratch_allocated_w = w;
    scratch_allocated_h = h;
}

#pragma pack(push, 1)
typedef struct {
  char id[4];
  uint32_t size;
} ChunkHeader_t;

typedef struct {
  uint16_t fmt;
  uint16_t ch;
  uint32_t rate;
  uint32_t brate;
  uint16_t align;
  uint16_t bps;
} FmtChunk_t;
#pragma pack(pop)

static uint8_t *wav_pcm_data = NULL;
static uint32_t wav_pcm_size = 0;
static uint32_t wav_pcm_pos = 0;
static bool wav_playing = false;

static lua_State *g_lua = NULL;

void api_tick_audio(void) {
  if (!wav_playing || !wav_pcm_data) return;

  uint32_t chunk_size = 8192;
  if (wav_pcm_pos + chunk_size > wav_pcm_size) {
    chunk_size = wav_pcm_size - wav_pcm_pos;
  }

  if (chunk_size > 0) {
    _syscall(20, (uint64_t)(wav_pcm_data + wav_pcm_pos), (uint64_t)chunk_size, 0, 0, 0);
    wav_pcm_pos += chunk_size;
  } else {
    _syscall(20, 0, 0, 0, 0, 0);
    wav_playing = false;
    wav_pcm_data = NULL;
    wav_pcm_size = 0;
    wav_pcm_pos = 0;
  }
}

static bool parse_wav(uint8_t *file_data, uint32_t size,
                      uint8_t **out_pcm, uint32_t *out_len, uint32_t *out_rate) {
  if (size < 44) return false;
  if (memcmp(file_data, "RIFF", 4) != 0 || memcmp(file_data + 8, "WAVE", 4) != 0) return false;

  FmtChunk_t fmt;
  uint8_t *audio_ptr = NULL;
  uint32_t audio_len = 0;
  uint32_t offset = 12;
  bool found_fmt = false;

  while (offset < size - 8) {
    ChunkHeader_t *ch = (ChunkHeader_t *)(file_data + offset);
    if (memcmp(ch->id, "fmt ", 4) == 0) {
      memcpy(&fmt, file_data + offset + 8, 16);
      found_fmt = true;
    } else if (memcmp(ch->id, "data", 4) == 0) {
      audio_len = ch->size;
      audio_ptr = file_data + offset + 8;
      break;
    }
    uint32_t next_offset = offset + 8 + ch->size;
    if (next_offset <= offset || next_offset >= size) break;
    offset = next_offset;
  }

  if (audio_ptr && audio_len > 0 && found_fmt) {
    *out_pcm = audio_ptr;
    *out_len = audio_len;
    *out_rate = fmt.rate;
    return true;
  }
  return false;
}

static void start_playback(uint8_t *pcm, uint32_t len, uint32_t rate) {
  _syscall(21, rate, 0, 0, 0, 0);
  wav_pcm_data = pcm;
  wav_pcm_size = len;
  wav_pcm_pos  = 0;
  wav_playing  = true;
}

static bool play_wav_file(const char *filename) {
  uint32_t size = 0;
  uint64_t addr = _syscall(2, (uint64_t)filename, (uint64_t)&size, 0, 0, 0);
  if (!addr) return false;
  uint8_t *pcm; uint32_t len, rate;
  if (!parse_wav((uint8_t *)addr, size, &pcm, &len, &rate)) return false;
  start_playback(pcm, len, rate);
  return true;
}

static int l_play_sound(lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  lua_pushboolean(L, play_wav_file(filename));
  return 1;
}

#ifndef BOOT_SOUND_ENABLED
#define BOOT_SOUND_ENABLED 1
#endif
#define BOOT_SOUND_PATH "res/sysgui/BOOTSOUND.wav"

static int audio_is_ready(void) { return (int)_syscall(22, 0, 0, 0, 0, 0); }

static bool boot_sound_cfg_enabled(void) {
  if (!g_lua) return true;
  lua_getglobal(g_lua, "BOOT_SOUND_ENABLED");
  bool enabled = true;
  if (lua_isboolean(g_lua, -1)) enabled = lua_toboolean(g_lua, -1);
  lua_pop(g_lua, 1);
  return enabled;
}

static uint8_t *boot_pcm = NULL;
static uint32_t boot_len = 0, boot_rate = 0;
static bool     boot_loaded = false;

void api_preload_boot_sound(void) {
#if BOOT_SOUND_ENABLED
  if (!boot_sound_cfg_enabled()) return;
  uint32_t size = 0;
  uint64_t addr = _syscall(2, (uint64_t)BOOT_SOUND_PATH, (uint64_t)&size, 0, 0, 0);
  if (!addr) return;
  if (parse_wav((uint8_t *)addr, size, &boot_pcm, &boot_len, &boot_rate)) {
    boot_loaded = true;
  }
#endif
}

void api_try_boot_sound(void) {
#if BOOT_SOUND_ENABLED
  static bool s_done = false;
  if (s_done || !boot_loaded) return;
  if (wav_playing) return;
  if (!audio_is_ready()) return;
  start_playback(boot_pcm, boot_len, boot_rate);
  s_done = true;
#endif
}

bool is_any_anim_active(void) {
  for (int i = 0; i < anim_count; i++) {
    if (anims[i].active) return true;
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
  if (id >= 0 && id < anim_count) eid_anim_to(&anims[id], target);
  return 0;
}

static int l_anim_step(lua_State *L) {
  int id = luaL_checkinteger(L, 1);
  float dt = (float)luaL_checknumber(L, 2);
  if (id >= 0 && id < anim_count) eid_anim_step(&anims[id], dt);
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

  eid_draw_text(draw_target, screen_w, screen_h, x, y, str, color);
  sysgui_mark_dirty(x, y, strlen(str) * 8, 16);
  return 0;
}

static int l_draw_rect(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  uint32_t color = (uint32_t)luaL_checknumber(L, 5);

  eid_draw_rect(draw_target, screen_w, screen_h, x, y, w, h, color);
  sysgui_mark_dirty(x, y, w, h);
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

  eid_draw_gradient_rect(draw_target, screen_w, screen_h, x, y, w, h, c1, c2, vertical);
  sysgui_mark_dirty(x, y, w, h);
  return 0;
}

static int l_draw_line(lua_State *L) {
  int x1 = luaL_checkinteger(L, 1);
  int y1 = luaL_checkinteger(L, 2);
  int x2 = luaL_checkinteger(L, 3);
  int y2 = luaL_checkinteger(L, 4);
  uint32_t color = (uint32_t)luaL_checknumber(L, 5);

  eid_draw_line(draw_target, screen_w, screen_h, x1, y1, x2, y2, color);
  int min_x = x1 < x2 ? x1 : x2;
  int min_y = y1 < y2 ? y1 : y2;
  int w = (x1 > x2 ? x1 : x2) - min_x + 1;
  int h = (y1 > y2 ? y1 : y2) - min_y + 1;
  sysgui_mark_dirty(min_x, min_y, w, h);
  return 0;
}

static int l_draw_circle(lua_State *L) {
  int cx = luaL_checkinteger(L, 1);
  int cy = luaL_checkinteger(L, 2);
  int r = luaL_checkinteger(L, 3);
  uint32_t color = (uint32_t)luaL_checknumber(L, 4);
  bool fill = lua_toboolean(L, 5);

  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      int dist2 = x * x + y * y;
      if (fill) {
        if (dist2 <= r * r) {
          eid_draw_pixel(draw_target, screen_w, screen_h, cx + x, cy + y, color);
        }
      } else {
        if (dist2 <= r * r && dist2 > (r - 2) * (r - 2)) {
          eid_draw_pixel(draw_target, screen_w, screen_h, cx + x, cy + y, color);
        }
      }
    }
  }
  sysgui_mark_dirty(cx - r, cy - r, r * 2 + 1, r * 2 + 1);
  return 0;
}

static int l_button(lua_State *L) {
  const char *label = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  int w = luaL_checkinteger(L, 4);
  int h = luaL_checkinteger(L, 5);

  uint32_t state = eid_button(&eid_ctx, label, x, y, w, h);
  sysgui_mark_dirty(x - 2, y - 2, w + 4, h + 6);
  lua_pushboolean(L, (state & EID_STATE_CLICKED) != 0);
  return 1;
}

static int l_checkbox(lua_State *L) {
  const char *label = luaL_checkstring(L, 1);
  int x = luaL_checkinteger(L, 2);
  int y = luaL_checkinteger(L, 3);
  bool val = lua_toboolean(L, 4);

  eid_checkbox(&eid_ctx, label, x, y, &val);
  sysgui_mark_dirty(x, y, 18 + 8 + strlen(label) * 8, 20);
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
  sysgui_mark_dirty(x, y - 4, w, 26);
  lua_pushnumber(L, val);
  return 1;
}

static int l_exec(lua_State *L) {
  const char *cmd = luaL_checkstring(L, 1);
  lua_pushinteger(L, sys_exec(cmd));
  return 1;
}

static int l_get_uptime(lua_State *L) {
  uint32_t ms = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
  lua_pushnumber(L, (double)ms / 1000.0);
  return 1;
}

static int l_get_mem_info(lua_State *L) {
  lua_pushnumber(L, (double)sys_get_used_mem());
  lua_pushnumber(L, (double)sys_get_total_mem());
  return 2;
}

static int l_get_mouse(lua_State *L) {
  lua_pushinteger(L, eid_ctx.mx);
  lua_pushinteger(L, eid_ctx.my);
  lua_pushboolean(L, eid_ctx.m_down);
  return 3;
}

static int l_get_last_key(lua_State *L) {
  lua_pushinteger(L, eid_ctx.last_key);
  eid_ctx.last_key = 0;
  return 1;
}

static int l_scancode_to_ascii(lua_State *L) {
  int sc = luaL_checkinteger(L, 1);
  bool shift = lua_toboolean(L, 2);
  char c = eid_scancode_to_ascii((uint8_t)sc, shift);
  char str[2] = {c, 0};
  lua_pushstring(L, str);
  return 1;
}

static int l_read_file(lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  uint32_t size = 0;
  uint64_t addr = _syscall(2, (uint64_t)filename, (uint64_t)&size, 0, 0, 0);
  if (addr && size > 0) {
    lua_pushlstring(L, (const char *)addr, size);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int l_save_file(lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  size_t len = 0;
  const char *data = luaL_checklstring(L, 2, &len);
  write_file(filename, (void *)data, (int)len);
  return 0;
}

static int l_get_files(lua_State *L) {
  lua_newtable(L);
  int idx = 1;
  struct { char name[128]; uint32_t size; char dev[32]; } entry;

  for (int i = 0;; i++) {
    uint64_t ret = _syscall(4, i, (uint64_t)&entry, 0, 0, 0);
    if (!ret) break;

    lua_newtable(L);
    lua_pushstring(L, "name"); lua_pushstring(L, entry.name); lua_settable(L, -3);
    lua_pushstring(L, "size"); lua_pushinteger(L, entry.size); lua_settable(L, -3);
    lua_pushstring(L, "dev");  lua_pushstring(L, entry.dev);  lua_settable(L, -3);
    lua_rawseti(L, -2, idx++);
  }
  return 1;
}

static int l_get_screen_size(lua_State *L) {
  lua_pushinteger(L, screen_w);
  lua_pushinteger(L, screen_h);
  return 2;
}

static int l_get_tasks(lua_State *L) {
  lua_newtable(L);
  int out_idx = 1;
  sys_task_info_t info;
  for (int i = 0; i < 256; i++) {
    uint64_t ok = _syscall(70, (uint64_t)i, (uint64_t)&info, 0, 0, 0);
    if (!ok) break;
    lua_newtable(L);
    lua_pushstring(L, "pid");   lua_pushinteger(L, (lua_Integer)info.pid); lua_settable(L, -3);
    lua_pushstring(L, "state"); lua_pushstring(L, info.running ? "RUNNING" : "STOPPED"); lua_settable(L, -3);
    lua_pushstring(L, "cr3");   lua_pushinteger(L, (lua_Integer)info.cr3); lua_settable(L, -3);
    lua_pushstring(L, "brk");   lua_pushinteger(L, (lua_Integer)info.brk); lua_settable(L, -3);
    lua_rawseti(L, -2, out_idx++);
  }
  return 1;
}

static int l_kill_task(lua_State *L) {
  lua_Integer pid = luaL_checkinteger(L, 1);
  lua_pushboolean(L, _syscall(71, (uint64_t)pid, 0, 0, 0, 0) ? 1 : 0);
  return 1;
}

static int l_kill_all_tasks(lua_State *L) {
  lua_pushinteger(L, (lua_Integer)_syscall(72, 0, 0, 0, 0, 0));
  return 1;
}

static int l_shell_exec(lua_State *L) {
  const char *line = luaL_checkstring(L, 1);
  static char outbuf[2048];
  outbuf[0] = '\0';
  uint64_t n = _syscall(73, (uint64_t)line, (uint64_t)outbuf, (uint64_t)sizeof(outbuf), 0, 0);
  if (n >= sizeof(outbuf)) n = sizeof(outbuf) - 1;
  outbuf[n] = '\0';
  lua_pushlstring(L, outbuf, (size_t)n);
  return 1;
}

static int l_set_app_window_pos(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);

  k_app_win_x = x; k_app_win_y = y;
  k_app_win_w = w; k_app_win_h = h;
  k_app_win_active = (w > 0 && h > 0);
  _syscall(36, (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h, 0);
  return 0;
}

// --- ВСПОМОГАТЕЛЬНЫЕ КЛИППИНГ-ФУНКЦИИ КРУГЛЫХ СТЕКЛЯННЫХ УГЛОВ ---
static inline bool is_pixel_outside_corners(int tx, int ty, int w, int h, int r) {
    if (r <= 0) return false;
    if (tx < r && ty < r) {
        int dx = r - tx, dy = r - ty;
        return (dx * dx + dy * dy > r * r);
    }
    if (tx >= w - r && ty < r) {
        int dx = tx - (w - r - 1), dy = r - ty;
        return (dx * dx + dy * dy > r * r);
    }
    if (tx < r && ty >= h - r) {
        int dx = r - tx, dy = ty - (h - r - 1);
        return (dx * dx + dy * dy > r * r);
    }
    if (tx >= w - r && ty >= h - r) {
        int dx = tx - (w - r - 1), dy = ty - (h - r - 1);
        return (dx * dx + dy * dy > r * r);
    }
    return false;
}

static inline bool is_pixel_on_border(int tx, int ty, int w, int h, int r, int border_width) {
    if ((tx >= 0 && tx < border_width) || (tx >= w - border_width && tx < w)) {
        if (ty >= r && ty < h - r) return true;
    }
    if ((ty >= 0 && ty < border_width) || (ty >= h - border_width && ty < h)) {
        if (tx >= r && tx < w - r) return true;
    }
    if (tx < r && ty < r) {
        int dx = r - tx, dy = r - ty, d = dx * dx + dy * dy;
        return (d <= r * r && d > (r - border_width) * (r - border_width));
    }
    if (tx >= w - r && ty < r) {
        int dx = tx - (w - r - 1), dy = r - ty, d = dx * dx + dy * dy;
        return (d <= r * r && d > (r - border_width) * (r - border_width));
    }
    if (tx < r && ty >= h - r) {
        int dx = r - tx, dy = ty - (h - r - 1), d = dx * dx + dy * dy;
        return (d <= r * r && d > (r - border_width) * (r - border_width));
    }
    if (tx >= w - r && ty >= h - r) {
        int dx = tx - (w - r - 1), dy = ty - (h - r - 1), d = dx * dx + dy * dy;
        return (d <= r * r && d > (r - border_width) * (r - border_width));
    }
    return false;
}

// --- ГЛАВНАЯ ФУНКЦИЯ ACRYLIC GLASS BLUR (SSE-даунсэмплинг и двухпроходное размытие) ---
static int l_draw_blur(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    float amount = (float)luaL_checknumber(L, 5); // Коэффициент прозрачности стекла
    int radius = (lua_gettop(L) >= 6) ? luaL_checkinteger(L, 6) : 12; // Радиус скругления
    uint32_t tint_rgb = (lua_gettop(L) >= 7) ? (uint32_t)luaL_checknumber(L, 7) : 0x1F222B; // Тинт стекла

    if (w <= 16 || h <= 16) return 0;

    int dsW = w / 4;
    int dsH = h / 4;
    ensure_scratch_buffers(dsW, dsH);

    // 1. Быстрый даунсэмплинг (захват фона под окном)
    for (int dy = 0; dy < dsH; dy++) {
        int src_y = y + (dy * 4);
        if (src_y >= (int)screen_h) src_y = screen_h - 1;
        uint32_t *src_row = &draw_target[src_y * screen_w];
        uint32_t *dst_row = &scratch_buf_1[dy * dsW];
        for (int dx = 0; dx < dsW; dx++) {
            int src_x = x + (dx * 4);
            if (src_x >= (int)screen_w) src_x = screen_w - 1;
            dst_row[dx] = src_row[src_x];
        }
    }

    // 2. Двухпроходное размытие боксом (Box Blur r=2 на уменьшенной картинке дает глубокий фокус)
    int r_blur = 2;
    // Горизонтальный проход
    for (int dy = 0; dy < dsH; dy++) {
        uint32_t *row_src = &scratch_buf_1[dy * dsW];
        uint32_t *row_dst = &scratch_buf_2[dy * dsW];
        for (int dx = 0; dx < dsW; dx++) {
            int sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
            for (int k = -r_blur; k <= r_blur; k++) {
                int px = dx + k;
                if (px >= 0 && px < dsW) {
                    uint32_t color = row_src[px];
                    sum_r += (color >> 16) & 0xFF;
                    sum_g += (color >> 8) & 0xFF;
                    sum_b += color & 0xFF;
                    count++;
                }
            }
            row_dst[dx] = ((sum_r / count) << 16) | ((sum_g / count) << 8) | (sum_b / count);
        }
    }
    // Вертикальный проход
    for (int dx = 0; dx < dsW; dx++) {
        for (int dy = 0; dy < dsH; dy++) {
            int sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
            for (int k = -r_blur; k <= r_blur; k++) {
                int py = dy + k;
                if (py >= 0 && py < dsH) {
                    uint32_t color = scratch_buf_2[py * dsW + dx];
                    sum_r += (color >> 16) & 0xFF;
                    sum_g += (color >> 8) & 0xFF;
                    sum_b += color & 0xFF;
                    count++;
                }
            }
            scratch_buf_1[dy * dsW + dx] = ((sum_r / count) << 16) | ((sum_g / count) << 8) | (sum_b / count);
        }
    }

    // 3. Билинейный апсэмплинг с маской скругления углов и наложением неоновой стеклянной фаски
    uint32_t border_color = (tint_rgb == 0x1F222B) ? 0x4A505C : 0x61AFEF; // Голубая фаска для активного, серая для неактивного

    for (int ty = 0; ty < h; ty++) {
        int dst_y = y + ty;
        if (dst_y < 0 || dst_y >= (int)screen_h) continue;

        float fy = (float)ty / 4.0f;
        int y0 = (int)fy;
        int y1 = (y0 + 1 < dsH) ? y0 + 1 : dsH - 1;
        float wy = fy - (float)y0;

        uint32_t *dst_row = &draw_target[dst_y * screen_w];

        for (int tx = 0; tx < w; tx++) {
            int dst_x = x + tx;
            if (dst_x < 0 || dst_x >= (int)screen_w) continue;

            // Если пиксель за пределами скругленного угла — пропускаем (оставляя нетронутым старый фон)
            if (is_pixel_outside_corners(tx, ty, w, h, radius)) continue;

            // Если пиксель попадает на рамку — рисуем светящуюся стеклянную грань
            if (is_pixel_on_border(tx, ty, w, h, radius, 1)) {
                dst_row[dst_x] = border_color;
                continue;
            }

            // Билинейная интерполяция цвета размытия
            int x0 = (int)((float)tx / 4.0f);
            int x1 = (x0 + 1 < dsW) ? x0 + 1 : dsW - 1;
            float wx = ((float)tx / 4.0f) - (float)x0;

            uint32_t c00 = scratch_buf_1[y0 * dsW + x0];
            uint32_t c10 = scratch_buf_1[y0 * dsW + x1];
            uint32_t c01 = scratch_buf_1[y1 * dsW + x0];
            uint32_t c11 = scratch_buf_1[y1 * dsW + x1];

            float r00 = (c00 >> 16) & 0xFF, g00 = (c00 >> 8) & 0xFF, b00 = c00 & 0xFF;
            float r10 = (c10 >> 16) & 0xFF, g10 = (c10 >> 8) & 0xFF, b10 = c10 & 0xFF;
            float r01 = (c01 >> 16) & 0xFF, g01 = (c01 >> 8) & 0xFF, b01 = c01 & 0xFF;
            float r11 = (c11 >> 16) & 0xFF, g11 = (c11 >> 8) & 0xFF, b11 = c11 & 0xFF;

            float r = (r00 * (1.0f - wx) + r10 * wx) * (1.0f - wy) + (r01 * (1.0f - wx) + r11 * wx) * wy;
            float g = (g00 * (1.0f - wx) + g10 * wx) * (1.0f - wy) + (g01 * (1.0f - wx) + g11 * wx) * wy;
            float b = (b00 * (1.0f - wx) + b10 * wx) * (1.0f - wy) + (b01 * (1.0f - wx) + b11 * wx) * wy;

            // Смешивание размытого фона с тонировкой стекла
            float tint_r = (tint_rgb >> 16) & 0xFF;
            float tint_g = (tint_rgb >> 8) & 0xFF;
            float tint_b = tint_rgb & 0xFF;

            int final_r = (int)(r * (1.0f - amount) + tint_r * amount);
            int final_g = (int)(g * (1.0f - amount) + tint_g * amount);
            int final_b = (int)(b * (1.0f - amount) + tint_b * amount);

            // Создаем Liquid Sparkle (легкий световой градиент сверху вниз для ощущения объема стекла)
            float sparkle = (1.0f - ((float)ty / (float)h)) * 12.0f;
            final_r = (final_r + (int)sparkle > 255) ? 255 : final_r + (int)sparkle;
            final_g = (final_g + (int)sparkle > 255) ? 255 : final_g + (int)sparkle;
            final_b = (final_b + (int)sparkle > 255) ? 255 : final_b + (int)sparkle;

            dst_row[dst_x] = (final_r << 16) | (final_g << 8) | final_b;
        }
    }

    sysgui_mark_dirty(x, y, w, h);
    return 0;
}

static void register_key_constants(lua_State *L) {
  struct { const char *name; int code; } keys[] = {
      {"KEY_UP",     0x148}, {"KEY_DOWN",   0x150},
      {"KEY_LEFT",   0x14B}, {"KEY_RIGHT",  0x14D},
      {"KEY_PGUP",   0x149}, {"KEY_PGDN",   0x151},
      {"KEY_HOME",   0x147}, {"KEY_END",    0x14F},
      {"KEY_INSERT", 0x152}, {"KEY_DELETE", 0x153},
      {"KEY_ENTER",     0x1C}, {"KEY_BACKSPACE", 0x0E},
      {"KEY_TAB",       0x0F}, {"KEY_ESC",       0x01},
      {"KEY_SPACE",     0x39}, {"KEY_LSHIFT",    0x2A},
      {"KEY_RSHIFT",    0x36},
  };
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    lua_pushinteger(L, keys[i].code);
    lua_setglobal(L, keys[i].name);
  }
}

void register_gui_api(lua_State *L) {
  g_lua = L;
  register_key_constants(L);
  lua_register(L, "drawText", l_draw_text);
  lua_register(L, "drawRect", l_draw_rect);
  lua_register(L, "drawGradient", l_draw_gradient);
  lua_register(L, "drawLine", l_draw_line);
  lua_register(L, "drawCircle", l_draw_circle); // Зарегистрировали круг!

  lua_register(L, "animCreate", l_anim_create);
  lua_register(L, "animTo", l_anim_to);
  lua_register(L, "animStep", l_anim_step);
  lua_register(L, "animEval", l_anim_eval);

  lua_register(L, "button", l_button);
  lua_register(L, "checkbox", l_checkbox);
  lua_register(L, "slider", l_slider);

  lua_register(L, "exec", l_exec);
  lua_register(L, "getUptime", l_get_uptime);
  lua_register(L, "getMemInfo", l_get_mem_info);
  lua_register(L, "getMouse", l_get_mouse);
  lua_register(L, "getLastKey", l_get_last_key);
  lua_register(L, "scancodeToAscii", l_scancode_to_ascii);
  lua_register(L, "readFile", l_read_file);
  lua_register(L, "saveFile", l_save_file);
  lua_register(L, "getFiles", l_get_files);
  lua_register(L, "getScreenSize", l_get_screen_size);

  lua_register(L, "getTasks", l_get_tasks);
  lua_register(L, "killTask", l_kill_task);
  lua_register(L, "killAllTasks", l_kill_all_tasks);

  lua_register(L, "shellExec", l_shell_exec);
  lua_register(L, "drawBlur", l_draw_blur); // Заменили на сверхбыстрый SSE!
  lua_register(L, "setAppWindowPos", l_set_app_window_pos);
  lua_register(L, "playSound", l_play_sound);
}
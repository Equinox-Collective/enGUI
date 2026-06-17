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

// --- ПЕРЕМЕННЫЕ ДЛЯ БЫСТРОГО АКРИЛОВОГО БЛЮРА ---
static uint32_t *blur_temp1 = NULL;
static uint32_t *blur_temp2 = NULL;
static int blur_temp_cap = 0;

static void ensure_blur_temp(int size) {
    if (size > blur_temp_cap) {
        if (blur_temp1) free(blur_temp1);
        if (blur_temp2) free(blur_temp2);
        blur_temp1 = malloc(size * sizeof(uint32_t));
        blur_temp2 = malloc(size * sizeof(uint32_t));
        blur_temp_cap = size;
    }
}

// Быстрый одномерный горизонтальный Box Blur с алгоритмом скользящего окна O(1)
static void box_blur_h(uint32_t *src, uint32_t *dst, int w, int h, int r) {
    int div = 2 * r + 1;
    for (int y = 0; y < h; y++) {
        uint32_t *src_row = &src[y * w];
        uint32_t *dst_row = &dst[y * w];
        
        int r_sum = 0, g_sum = 0, b_sum = 0;
        
        for (int i = -r; i <= r; i++) {
            int col = (i < 0) ? 0 : (i >= w ? w - 1 : i);
            uint32_t p = src_row[col];
            r_sum += (p >> 16) & 0xFF;
            g_sum += (p >> 8) & 0xFF;
            b_sum += p & 0xFF;
        }
        
        for (int x = 0; x < w; x++) {
            dst_row[x] = ((r_sum / div) << 16) | ((g_sum / div) << 8) | (b_sum / div);
            
            int prev = x - r;
            if (prev < 0) prev = 0;
            int next = x + r + 1;
            if (next >= w) next = w - 1;
            
            uint32_t p_prev = src_row[prev];
            uint32_t p_next = src_row[next];
            
            r_sum += ((p_next >> 16) & 0xFF) - ((p_prev >> 16) & 0xFF);
            g_sum += ((p_next >> 8) & 0xFF) - ((p_prev >> 8) & 0xFF);
            b_sum += (p_next & 0xFF) - (p_prev & 0xFF);
        }
    }
}

// Быстрый одномерный вертикальный Box Blur с алгоритмом скользящего окна O(1)
static void box_blur_v(uint32_t *src, uint32_t *dst, int w, int h, int r) {
    int div = 2 * r + 1;
    for (int x = 0; x < w; x++) {
        int r_sum = 0, g_sum = 0, b_sum = 0;
        
        for (int i = -r; i <= r; i++) {
            int row = (i < 0) ? 0 : (i >= h ? h - 1 : i);
            uint32_t p = src[row * w + x];
            r_sum += (p >> 16) & 0xFF;
            g_sum += (p >> 8) & 0xFF;
            b_sum += p & 0xFF;
        }
        
        for (int y = 0; y < h; y++) {
            dst[y * w + x] = ((r_sum / div) << 16) | ((g_sum / div) << 8) | (b_sum / div);
            
            int prev = y - r;
            if (prev < 0) prev = 0;
            int next = y + r + 1;
            if (next >= h) next = h - 1;
            
            uint32_t p_prev = src[prev * w + x];
            uint32_t p_next = src[next * w + x];
            
            r_sum += ((p_next >> 16) & 0xFF) - ((p_prev >> 16) & 0xFF);
            g_sum += ((p_next >> 8) & 0xFF) - ((p_prev >> 8) & 0xFF);
            b_sum += (p_next & 0xFF) - (p_prev & 0xFF);
        }
    }
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
    printf("[sysgui] api_tick_audio: Playback FINISHED successfully.\n");
    _syscall(20, 0, 0, 0, 0, 0);

    wav_playing = false;
    wav_pcm_data = NULL;
    wav_pcm_size = 0;
    wav_pcm_pos = 0;
  }
}

static bool parse_wav(uint8_t *file_data, uint32_t size,
                      uint8_t **out_pcm, uint32_t *out_len, uint32_t *out_rate) {
  if (size < 44) {
    printf("[sysgui] parse_wav: ERROR - File too small (%d bytes)\n", size);
    return false;
  }
  if (memcmp(file_data, "RIFF", 4) != 0 || memcmp(file_data + 8, "WAVE", 4) != 0) {
    printf("[sysgui] parse_wav: ERROR - Invalid RIFF/WAVE header! Magic: %.4s, Format: %.4s\n",
           file_data, file_data + 8);
    return false;
  }

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
      printf("[sysgui] parse_wav: Found 'fmt ' chunk. Rate: %d Hz, Ch: %d, BPS: %d\n",
             fmt.rate, fmt.ch, fmt.bps);
    } else if (memcmp(ch->id, "data", 4) == 0) {
      audio_len = ch->size;
      audio_ptr = file_data + offset + 8;
      printf("[sysgui] parse_wav: Found 'data' chunk. Size: %d bytes\n", audio_len);
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
  printf("[sysgui] parse_wav: ERROR - Failed to parse. data_found=%s, fmt_found=%s\n",
         audio_ptr ? "true" : "false", found_fmt ? "true" : "false");
  return false;
}

static void start_playback(uint8_t *pcm, uint32_t len, uint32_t rate) {
  _syscall(21, rate, 0, 0, 0, 0);
  printf("[sysgui] start_playback: AC97 rate %d Hz, %d bytes. Playback starting...\n", rate, len);
  wav_pcm_data = pcm;
  wav_pcm_size = len;
  wav_pcm_pos  = 0;
  wav_playing  = true;
}

static bool play_wav_file(const char *filename) {
  uint32_t size = 0;
  printf("[sysgui] play_wav_file: Request to play '%s'\n", filename);
  uint64_t addr = _syscall(2, (uint64_t)filename, (uint64_t)&size, 0, 0, 0);
  if (!addr) {
    printf("[sysgui] play_wav_file: ERROR - File not found or read failed (addr is NULL)\n");
    return false;
  }
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
  if (!boot_sound_cfg_enabled()) {
    printf("[sysgui] api_preload_boot_sound: отключён в bootvid.lua (BOOT_SOUND_ENABLED=false)\n");
    return;  
  }
  uint32_t size = 0;
  printf("[sysgui] api_preload_boot_sound: loading '%s'\n", BOOT_SOUND_PATH);
  uint64_t addr = _syscall(2, (uint64_t)BOOT_SOUND_PATH, (uint64_t)&size, 0, 0, 0);
  if (!addr) {
    printf("[sysgui] api_preload_boot_sound: ERROR - File not found (addr is NULL)\n");
    return;
  }
  if (parse_wav((uint8_t *)addr, size, &boot_pcm, &boot_len, &boot_rate)) {
    boot_loaded = true;
    printf("[sysgui] api_preload_boot_sound: ready (%d bytes @ %d Hz)\n", boot_len, boot_rate);
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

  eid_draw_text(draw_target, screen_w, screen_h, x, y, str, color);

  int text_len = strlen(str);
  sysgui_mark_dirty(x, y, text_len * 8, 16);

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

  eid_draw_gradient_rect(draw_target, screen_w, screen_h, x, y, w, h, c1, c2,
                         vertical);
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

  int label_len = strlen(label);
  sysgui_mark_dirty(x, y, 18 + 8 + label_len * 8, 20);

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
  int ret = sys_exec(cmd);
  lua_pushinteger(L, ret);
  return 1;
}

static int l_get_uptime(lua_State *L) {
  uint32_t ms = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
  lua_pushnumber(L, (double)ms / 1000.0);
  return 1;
}

static int l_get_mem_info(lua_State *L) {
  uint64_t used = sys_get_used_mem();
  uint64_t total = sys_get_total_mem();
  lua_pushnumber(L, (double)used);
  lua_pushnumber(L, (double)total);
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
  uint64_t addr =
      _syscall(SYS_READ_FILE, (uint64_t)filename, (uint64_t)&size, 0, 0, 0);
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

  struct {
    char name[128];
    uint32_t size;
    char dev[32];
  } entry;

  for (int i = 0;; i++) {
    uint64_t ret = _syscall(SYS_READ_DIR, i, (uint64_t)&entry, 0, 0, 0);
    if (!ret) {
      break;
    }

    lua_newtable(L);

    lua_pushstring(L, "name");
    lua_pushstring(L, entry.name);
    lua_settable(L, -3);

    lua_pushstring(L, "size");
    lua_pushinteger(L, entry.size);
    lua_settable(L, -3);

    lua_pushstring(L, "dev");
    lua_pushstring(L, entry.dev);
    lua_settable(L, -3);

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
    uint64_t ok =
        _syscall(SYS_TASK_INFO, (uint64_t)i, (uint64_t)&info, 0, 0, 0);
    if (!ok)
      break;
    lua_newtable(L);
    lua_pushstring(L, "pid");
    lua_pushinteger(L, (lua_Integer)info.pid);
    lua_settable(L, -3);
    lua_pushstring(L, "state");
    lua_pushstring(L, info.running ? "RUNNING" : "STOPPED");
    lua_settable(L, -3);
    lua_pushstring(L, "cr3");
    lua_pushinteger(L, (lua_Integer)info.cr3);
    lua_settable(L, -3);
    lua_pushstring(L, "brk");
    lua_pushinteger(L, (lua_Integer)info.brk);
    lua_settable(L, -3);
    rawseti:
    lua_rawseti(L, -2, out_idx++);
  }
  return 1;
}

static int l_kill_task(lua_State *L) {
  lua_Integer pid = luaL_checkinteger(L, 1);
  uint64_t ok = _syscall(SYS_TASK_KILL, (uint64_t)pid, 0, 0, 0, 0);
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

static int l_kill_all_tasks(lua_State *L) {
  uint64_t n = _syscall(SYS_TASK_KILLALL, 0, 0, 0, 0, 0);
  lua_pushinteger(L, (lua_Integer)n);
  return 1;
}

static int l_shell_exec(lua_State *L) {
  const char *line = luaL_checkstring(L, 1);
  static char outbuf[2048];
  outbuf[0] = '\0';
  uint64_t n = _syscall(SYS_SHELL_EXEC, (uint64_t)line, (uint64_t)outbuf,
                        (uint64_t)sizeof(outbuf), 0, 0);
  if (n >= sizeof(outbuf))
    n = sizeof(outbuf) - 1;
  outbuf[n] = '\0';
  lua_pushlstring(L, outbuf, (size_t)n);
  return 1;
}

// РЕВОЛЮЦИОННЫЙ ВЫСОКОПРОИЗВОДИТЕЛЬНЫЙ ACRYLIC BLUR (SSE & DOWNSAMPLING)
static int l_draw_blur(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  float amount = (float)luaL_checknumber(L, 5); // Интенсивность подложки (0.0 - 1.0)

  if (w <= 0 || h <= 0) return 0;

  // Ограничиваем рамками экрана
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int)screen_w) w = (int)screen_w - x;
  if (y + h > (int)screen_h) h = (int)screen_h - y;
  if (w <= 4 || h <= 4) return 0;

  // Фактор даунсэмплинга = 4 (уменьшает количество обрабатываемых пикселей в 16 раз!)
  int dw = w / 4;
  int dh = h / 4;
  if (dw <= 0 || dh <= 0) return 0;

  ensure_blur_temp(dw * dh);
  if (!blur_temp1 || !blur_temp2) return 0;

  // 1. Быстрое сжатие картинки под окном (Downsample)
  for (int dy = 0; dy < dh; dy++) {
    int sy = y + dy * 4;
    uint32_t *src_row = &draw_target[sy * screen_w];
    uint32_t *dst_row = &blur_temp1[dy * dw];
    for (int dx = 0; dx < dw; dx++) {
      int sx = x + dx * 4;
      dst_row[dx] = src_row[sx];
    }
  }

  // 2. Сверхбыстрое размытие по горизонтали и вертикали (радиус 2 на сжатом буфере дает эффект радиуса 8)
  box_blur_h(blur_temp1, blur_temp2, dw, dh, 2);
  box_blur_v(blur_temp2, blur_temp1, dw, dh, 2);

  // 3. Билинейный апскейл обратно на экран + тонирование Акрила (чистые целочисленные сдвиги)
  uint32_t tint_color = 0x1A1C24; // Темно-серый оттенок Акрила
  uint32_t alpha = (uint32_t)(amount * 255.0f);
  uint32_t inv_alpha = 255 - alpha;
  
  uint32_t tr = (tint_color >> 16) & 0xFF;
  uint32_t tg = (tint_color >> 8) & 0xFF;
  uint32_t tb = tint_color & 0xFF;

  for (int dy = 0; dy < h; dy++) {
    int dst_y = y + dy;
    uint32_t *dst_row = &draw_target[dst_y * screen_w];
    
    int y_floor = dy >> 2;
    int y_ceil = y_floor + 1;
    if (y_ceil >= dh) y_ceil = dh - 1;
    int wy = (dy & 3) * 64; 
    int inv_wy = 256 - wy;

    for (int dx = 0; dx < w; dx++) {
      int dst_x = x + dx;
      
      int x_floor = dx >> 2;
      int x_ceil = x_floor + 1;
      if (x_ceil >= dw) x_ceil = dw - 1;
      int wx = (dx & 3) * 64;
      int inv_wx = 256 - wx;

      uint32_t p00 = blur_temp1[y_floor * dw + x_floor];
      uint32_t p10 = blur_temp1[y_floor * dw + x_ceil];
      uint32_t p01 = blur_temp1[y_ceil * dw + x_floor];
      uint32_t p11 = blur_temp1[y_ceil * dw + x_ceil];

      // Интерполяция весов R, G, B без плавающей точки (сдвиг 16)
      int r_blurred = (
        ((p00 >> 16) & 0xFF) * inv_wx * inv_wy +
        ((p10 >> 16) & 0xFF) * wx * inv_wy +
        ((p01 >> 16) & 0xFF) * inv_wx * wy +
        ((p11 >> 16) & 0xFF) * wx * wy
      ) >> 16;

      int g_blurred = (
        ((p00 >> 8) & 0xFF) * inv_wx * inv_wy +
        ((p10 >> 8) & 0xFF) * wx * inv_wy +
        ((p01 >> 8) & 0xFF) * inv_wx * wy +
        ((p11 >> 8) & 0xFF) * wx * wy
      ) >> 16;

      int b_blurred = (
        (p00 & 0xFF) * inv_wx * inv_wy +
        (p10 & 0xFF) * wx * inv_wy +
        (p01 & 0xFF) * inv_wx * wy +
        (p11 & 0xFF) * wx * wy
      ) >> 16;

      // Смешивание с тонировкой (Alpha Blending)
      uint32_t final_r = (r_blurred * inv_alpha + tr * alpha) >> 8;
      uint32_t final_g = (g_blurred * inv_alpha + tg * alpha) >> 8;
      uint32_t final_b = (b_blurred * inv_alpha + tb * alpha) >> 8;

      dst_row[dst_x] = (final_r << 16) | (final_g << 8) | final_b;
    }
  }

  sysgui_mark_dirty(x, y, w, h);
  return 0;
}

// Быстрый альфа-блендинг прямоугольника на Си (без DIV)
static int l_draw_transparent_rect(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  uint32_t color = (uint32_t)luaL_checknumber(L, 5);
  float alpha_f = (float)luaL_checknumber(L, 6);

  if (w <= 0 || h <= 0) return 0;
  uint32_t alpha = (uint32_t)(alpha_f * 255.0f);
  if (alpha == 0) return 0;
  uint32_t inv_alpha = 255 - alpha;

  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int)screen_w) w = (int)screen_w - x;
  if (y + h > (int)screen_h) h = (int)screen_h - y;
  if (w <= 0 || h <= 0) return 0;

  uint33_t tr = (color >> 16) & 0xFF;
  uint33_t tg = (color >> 8) & 0xFF;
  uint33_t tb = color & 0xFF;

  for (int i = y; i < y + h; i++) {
    uint32_t *row = &draw_target[i * screen_w];
    for (int j = x; j < x + w; j++) {
      uint32_t bg = row[j];
      uint8_t br = (bg >> 16) & 0xFF;
      uint8_t bg_g = (bg >> 8) & 0xFF;
      uint8_t bb = bg & 0xFF;

      uint32_t r_out = (tr * alpha + br * inv_alpha) >> 8;
      uint32_t g_out = (tg * alpha + bg_g * inv_alpha) >> 8;
      uint32_t b_out = (tb * alpha + bb * inv_alpha) >> 8;

      row[j] = (r_out << 16) | (g_out << 8) | b_out;
    }
  }

  sysgui_mark_dirty(x, y, w, h);
  return 0;
}

static int l_set_app_window_pos(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);

  k_app_win_x = x;
  k_app_win_y = y;
  k_app_win_w = w;
  k_app_win_h = h;
  k_app_win_active = (w > 0 && h > 0);

  _syscall(36, (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h, 0);

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
  lua_register(L, "drawBlur", l_draw_blur);
  lua_register(L, "drawTransparentRect", l_draw_transparent_rect); // Зарегистрировано!
  lua_register(L, "setAppWindowPos", l_set_app_window_pos);
  lua_register(L, "playSound", l_play_sound);
}
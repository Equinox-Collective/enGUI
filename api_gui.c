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
    
    // СТОПИМ КАРТУ, чтобы в QEMU не зацикливался последний буфер!
    _syscall(20, 0, 0, 0, 0, 0);

    wav_playing = false;
    wav_pcm_data = NULL;
    wav_pcm_size = 0;
    wav_pcm_pos = 0;
  }
}

// Парсит уже загруженный в память WAV (RIFF/WAVE PCM). НЕ трогает AC'97 и не
// запускает воспроизведение — только находит data-чанк и частоту. Возвращает
// true и заполняет out_pcm/out_len/out_rate при успехе.
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

// Немедленно начинает воспроизведение: настраивает AC'97 на нужную частоту и
// взводит wav_playing (далее буферы докармливает api_tick_audio()).
static void start_playback(uint8_t *pcm, uint32_t len, uint32_t rate) {
  _syscall(21, rate, 0, 0, 0, 0);
  printf("[sysgui] start_playback: AC97 rate %d Hz, %d bytes. Playback starting...\n", rate, len);
  wav_pcm_data = pcm;
  wav_pcm_size = len;
  wav_pcm_pos  = 0;
  wav_playing  = true;
}

// Загружает WAV с диска (syscall 2 — БЛОКИРУЮЩЕЕ чтение по ATA-PIO), парсит и
// сразу запускает воспроизведение. Используется Lua-функцией playSound.
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

// === ЗВУК ЗАПУСКА ОС ===
// При быстрой загрузке звуковая карта (AC'97) инициализируется в ФОНОВОМ потоке
// ПОСЛЕ старта рабочего стола, поэтому к моменту короткого сплэша она ещё не
// готова. Чтение же 846/423КБ WAV с диска по ATA-PIO на whpx ОЧЕНЬ медленное
// (каждый опрос порта = VM-exit), и если делать его в главном цикле отрисовки —
// рабочий стол замирает на ~10 c (чёрный экран).
//
// Решение: ФАЙЛ ГРУЗИМ ОДИН РАЗ ЗАРАНЕЕ — api_preload_boot_sound() вызывается ДО
// главного цикла, пока ещё крутится kernel-сплэш (анимация не замирает на
// блокирующих сисколлах). А запуск воспроизведения (дёшево, без чтения диска)
// откладываем до готовности AC'97 — api_try_boot_sound() в главном цикле.
#ifndef BOOT_SOUND_ENABLED
#define BOOT_SOUND_ENABLED 1
#endif
#define BOOT_SOUND_PATH "res/sysgui/BOOTSOUND.wav"

static int audio_is_ready(void) { return (int)_syscall(22, 0, 0, 0, 0, 0); }

static uint8_t *boot_pcm = NULL;
static uint32_t boot_len = 0, boot_rate = 0;
static bool     boot_loaded = false;

// Грузит и парсит звук запуска В ПАМЯТЬ (без старта). Вызывать ОДИН раз до цикла.
void api_preload_boot_sound(void) {
#if BOOT_SOUND_ENABLED
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

// Запускает заранее загруженный звук запуска ОДИН раз, как только готов AC'97.
// Дёшево: никакого чтения диска здесь нет.
void api_try_boot_sound(void) {
#if BOOT_SOUND_ENABLED
  static bool s_done = false;
  if (s_done || !boot_loaded) return;
  if (wav_playing) return;        // что-то уже играет — не перебиваем
  if (!audio_is_ready()) return;  // ждём инициализации AC'97 (фоновый hw_init)
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

static int l_draw_blur(lua_State *L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  float amount = (float)luaL_checknumber(L, 5);

  for (int i = y; i < y + h; i++) {
    for (int j = x; j < x + w; j++) {
      if (i <= 0 || j <= 0 || (uint32_t)i >= screen_h - 1 ||
          (uint32_t)j >= screen_w - 1)
        continue;

      uint32_t c1 = draw_target[i * screen_w + j];
      uint32_t c2 = draw_target[(i + 1) * screen_w + j];
      uint32_t c3 = draw_target[i * screen_w + (j + 1)];
      uint32_t c4 = draw_target[(i - 1) * screen_w + j];

      uint8_t r = (((c1 >> 16) & 0xFF) + ((c2 >> 16) & 0xFF) +
                   ((c3 >> 16) & 0xFF) + ((c4 >> 16) & 0xFF)) /
                  4;
      uint8_t g = (((c1 >> 8) & 0xFF) + ((c2 >> 8) & 0xFF) +
                   ((c3 >> 8) & 0xFF) + ((c4 >> 8) & 0xFF)) /
                  4;
      uint8_t b = ((c1 & 0xFF) + (c2 & 0xFF) + (c3 & 0xFF) + (c4 & 0xFF)) / 4;

      r = (uint8_t)(r * amount);
      g = (uint8_t)(g * amount);
      b = (uint8_t)(b * amount);

      draw_target[i * screen_w + j] = (r << 16) | (g << 8) | b;
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

  // 1. Обновляем копию в юзерспейсе (для copy_dirty_to_vram в main.c)
  k_app_win_x = x;
  k_app_win_y = y;
  k_app_win_w = w;
  k_app_win_h = h;
  k_app_win_active = (w > 0 && h > 0);

  // 2. Отправляем сисколл в ядро (для блокировки ввода в syscall.c)
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
  lua_register(L, "setAppWindowPos", l_set_app_window_pos);
  
  // Регистрируем новую функцию в Lua
  lua_register(L, "playSound", l_play_sound);
}
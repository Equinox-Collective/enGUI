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

uint32_t *vram = NULL;
uint32_t *backbuffer = NULL;
uint32_t *draw_target = NULL;
uint32_t screen_w = 1024;
uint32_t screen_h = 768;

// --- СИСТЕМА ДИНАМИЧЕСКИХ ГРЯЗНЫХ ТАЙЛОВ ---
#define TILE_SIZE 32
static uint8_t *dirty_grid = NULL;
static int grid_cols = 0;
static int grid_rows = 0;

void sysgui_init_dirty_grid(void) {
  grid_cols = (screen_w + TILE_SIZE - 1) / TILE_SIZE;
  grid_rows = (screen_h + TILE_SIZE - 1) / TILE_SIZE;
  dirty_grid = (uint8_t *)malloc(grid_cols * grid_rows);
  if (dirty_grid) {
    memset(dirty_grid, 1,
           grid_cols * grid_rows); // Первый кадр полностью грязный
  }
}

// Пометка произвольной области экрана как требующей обновления
void sysgui_mark_dirty(int x, int y, int w, int h) {
  if (!dirty_grid)
    return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > (int)screen_w)
    w = (int)screen_w - x;
  if (y + h > (int)screen_h)
    h = (int)screen_h - y;
  if (w <= 0 || h <= 0)
    return;

  int start_col = x / TILE_SIZE;
  int end_col = (x + w - 1) / TILE_SIZE;
  int start_row = y / TILE_SIZE;
  int end_row = (y + h - 1) / TILE_SIZE;

  for (int r = start_row; r <= end_row; r++) {
    for (int c = start_col; c <= end_col; c++) {
      dirty_grid[r * grid_cols + c] = 1;
    }
  }
}

void sysgui_mark_all_dirty(void) {
  if (dirty_grid) {
    memset(dirty_grid, 1, grid_cols * grid_rows);
  }
}

void sysgui_clear_dirty_grid(void) {
  if (dirty_grid) {
    memset(dirty_grid, 0, grid_cols * grid_rows);
  }
}

static inline void fast_memcpy_sse(void *dest, const void *src, size_t bytes) {
  size_t blocks = bytes / 64;
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  for (size_t i = 0; i < blocks; i++) {
    __asm__ volatile("movups 0(%0), %%xmm0\n"
                     "movups 16(%0), %%xmm1\n"
                     "movups 32(%0), %%xmm2\n"
                     "movups 48(%0), %%xmm3\n"
                     "movntdq %%xmm0, 0(%1)\n"
                     "movntdq %%xmm1, 16(%1)\n"
                     "movntdq %%xmm2, 32(%1)\n"
                     "movntdq %%xmm3, 48(%1)\n"
                     :
                     : "r"(s), "r"(d)
                     : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
    s += 64;
    d += 64;
  }

  size_t remaining = bytes % 64;
  for (size_t i = 0; i < remaining; i++) {
    d[i] = s[i];
  }

  __asm__ volatile("sfence" ::: "memory");
}

// Высокопроизводительное копирование только изменившихся участков
void copy_dirty_to_vram(void) {
  if (!dirty_grid)
    return;

  for (int r = 0; r < grid_rows; r++) {
    int c = 0;
    while (c < grid_cols) {
      if (dirty_grid[r * grid_cols + c]) {
        // Ищем непрерывную последовательность "грязных" тайлов в строке для
        // пакетного копирования
        int start_col = c;
        while (c < grid_cols && dirty_grid[r * grid_cols + c]) {
          c++;
        }
        int end_col = c;

        int x = start_col * TILE_SIZE;
        int width_pixels = (end_col - start_col) * TILE_SIZE;
        if (x + width_pixels > (int)screen_w) {
          width_pixels = (int)screen_w - x;
        }
        int bytes_to_copy = width_pixels * 4;

        // Копируем строки внутри этого горизонтального среза
        for (int line = 0; line < TILE_SIZE; line++) {
          int pixel_y = r * TILE_SIZE + line;
          if (pixel_y >= (int)screen_h)
            break;

          uint32_t *src = &backbuffer[pixel_y * screen_w + x];
          uint32_t *dst = &vram[pixel_y * screen_w + x];
          fast_memcpy_sse(dst, src, bytes_to_copy);
        }
      } else {
        c++;
      }
    }
  }
}

eid_ctx_t eid_ctx;

extern bool is_any_anim_active(void);

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

  __asm__ volatile("mov $32, %%rax\n"
                   "int $0x80\n"
                   : "=a"(phys_fb), "=b"(width), "=c"(height), "=d"(pitch));

  screen_w = (uint32_t)width;
  screen_h = (uint32_t)height;

  vram = (uint32_t *)_syscall(SYS_MAP_PHYS, phys_fb, screen_w * screen_h * 4, 0,
                              0, 0);

  backbuffer = (uint32_t *)malloc(screen_w * screen_h * 4);
  memset(backbuffer, 0, screen_w * screen_h * 4);

  draw_target = backbuffer;

  // Инициализация сетки грязных тайлов
  sysgui_init_dirty_grid();

  eid_init();
  memset(&eid_ctx, 0, sizeof(eid_ctx));

  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  register_gui_api(L);

  if (luaL_dofile(L, "res/sysgui/init.lua")) {
    printf("enGUI Lua Error: %s\n", lua_tostring(L, -1));
    return 1;
  }

  const uint32_t TICK_MS = 1;
  const uint32_t TARGET_FRAME_MS = 16;
  int last_mx = -9999, last_my = -9999;
  int last_mdown = -1;
  uint16_t last_key = 0;
  uint32_t force_frames = 4;

  uint32_t last_tick = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
  uint32_t frame_start = last_tick;

  while (1) {
    uint64_t fg = _syscall(SYS_GET_FG_APP, 0, 0, 0, 0, 0);
    if (fg == SYS_GET_FG_APP)
      fg = 0;

    // --- БАЙПАС ДЛЯ ВНЕШНИХ ПРИЛОЖЕНИЙ (Браузер, Doom и т.д.) ---
    // Если запущено приложение на переднем плане, sysgui засыпает
    // и полностью освобождает видеопамять и буфер ввода.
    if (fg != 0) {
      sys_sleep(16);
      continue;
    }

    uint64_t mx = 0, my = 0, m_btn = 0;
    __asm__ volatile("mov $7, %%rax\n int $0x80"
                     : "=a"(mx), "=b"(my), "=c"(m_btn));
    int cur_mx = (int)mx;
    int cur_my = (int)my;
    int cur_mdown = (int)((m_btn & 1) != 0);

    // --- Чтение клавиатуры с поддержкой extended-сканкодов (0xE0) ---------
    static bool pending_ext = false;
    uint16_t cur_key = 0;
    for (int i = 0; i < 8; i++) {
      uint8_t b = (uint8_t)_syscall(SYS_GET_SCANCODE, 0, 0, 0, 0, 0);
      if (b == 0)
        break;
      if (b == 0xE0) {
        pending_ext = true;
        continue;
      }
      if (pending_ext) {
        cur_key = (uint16_t)(0x100 | b);
        pending_ext = false;
      } else {
        cur_key = (uint16_t)b;
      }
      break; 
    }

    uint32_t now = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);

    int need_redraw = (force_frames > 0) || (cur_mx != last_mx) ||
                      (cur_my != last_my) || (cur_mdown != last_mdown) ||
                      (cur_key != 0 && cur_key != last_key) ||
                      is_any_anim_active() || (now - last_tick >= 500);

    if (need_redraw) {
      uint32_t elapsed = now - last_tick;
      float dt = (float)(elapsed * TICK_MS);
      if (dt > 200.0f)
        dt = 200.0f;

      sysgui_clear_dirty_grid();

      if (force_frames > 0) {
        sysgui_mark_all_dirty();
      }

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

      lua_getglobal(L, "needs_redraw");
      if (lua_toboolean(L, -1) && force_frames == 0)
        force_frames = 1;
      lua_pop(L, 1);

      sysgui_mark_dirty(last_mx, last_my, 8, 8);
      sysgui_mark_dirty(cur_mx, cur_my, 8, 8);

      draw_cursor_user(backbuffer, cur_mx, cur_my, screen_w, screen_h);

      copy_dirty_to_vram();

      last_mx = cur_mx;
      last_my = cur_my;
      last_mdown = cur_mdown;
      last_key = cur_key;
      if (force_frames > 0)
        force_frames--;

      last_tick = now;
    }

    uint32_t frame_end = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
    uint32_t frame_elapsed = frame_end - frame_start;
    if (frame_elapsed < TARGET_FRAME_MS) {
      sys_sleep(TARGET_FRAME_MS - frame_elapsed);
    } else {
      sys_yield();
    }
    frame_start = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
  }

  lua_close(L);
  free(backbuffer);
  if (dirty_grid)
    free(dirty_grid);
  return 0;
}
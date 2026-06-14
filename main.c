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
extern void api_tick_audio(void);
extern void api_preload_boot_sound(void);
extern void api_try_boot_sound(void);
int k_app_win_x = 100;
int k_app_win_y = 100;
int k_app_win_w = 640;
int k_app_win_h = 400;
bool k_app_win_active = false;

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

  // Первый реальный present кадра GUI: просим ядро погасить boot-анимацию
  // Nyan Cat (всё это время её крутил PIT-таймер ядра, см. src/system/misc/
  // timer.c + src/boot/eqstart.c). Делаем это ровно здесь, чтобы между
  // последним кадром гифки и первым кадром рабочего стола НЕ было заметной
  // «заморозки». syscall 88 = SYS_BOOT_ANIM_DONE.
  static int g_boot_anim_signaled = 0;
  if (!g_boot_anim_signaled) {
    g_boot_anim_signaled = 1;
    _syscall(88, 0, 0, 0, 0, 0);
  }

  // Получаем координаты окна запущенного приложения
  extern int k_app_win_x, k_app_win_y, k_app_win_w, k_app_win_h;
  extern bool k_app_win_active;

  for (int r = 0; r < grid_rows; r++) {
    int c = 0;
    while (c < grid_cols) {
      if (dirty_grid[r * grid_cols + c]) {
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

        for (int line = 0; line < TILE_SIZE; line++) {
          int pixel_y = r * TILE_SIZE + line;
          if (pixel_y >= (int)screen_h)
            break;

          // Если строка пересекает окно Doom, копируем левую и правую части
          // строго через стандартный безопасный memcpy (он не требует 16-байтового выравнивания)
          if (k_app_win_active && 
              pixel_y >= k_app_win_y && pixel_y < k_app_win_y + k_app_win_h) {
              
              // Копируем часть строки слева от окна
              int left_copy_w = k_app_win_x - x;
              if (left_copy_w > width_pixels) left_copy_w = width_pixels;
              
              if (left_copy_w > 0) {
                  memcpy(&vram[pixel_y * screen_w + x], 
                         &backbuffer[pixel_y * screen_w + x], 
                         left_copy_w * 4);
              }
              
              // Копируем часть строки справа от окна
              int right_start_x = k_app_win_x + k_app_win_w;
              int right_copy_offset = right_start_x - x;
              if (right_copy_offset < width_pixels) {
                  int right_copy_w = width_pixels - right_copy_offset;
                  if (right_copy_w > 0) {
                      memcpy(&vram[pixel_y * screen_w + right_start_x], 
                             &backbuffer[pixel_y * screen_w + right_start_x], 
                             right_copy_w * 4);
                  }
              }
          } else {
              // Вне окна копируем всю грязную линию целиком (здесь выравнивание по TILE_SIZE гарантировано)
              uint32_t *src = &backbuffer[pixel_y * screen_w + x];
              uint32_t *dst = &vram[pixel_y * screen_w + x];
              fast_memcpy_sse(dst, src, width_pixels * 4);
          }
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

  // ПРИМЕЧАНИЕ: Nyan Cat намеренно НЕ гасится здесь. Пока ниже грузятся и
  // парсятся lua-скрипты (window/terminal/paint/... + bootvid), kernel-таймер
  // продолжает крутить гифку прямо на фреймбуфере. Чтобы она не «замерзала» на
  // время блокирующих сисколлов (чтения с диска / serial), ядро на время
  // boot-анимации держит шлюз int 0x80 как trap gate — прерывания не гасятся
  // во время сисколлов, и PIT успевает крутить кадры. Гасим анимацию только на
  // ПЕРВОМ реальном present GUI (syscall 88 внутри copy_dirty_to_vram).

  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  register_gui_api(L);

  // Останавливаем сборщик мусора на время загрузки всех скриптов (init.lua
  // через dofile тянет ещё 7 модулей). При загрузке создаётся МНОГО мелких
  // объектов (строки токенов, прототипы функций), и инкрементальный GC по
  // умолчанию гоняет проходы кучи прямо во время парсинга -> заметные паузы.
  // Выключаем GC, грузим всё, затем один раз собираем мусор и включаем обратно.
  printf("[GUI T=%u] lua load begin (GC stopped)\n",
         (unsigned)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0));
  lua_gc(L, LUA_GCSTOP, 0);

  // === ДЕБАГГЕР: Ловим ошибки синтаксиса при загрузке (КРАСНЫЙ ЭКРАН) ===
  if (luaL_dofile(L, "res/sysgui/init.lua")) {
    const char *err_msg = lua_tostring(L, -1);
    printf("enGUI Lua Load Error: %s\n", err_msg);
    
    // Заливаем экран темно-красным
    for (uint32_t i = 0; i < screen_w * screen_h; i++) {
      backbuffer[i] = 0x550000;
    }
    
    eid_draw_text(backbuffer, screen_w, screen_h, 40, 50, "enGUI LUA SYNTAX ERROR", 0xFFFFFF);
    eid_draw_text(backbuffer, screen_w, screen_h, 40, 80, err_msg, 0xFFFF00);
    
    sysgui_mark_all_dirty();
    copy_dirty_to_vram();
    
    // Зависаем, чтобы разработчик мог прочитать лог
    while (1) {
      sys_sleep(1000);
    }
  }

  // Загрузка завершена: один полный сбор мусора и возвращаем GC в работу.
  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCRESTART, 0);
  printf("[GUI T=%u] lua load end (GC restarted)\n",
         (unsigned)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0));

  const uint32_t TICK_MS = 1;
  const uint32_t TARGET_FRAME_MS = 16;
  int last_mx = -9999, last_my = -9999;
  int last_mdown = -1;
  uint16_t last_key = 0;
  uint32_t force_frames = 4;
  uint32_t high_resp_frames = 0; // <--- Счетчик кадров повышенной отзывчивости

  // Грузим звук запуска ДО главного цикла: чтение WAV по ATA-PIO блокирующее,
  // но здесь ещё не было ни одного present, поэтому продолжает крутиться
  // kernel-сплэш (анимация не замирает на блокирующих сисколлах). Сам запуск
  // воспроизведения произойдёт в цикле, когда инициализируется AC'97.
  api_preload_boot_sound();

  uint32_t last_tick = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
  uint32_t frame_start = last_tick;
  uint64_t last_fg = 0;

  while (1) {
    uint64_t fg = _syscall(SYS_GET_FG_APP, 0, 0, 0, 0, 0);
    if (fg == SYS_GET_FG_APP)
      fg = 0;

    // Пока активен полноэкранный foreground-ELF (например browser.elf или
    // doom), он рисует через SYS_DRAW_BUFFER в ту же VRAM, что и sysgui через
    // copy_dirty_to_vram(). Без этой проверки оба процесса гонят свои кадры
    // наперегонки — видно как сильное мерцание / затирание поверх окна
    // приложения. Пока fg != 0, sysgui просто спит и не трогает экран. Когда
    // foreground отпускает фокус (SYS_EXIT обнуляет fg_app_pid в ядре),
    // форсируем полный перерисов рабочего стола.
    if (fg != 0) {
      last_fg = fg;

      /* SDL-приложение рисует в VRAM напрямую через sys_draw_app_buffer.
       * enGUI при этом не трогает экран — но курсор тоже никто не рисует.
       * Рисуем курсор поверх VRAM с сохранением пикселей под ним, чтобы
       * не оставался шлейф при движении. */
      {
        uint64_t cmx = 0, cmy = 0;
        __asm__ volatile("mov $7, %%rax\n int $0x80"
                         : "=a"(cmx), "=b"(cmy)
                         :
                         : "rcx", "rdx", "rsi", "rdi", "r8", "memory");

        /* Буфер для сохранения пикселей под курсором (8x8 пикселей) */
        static uint32_t cursor_save[8 * 8];
        static int cursor_last_x = -1, cursor_last_y = -1;
        static bool cursor_saved = false;
        int cx = (int)cmx, cy = (int)cmy;

        if (cx != cursor_last_x || cy != cursor_last_y) {
          /* 1. Восстанавливаем пиксели под старым положением курсора */
          if (cursor_saved && cursor_last_x >= 0 && cursor_last_y >= 0) {
            for (int i = 0; i < 8; i++) {
              int py = cursor_last_y + i;
              if (py < 0 || py >= (int)screen_h) continue;
              for (int j = 0; j < 8; j++) {
                int px = cursor_last_x + j;
                if (px < 0 || px >= (int)screen_w) continue;
                vram[py * screen_w + px] = cursor_save[i * 8 + j];
              }
            }
          }

          /* 2. Сохраняем пиксели под новым положением курсора */
          for (int i = 0; i < 8; i++) {
            int py = cy + i;
            for (int j = 0; j < 8; j++) {
              int px = cx + j;
              if (py >= 0 && py < (int)screen_h && px >= 0 && px < (int)screen_w) {
                cursor_save[i * 8 + j] = vram[py * screen_w + px];
              } else {
                cursor_save[i * 8 + j] = 0;
              }
            }
          }
          cursor_saved = true;

          /* 3. Рисуем курсор прямо в vram */
          draw_cursor_user(vram, cx, cy, screen_w, screen_h);
          cursor_last_x = cx;
          cursor_last_y = cy;
        }
      }

      /* Минимальный сон 1ms вместо 16ms — не блокируем шедулер.
       * Если enGUI и SDL оба делают sys_sleep(16) одновременно,
       * шедулер застревает в цикле поиска активной задачи пока
       * PIT не обновит tick — но PIT заблокирован в ISR → deadlock.
       * sys_sleep(1) гарантирует что enGUI проснётся через следующий
       * тик и шедулер найдёт хотя бы одну готовую задачу. */
      sys_sleep(1);
      frame_start = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);
      continue;
    }
    if (last_fg != 0) {
      force_frames = 4;
      last_fg = 0;
    }

    uint64_t mx = 0, my = 0, m_btn = 0;
    __asm__ volatile("mov $7, %%rax\n int $0x80"
                     : "=a"(mx), "=b"(my), "=c"(m_btn));
    int cur_mx = (int)mx;
    int cur_my = (int)my;
    int cur_mdown = (int)((m_btn & 1) != 0);

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

    // Если нажата клавиша или кликнули мышкой — включаем режим отзывчивости на 60 кадров (~1 секунда)
    if (cur_key != 0 || cur_mdown != last_mdown) {
      high_resp_frames = 60;
    }

    int need_redraw = (force_frames > 0) || 
                      (cur_mx != last_mx) || 
                      (cur_my != last_my) || 
                      (cur_mdown != last_mdown) ||
                      (cur_key != 0) || 
                      is_any_anim_active() || 
                      k_app_win_active ||  
                      (now - last_tick >= 100);

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
        
        // === ДЕБАГГЕР: Ловим ошибки выполнения во время работы (СИНИЙ ЭКРАН) ===
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
          const char *err_msg = lua_tostring(L, -1);
          printf("enGUI Lua Runtime Error: %s\n", err_msg);
          
          // Заливаем экран темно-синим
          for (uint32_t i = 0; i < screen_w * screen_h; i++) {
            backbuffer[i] = 0x000088;
          }
          
          eid_draw_text(backbuffer, screen_w, screen_h, 40, 50, "enGUI LUA RUNTIME ERROR", 0xFFFFFF);
          eid_draw_text(backbuffer, screen_w, screen_h, 40, 80, err_msg, 0xFFFF00);
          
          sysgui_mark_all_dirty();
          copy_dirty_to_vram();
          
          while (1) {
            sys_sleep(1000);
          }
        }
      } else {
        lua_pop(L, 1);
      }

      lua_getglobal(L, "needs_redraw");
      if (lua_toboolean(L, -1))
        force_frames = 2; // <=== СТАВИМ ТУТ 2. Это разгонит цикл до 60 FPS и починит звук!
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

    if (high_resp_frames > 0) {
      high_resp_frames--;
      sys_yield(); // Уступаем CPU вместо сна, убирая любые задержки при вводе текста
    } else {
      if (frame_elapsed < TARGET_FRAME_MS) {
        sys_sleep(TARGET_FRAME_MS - frame_elapsed);
      } else {
        sys_yield();
      }
    }
    frame_start = (uint32_t)_syscall(SYS_GET_TIME, 0, 0, 0, 0, 0);

    api_tick_audio();
    api_try_boot_sound(); // однократно запустит звук запуска, когда карта готова
  }

  lua_close(L);
  free(backbuffer);
  if (dirty_grid)
    free(dirty_grid);
  return 0;
}
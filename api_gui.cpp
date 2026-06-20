#include "api_gui.h"
#include <equos.h>
#include <stdbool.h>
#include <stdint.h>

extern "C" {
#include <stdlib.h>
#include <string.h>
}

extern uint32_t *draw_target;
extern uint32_t screen_w, screen_h;

extern void sysgui_mark_dirty(int x, int y, int w, int h);

// --- СТАТИЧЕСКИЕ БУФЕРЫ КЭША ДЛЯ БЛЮРА ---
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

bool play_wav_file(const char *filename) {
  uint32_t size = 0;
  uint64_t addr = _syscall(2, (uint64_t)filename, (uint64_t)&size, 0, 0, 0);
  if (!addr) return false;
  uint8_t *pcm; uint32_t len, rate;
  if (!parse_wav((uint8_t *)addr, size, &pcm, &len, &rate)) return false;
  start_playback(pcm, len, rate);
  return true;
}

#ifndef BOOT_SOUND_ENABLED
#define BOOT_SOUND_ENABLED 1
#endif
#define BOOT_SOUND_PATH "res/sysgui/BOOTSOUND.wav"

static int audio_is_ready(void) { return (int)_syscall(22, 0, 0, 0, 0, 0); }

static uint8_t *boot_pcm = NULL;
static uint32_t boot_len = 0, boot_rate = 0;
static bool     boot_loaded = false;

void api_preload_boot_sound(void) {
#if BOOT_SOUND_ENABLED
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

// --- ГЛАВНАЯ ФУНКЦИЯ ACRYLIC GLASS BLUR ---
void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb) {
    if (w <= 16 || h <= 16) return;

    int dsW = w / 4;
    int dsH = h / 4;
    ensure_scratch_buffers(dsW, dsH);
    if (!scratch_buf_1 || !scratch_buf_2) return;

    // 1. Клиппированный безопасный даунсэмплинг
    for (int dy = 0; dy < dsH; dy++) {
        int src_y = y + (dy * 4);
        if (src_y < 0) src_y = 0;
        if (src_y >= (int)screen_h) src_y = screen_h - 1;
        
        uint32_t *src_row = &draw_target[src_y * screen_w];
        uint32_t *dst_row = &scratch_buf_1[dy * dsW];
        
        for (int dx = 0; dx < dsW; dx++) {
            int src_x = x + (dx * 4);
            if (src_x < 0) src_x = 0;
            if (src_x >= (int)screen_w) src_x = screen_w - 1;
            dst_row[dx] = src_row[src_x];
        }
    }

    // 2. Скользящее окно Box Blur
    int r_blur = 2;
    int window_size = r_blur * 2 + 1;

    for (int dy = 0; dy < dsH; dy++) {
        uint32_t *row_src = &scratch_buf_1[dy * dsW];
        uint32_t *row_dst = &scratch_buf_2[dy * dsW];
        
        int sum_r = 0, sum_g = 0, sum_b = 0;
        
        for (int k = -r_blur; k <= r_blur; k++) {
            int px = (k < 0) ? 0 : (k >= dsW ? dsW - 1 : k);
            uint32_t color = row_src[px];
            sum_r += (color >> 16) & 0xFF;
            sum_g += (color >> 8) & 0xFF;
            sum_b += color & 0xFF;
        }
        row_dst[0] = ((sum_r / window_size) << 16) | ((sum_g / window_size) << 8) | (sum_b / window_size);
        
        for (int dx = 1; dx < dsW; dx++) {
            int prev_idx = dx - 1 - r_blur;
            if (prev_idx < 0) prev_idx = 0;
            uint32_t prev_color = row_src[prev_idx];
            sum_r -= (prev_color >> 16) & 0xFF;
            sum_g -= (prev_color >> 8) & 0xFF;
            sum_b -= prev_color & 0xFF;
            
            int next_idx = dx + r_blur;
            if (next_idx >= dsW) next_idx = dsW - 1;
            uint32_t next_color = row_src[next_idx];
            sum_r += (next_color >> 16) & 0xFF;
            sum_g += (next_color >> 8) & 0xFF;
            sum_b += next_color & 0xFF;
            
            row_dst[dx] = ((sum_r / window_size) << 16) | ((sum_g / window_size) << 8) | (sum_b / window_size);
        }
    }

    for (int dx = 0; dx < dsW; dx++) {
        int sum_r = 0, sum_g = 0, sum_b = 0;
        
        for (int k = -r_blur; k <= r_blur; k++) {
            int py = (k < 0) ? 0 : (k >= dsH ? dsH - 1 : k);
            uint32_t color = scratch_buf_2[py * dsW + dx];
            sum_r += (color >> 16) & 0xFF;
            sum_g += (color >> 8) & 0xFF;
            sum_b += color & 0xFF;
        }
        scratch_buf_1[0 * dsW + dx] = ((sum_r / window_size) << 16) | ((sum_g / window_size) << 8) | (sum_b / window_size);
        
        for (int dy = 1; dy < dsH; dy++) {
            int prev_idx = dy - 1 - r_blur;
            if (prev_idx < 0) prev_idx = 0;
            uint32_t prev_color = scratch_buf_2[prev_idx * dsW + dx];
            sum_r -= (prev_color >> 16) & 0xFF;
            sum_g -= (prev_color >> 8) & 0xFF;
            sum_b -= prev_color & 0xFF;
            
            int next_idx = dy + r_blur;
            if (next_idx >= dsH) next_idx = dsH - 1;
            uint32_t next_color = scratch_buf_2[next_idx * dsW + dx];
            sum_r += (next_color >> 16) & 0xFF;
            sum_g += (next_color >> 8) & 0xFF;
            sum_b += next_color & 0xFF;
            
            scratch_buf_1[dy * dsW + dx] = ((sum_r / window_size) << 16) | ((sum_g / window_size) << 8) | (sum_b / window_size);
        }
    }

    // 3. Билинейный апсэмплинг и альфа-смешивание
    uint32_t border_color = (tint_rgb == 0x1F222B) ? 0x4A505C : 0x61AFEF; 
    
    int alpha = (int)(amount_f * 256.0f);
    if (alpha < 0) alpha = 0;
    if (alpha > 256) alpha = 256;
    
    int tint_r = (tint_rgb >> 16) & 0xFF;
    int tint_g = (tint_rgb >> 8) & 0xFF;
    int tint_b = tint_rgb & 0xFF;

    for (int ty = 0; ty < h; ty++) {
        int dst_y = y + ty;
        if (dst_y < 0 || dst_y >= (int)screen_h) continue;

        int y0 = ty >> 2;
        if (y0 >= dsH) y0 = dsH - 1;
        int y1 = (y0 + 1 < dsH) ? y0 + 1 : dsH - 1;
        int wy_int = (ty & 3) << 6;

        uint32_t *dst_row = &draw_target[dst_y * screen_w];

        for (int tx = 0; tx < w; tx++) {
            int dst_x = x + tx;
            if (dst_x < 0 || dst_x >= (int)screen_w) continue;

            bool check_geometry = true;
            if (tx >= radius && tx < w - radius && ty >= radius && ty < h - radius) {
                check_geometry = false;
            }

            if (check_geometry) {
                if (is_pixel_outside_corners(tx, ty, w, h, radius)) continue;
                if (is_pixel_on_border(tx, ty, w, h, radius, 1)) {
                    dst_row[dst_x] = border_color;
                    continue;
                }
            }

            int x0 = tx >> 2;
            if (x0 >= dsW) x0 = dsW - 1;
            int x1 = (x0 + 1 < dsW) ? x0 + 1 : dsW - 1;
            int wx_int = (tx & 3) << 6;

            uint32_t c00 = scratch_buf_1[y0 * dsW + x0];
            uint32_t c10 = scratch_buf_1[y0 * dsW + x1];
            uint32_t c01 = scratch_buf_1[y1 * dsW + x0];
            uint32_t c11 = scratch_buf_1[y1 * dsW + x1];

            int r00 = (c00 >> 16) & 0xFF, g00 = (c00 >> 8) & 0xFF, b00 = c00 & 0xFF;
            int r10 = (c10 >> 16) & 0xFF, g10 = (c10 >> 8) & 0xFF, b10 = c10 & 0xFF;
            int r01 = (c01 >> 16) & 0xFF, g01 = (c01 >> 8) & 0xFF, b01 = c01 & 0xFF;
            int r11 = (c11 >> 16) & 0xFF, g11 = (c11 >> 8) & 0xFF, b11 = c11 & 0xFF;

            int w00 = (256 - wx_int) * (256 - wy_int);
            int w10 = wx_int * (256 - wy_int);
            int w01 = (256 - wx_int) * wy_int;
            int w11 = wx_int * wy_int;

            int blur_r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 16;
            int blur_g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 16;
            int blur_b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 16;

            int final_r = (blur_r * (256 - alpha) + tint_r * alpha) >> 8;
            int final_g = (blur_g * (256 - alpha) + tint_g * alpha) >> 8;
            int final_b = (blur_b * (256 - alpha) + tint_b * alpha) >> 8;

            int sparkle = ((h - ty) * 12) / h;
            final_r = (final_r + sparkle > 255) ? 255 : final_r + sparkle;
            final_g = (final_g + sparkle > 255) ? 255 : final_g + sparkle;
            final_b = (final_b + sparkle > 255) ? 255 : final_b + sparkle;

            dst_row[dst_x] = (final_r << 16) | (final_g << 8) | final_b;
        }
    }

    sysgui_mark_dirty(x, y, w, h);
}

#include "api_gui.h"
#include <equos.h>

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
}

extern uint32_t *draw_target;
extern uint32_t screen_w, screen_h;

// --- ШРИФТОВОЙ ДВИЖОК PSF1/PSF2 ---
static uint8_t* font_glyphs = nullptr;
static int font_width = 8;
static int font_height = 16;
static int font_charsize = 16;

static inline int clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static inline bool is_pixel_masked(int tx, int ty, int w, int h, int r) {
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

bool load_psf_font(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    uint8_t magic[4];
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return false; }

    // PSF2 Формат
    if (magic[0] == 0x72 && magic[1] == 0xb5 && magic[2] == 0x4a && magic[3] == 0x86) {
        uint32_t version, headersize, flags, numglyph, bytesperglyph, height, width;
        fread(&version, 4, 1, f);
        fread(&headersize, 4, 1, f);
        fread(&flags, 4, 1, f);
        fread(&numglyph, 4, 1, f);
        fread(&bytesperglyph, 4, 1, f);
        fread(&height, 4, 1, f);
        fread(&width, 4, 1, f);

        fseek(f, headersize, SEEK_SET);
        font_glyphs = (uint8_t*)malloc(numglyph * bytesperglyph);
        fread(font_glyphs, 1, numglyph * bytesperglyph, f);
        font_width = width;
        font_height = height;
        font_charsize = bytesperglyph;
        fclose(f);
        return true;
    }
    // PSF1 Формат
    else if (magic[0] == 0x36 && magic[1] == 0x04) {
        uint8_t charsize = magic[3];
        font_glyphs = (uint8_t*)malloc(256 * charsize);
        fread(font_glyphs, 1, 256 * charsize, f);
        font_width = 8;
        font_height = charsize;
        font_charsize = charsize;
        fclose(f);
        return true;
    }
    fclose(f);
    return false;
}

namespace GUI {

    void Painter::FillRect(int x, int y, int w, int h, uint32_t color) {
        int x1 = x < 0 ? 0 : x;
        int y1 = y < 0 ? 0 : y;
        int x2 = x + w > (int)width ? width : x + w;
        int y2 = y + h > (int)height ? height : y + h;

        for (int py = y1; py < y2; py++) {
            uint32_t* row = &target[py * width];
            for (int px = x1; px < x2; px++) {
                row[px] = color;
            }
        }
    }

    void Painter::FillRectAlpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
        int x1 = x < 0 ? 0 : x;
        int y1 = y < 0 ? 0 : y;
        int x2 = x + w > (int)width ? width : x + w;
        int y2 = y + h > (int)height ? height : y + h;

        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;

        for (int py = y1; py < y2; py++) {
            uint32_t* row = &target[py * width];
            for (int px = x1; px < x2; px++) {
                uint32_t bg = row[px];
                int br = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bb = bg & 0xFF;
                int res_r = (r * alpha + br * (255 - alpha)) >> 8;
                int res_g = (g * alpha + bg_g * (255 - alpha)) >> 8;
                int res_b = (b * alpha + bb * (255 - alpha)) >> 8;
                row[px] = (res_r << 16) | (res_g << 8) | res_b;
            }
        }
    }

    void Painter::RoundedRect(int x, int y, int w, int h, int r, uint32_t color) {
        int x1 = x < 0 ? 0 : x;
        int y1 = y < 0 ? 0 : y;
        int x2 = x + w > (int)width ? width : x + w;
        int y2 = y + h > (int)height ? height : y + h;

        for (int py = y1; py < y2; py++) {
            uint32_t* row = &target[py * width];
            for (int px = x1; px < x2; px++) {
                int dx = px - x;
                int dy = py - y;
                // Скругление углов
                if (dx < r && dy < r && (r - dx) * (r - dx) + (r - dy) * (r - dy) > r * r) continue;
                if (dx >= w - r && dy < r && (dx - (w - r - 1)) * (dx - (w - r - 1)) + (r - dy) * (r - dy) > r * r) continue;
                if (dx < r && dy >= h - r && (r - dx) * (r - dx) + (dy - (h - r - 1)) * (dy - (h - r - 1)) > r * r) continue;
                if (dx >= w - r && dy >= h - r && (dx - (w - r - 1)) * (dx - (w - r - 1)) + (dy - (h - r - 1)) * (dy - (h - r - 1)) > r * r) continue;
                row[px] = color;
            }
        }
    }

    void Painter::RoundedRectAlpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
        int x1 = x < 0 ? 0 : x;
        int y1 = y < 0 ? 0 : y;
        int x2 = x + w > (int)width ? width : x + w;
        int y2 = y + h > (int)height ? height : y + h;

        int cr = (color >> 16) & 0xFF;
        int cg = (color >> 8) & 0xFF;
        int cb = color & 0xFF;

        for (int py = y1; py < y2; py++) {
            uint32_t* row = &target[py * width];
            for (int px = x1; px < x2; px++) {
                int dx = px - x;
                int dy = py - y;
                if (dx < r && dy < r && (r - dx) * (r - dx) + (r - dy) * (r - dy) > r * r) continue;
                if (dx >= w - r && dy < r && (dx - (w - r - 1)) * (dx - (w - r - 1)) + (r - dy) * (r - dy) > r * r) continue;
                if (dx < r && dy >= h - r && (r - dx) * (r - dx) + (dy - (h - r - 1)) * (dy - (h - r - 1)) > r * r) continue;
                if (dx >= w - r && dy >= h - r && (dx - (w - r - 1)) * (dx - (w - r - 1)) + (dy - (h - r - 1)) * (dy - (h - r - 1)) > r * r) continue;

                uint32_t bg = row[px];
                int br = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bb = bg & 0xFF;
                int res_r = (cr * alpha + br * (255 - alpha)) >> 8;
                int res_g = (cg * alpha + bg_g * (255 - alpha)) >> 8;
                int res_b = (cb * alpha + bb * (255 - alpha)) >> 8;
                row[px] = (res_r << 16) | (res_g << 8) | res_b;
            }
        }
    }

    void Painter::DrawRect(int x, int y, int w, int h, uint32_t color, int thickness) {
        FillRect(x, y, w, thickness, color); // Top
        FillRect(x, y + h - thickness, w, thickness, color); // Bottom
        FillRect(x, y, thickness, h, color); // Left
        FillRect(x + w - thickness, y, thickness, h, color); // Right
    }

    void Painter::DrawRoundedRect(int x, int y, int w, int h, int r, uint32_t color, int thickness) {
        (void)thickness;
        RoundedRect(x, y, w, h, r, color); // Заглушка под кастомные контуры скруглений
    }

    void Painter::Line(int x1, int y1, int x2, int y2, uint32_t color, int thickness) {
        (void)thickness;
        int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
        int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
        int err = dx + dy, e2;

        while (true) {
            if (x1 >= 0 && x1 < (int)width && y1 >= 0 && y1 < (int)height) {
                target[y1 * width + x1] = color;
            }
            if (x1 == x2 && y1 == y2) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    }

    void Painter::Circle(int cx, int cy, int r, uint32_t color, int thickness) {
        (void)thickness;
        int x = -r, y = 0, err = 2 - 2 * r;
        do {
            if (cx - x >= 0 && cx - x < (int)width && cy + y >= 0 && cy + y < (int)height) target[(cy + y) * width + (cx - x)] = color;
            if (cx - y >= 0 && cx - y < (int)width && cy - x >= 0 && cy - x < (int)height) target[(cy - x) * width + (cx - y)] = color;
            if (cx + x >= 0 && cx + x < (int)width && cy - y >= 0 && cy - y < (int)height) target[(cy - y) * width + (cx + x)] = color;
            if (cx + y >= 0 && cx + y < (int)width && cy + x >= 0 && cy + x < (int)height) target[(cy + x) * width + (cx + y)] = color;
            r = err;
            if (r <= y) err += ++y * 2 + 1;
            if (r > x || err > y) err += ++x * 2 + 1;
        } while (x < 0);
    }

    void Painter::CircleFilled(int cx, int cy, int r, uint32_t color) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy <= r * r) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px >= 0 && px < (int)width && py >= 0 && py < (int)height) {
                        target[py * width + px] = color;
                    }
                }
            }
        }
    }

    void Painter::Text(const char* str, int x, int y, uint32_t color) {
        if (!font_glyphs) {
            // Фолбек: если PSF-шрифт не загружен, рисуем простую сеточку
            FillRect(x, y, 8, 16, color);
            return;
        }

        int cur_x = x;
        while (*str) {
            if (*str == '\n') {
                cur_x = x;
                y += font_height;
            } else {
                uint8_t* glyph = &font_glyphs[(uint8_t)*str * font_charsize];
                for (int cy = 0; cy < font_height; cy++) {
                    if (y + cy < 0 || y + cy >= (int)height) continue;
                    uint32_t* dst_row = &target[(y + cy) * width];

                    int bytes_per_row = (font_width + 7) / 8;
                    for (int b = 0; b < bytes_per_row; b++) {
                        uint8_t row_byte = glyph[cy * bytes_per_row + b];
                        for (int cx = 0; cx < 8; cx++) {
                            int pixel_x = cur_x + b * 8 + cx;
                            if (pixel_x < 0 || pixel_x >= (int)width) continue;
                            if ((row_byte >> (7 - cx)) & 1) {
                                dst_row[pixel_x] = color;
                            }
                        }
                    }
                }
                cur_x += font_width;
            }
            str++;
        }
    }

    void Painter::GradientRect(int x, int y, int w, int h, uint32_t col1, uint32_t col2, bool vertical) {
        int x1 = x < 0 ? 0 : x;
        int y1 = y < 0 ? 0 : y;
        int x2 = x + w > (int)width ? width : x + w;
        int y2 = y + h > (int)height ? height : y + h;

        int r1 = (col1 >> 16) & 0xFF, g1 = (col1 >> 8) & 0xFF, b1 = col1 & 0xFF;
        int r2 = (col2 >> 16) & 0xFF, g2 = (col2 >> 8) & 0xFF, b2 = col2 & 0xFF;

        for (int py = y1; py < y2; py++) {
            uint32_t* row = &target[py * width];
            float t = vertical ? (float)(py - y) / (float)h : 0.0f;
            for (int px = x1; px < x2; px++) {
                if (!vertical) t = (float)(px - x) / (float)w;
                int r = (int)(r1 + t * (r2 - r1));
                int g = (int)(g1 + t * (g2 - g1));
                int b = (int)(b1 + t * (b2 - b1));
                row[px] = (r << 16) | (g << 8) | b;
            }
        }
    }
}

// Заглушки для аудио стека (совместимость)
void api_preload_boot_sound(void) {}
void api_try_boot_sound(void) {}
void api_tick_audio(void) {}
bool play_wav_file(const char *filename) {
    (void)filename;
    return true;
}

// --- СТАРАЯ РЕАЛИЗАЦИЯ ШАДОУ И БЛЮРА (С КУЧЕЙ ОПТИМИЗАЦИЙ) ОСТАЕТСЯ БЕЗ ИЗМЕНЕНИЙ ---
static uint32_t *blur_scratch_1 = nullptr;
static uint32_t *blur_scratch_2 = nullptr;
static int scratch_w = 0, scratch_h = 0;

static void ensure_blur_buffers(int w, int h) {
    if (w <= scratch_w && h <= scratch_h && blur_scratch_1) return;
    if (blur_scratch_1) { free(blur_scratch_1); blur_scratch_1 = nullptr; }
    if (blur_scratch_2) { free(blur_scratch_2); blur_scratch_2 = nullptr; }
    
    blur_scratch_1 = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    blur_scratch_2 = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    
    if (!blur_scratch_1 || !blur_scratch_2) {
        if (blur_scratch_1) { free(blur_scratch_1); blur_scratch_1 = nullptr; }
        if (blur_scratch_2) { free(blur_scratch_2); blur_scratch_2 = nullptr; }
        scratch_w = 0; scratch_h = 0;
        return;
    }
    scratch_w = w; scratch_h = h;
}

void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb) {
    if (!draw_target || w <= 0 || h <= 0) return;
    
    int dsW = w / 4;
    int dsH = h / 4;
    if (dsW < 1) dsW = 1; if (dsH < 1) dsH = 1;
    
    ensure_blur_buffers(dsW, dsH);
    if (!blur_scratch_1) return; 

    for (int dy = 0; dy < dsH; dy++) {
        for (int dx = 0; dx < dsW; dx++) {
            int sx = x + dx * 4;
            int sy = y + dy * 4;
            if (sx >= (int)screen_w) sx = screen_w - 1;
            if (sy >= (int)screen_h) sy = screen_h - 1;
            blur_scratch_1[dy * dsW + dx] = draw_target[sy * screen_w + sx];
        }
    }

    int r_blur = 3; 
    int window = r_blur * 2 + 1;

    for (int j = 0; j < dsH; j++) {
        for (int i = 0; i < dsW; i++) {
            int r = 0, g = 0, b = 0;
            for (int k = -r_blur; k <= r_blur; k++) {
                int ix = i + k;
                if (ix < 0) ix = 0; if (ix >= dsW) ix = dsW - 1;
                uint32_t col = blur_scratch_1[j * dsW + ix];
                r += (col >> 16) & 0xFF; g += (col >> 8) & 0xFF; b += col & 0xFF;
            }
            blur_scratch_2[j * dsW + i] = ((r/window)<<16) | ((g/window)<<8) | (b/window);
        }
    }
    for (int i = 0; i < dsW; i++) {
        for (int j = 0; j < dsH; j++) {
            int r = 0, g = 0, b = 0;
            for (int k = -r_blur; k <= r_blur; k++) {
                int iy = j + k;
                if (iy < 0) iy = 0; if (iy >= dsH) iy = dsH - 1;
                uint32_t col = blur_scratch_2[iy * dsW + i];
                r += (col >> 16) & 0xFF; g += (col >> 8) & 0xFF; b += col & 0xFF;
            }
            blur_scratch_1[j * dsW + i] = ((r/window)<<16) | ((g/window)<<8) | (b/window);
        }
    }

    int alpha = (int)(amount_f * 255.0f);
    int tr = (tint_rgb >> 16) & 0xFF, tg = (tint_rgb >> 8) & 0xFF, tb = tint_rgb & 0xFF;

    for (int ty = 0; ty < h; ty++) {
        int dy = y + ty;
        if (dy < 0 || dy >= (int)screen_h) continue;
        uint32_t *row = &draw_target[dy * screen_w];

        for (int tx = 0; tx < w; tx++) {
            int dx = x + tx;
            if (dx < 0 || dx >= (int)screen_w) continue;

            if (radius > 0 && is_pixel_masked(tx, ty, w, h, radius)) continue;

            float f_dx = (float)tx / 4.0f;
            float f_dy = (float)ty / 4.0f;
            int x0 = (int)f_dx; int y0 = (int)f_dy;
            int x1 = x0 + 1; if (x1 >= dsW) x1 = dsW - 1;
            int y1 = y0 + 1; if (y1 >= dsH) y1 = dsH - 1;
            float tx_diff = f_dx - x0;
            float ty_diff = f_dy - y0;

            uint32_t c00 = blur_scratch_1[y0 * dsW + x0];
            uint32_t c10 = blur_scratch_1[y0 * dsW + x1];
            uint32_t c01 = blur_scratch_1[y1 * dsW + x0];
            uint32_t c11 = blur_scratch_1[y1 * dsW + x1];

            auto interpolate = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d, float tx, float ty) {
                return (uint8_t)((a * (1.0f - tx) * (1.0f - ty)) + (b * tx * (1.0f - ty)) + (c * (1.0f - tx) * ty) + (d * tx * ty));
            };

            int br = interpolate((c00>>16)&0xFF, (c10>>16)&0xFF, (c01>>16)&0xFF, (c11>>16)&0xFF, tx_diff, ty_diff);
            int bg = interpolate((c00>>8)&0xFF,  (c10>>8)&0xFF,  (c01>>8)&0xFF,  (c11>>8)&0xFF,  tx_diff, ty_diff);
            int bb = interpolate(c00&0xFF,        c10&0xFF,        c01&0xFF,        c11&0xFF,        tx_diff, ty_diff);

            int fr = (br * (255 - alpha) + tr * alpha) >> 8;
            int fg = (bg * (255 - alpha) + tg * alpha) >> 8;
            int fb = (bb * (255 - alpha) + tb * alpha) >> 8;

            int noise = (rand() % 4) - 2;
            fr = clamp_int(fr + noise, 0, 255);
            fg = clamp_int(fg + noise, 0, 255);
            fb = clamp_int(fb + noise, 0, 255);
            
            row[dx] = (fr << 16) | (fg << 8) | fb;
        }
    }
}

void draw_soft_shadow(int x, int y, int w, int h, int radius, int shadow_radius, float max_alpha, int offset_x, int offset_y) {
    if (!draw_target || w <= 0 || h <= 0 || shadow_radius <= 0) return;
    
    int sx = x + offset_x - shadow_radius;
    int sy = y + offset_y - shadow_radius;
    int sw = w + 2 * shadow_radius;
    int sh = h + 2 * shadow_radius;
    
    float cx = x + w / 2.0f;
    float cy = y + h / 2.0f;
    float dx = w / 2.0f - radius;
    float dy = h / 2.0f - radius;
    
    for (int py = sy; py < sy + sh; py++) {
        if (py < 0 || py >= (int)screen_h) continue;
        uint32_t* row = &draw_target[py * screen_w];
        
        for (int px = sx; px < sx + sw; px++) {
            if (px < 0 || px >= (int)screen_w) continue;
            
            float rx = fabsf(px - cx);
            float ry = fabsf(py - cy);
            float rtx = rx - dx; if (rtx < 0) rtx = 0;
            float rty = ry - dy; if (rty < 0) rty = 0;
            if (sqrtf(rtx * rtx + rty * rty) - radius < -0.5f) {
                continue;
            }
            
            float vx = fabsf((px - offset_x) - cx);
            float vy = fabsf((py - offset_y) - cy);
            float tx = vx - dx; if (tx < 0) tx = 0;
            float ty = vy - dy; if (ty < 0) ty = 0;
            
            float dist = sqrtf(tx * tx + ty * ty) - radius;
            
            if (dist < shadow_radius) {
                float t = dist / (float)shadow_radius;
                if (t < 0.0f) t = 0.0f;
                float factor = (1.0f - t * t * (3.0f - 2.0f * t)) * max_alpha;
                
                uint32_t bg = row[px];
                int br = (bg >> 16) & 0xFF;
                int bg_g = (bg >> 8) & 0xFF;
                int bb = bg & 0xFF;
                
                int a = (int)(factor * 255.0f);
                int res_r = (br * (255 - a)) >> 8;
                int res_g = (bg_g * (255 - a)) >> 8;
                int res_b = (bb * (255 - a)) >> 8;
                
                row[px] = (res_r << 16) | (res_g << 8) | res_b;
            }
        }
    }
}
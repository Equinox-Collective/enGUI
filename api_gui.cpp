#include "api_gui.h"
#include <equos.h>

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
}

#include "imgui/imgui.h"

#ifndef ImMin
#define ImMin(A, B) ((A) < (B) ? (A) : (B))
#endif
#ifndef ImMax
#define ImMax(A, B) ((A) > (B) ? (A) : (B))
#endif

// Вспомогательная функция ограничения значений, независимая от пространств имен ImGui
static inline int clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

extern uint32_t *draw_target;
extern uint32_t screen_w, screen_h;

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

void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb) {
    if (!draw_target || w <= 0 || h <= 0) return;
    
    int dsW = w / 4;
    int dsH = h / 4;
    if (dsW < 1) dsW = 1; if (dsH < 1) dsH = 1;
    
    ensure_blur_buffers(dsW, dsH);
    if (!blur_scratch_1) return; 

    // 1. Адаптивный даунсэмплинг для оптимизации CPU
    for (int dy = 0; dy < dsH; dy++) {
        for (int dx = 0; dx < dsW; dx++) {
            int sx = x + dx * 4;
            int sy = y + dy * 4;
            if (sx >= (int)screen_w) sx = screen_w - 1;
            if (sy >= (int)screen_h) sy = screen_h - 1;
            blur_scratch_1[dy * dsW + dx] = draw_target[sy * screen_w + sx];
        }
    }

    // 2. Двухпроходный фильтр Гаусса (3x3 Box Approximation)
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

    // 3. Апсэмплинг с цветовым тинированием и наложением органического шума (Noise/Dither)
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

            // Билинейное сглаживание размытых пикселей при масштабировании
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

            // Смешивание с полупрозрачной подложкой
            int fr = (br * (255 - alpha) + tr * alpha) >> 8;
            int fg = (bg * (255 - alpha) + tg * alpha) >> 8;
            int fb = (bb * (255 - alpha) + tb * alpha) >> 8;

            // Наложение мягкого пленочного шума (дизеринг цвета) для реалистичности стекла
            int noise = (rand() % 4) - 2;
            fr = clamp_int(fr + noise, 0, 255);
            fg = clamp_int(fg + noise, 0, 255);
            fb = clamp_int(fb + noise, 0, 255);
            
            row[dx] = (fr << 16) | (fg << 8) | fb;
        }
    }
}

static inline float cross_product(ImVec2 a, ImVec2 b, ImVec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// ВЫСОКОКАЧЕСТВЕННЫЙ БИЛИНИЙНЫЙ ТЕКСТУРНЫЙ СЭМПЛЕР ДЛЯ СГЛАЖИВАНИЯ ШРИФТОВ
static inline uint8_t sample_font_bilinear(const uint8_t* pixels, int tex_w, int tex_h, float u, float v) {
    if (!pixels) return 255;
    float tex_x = u * (tex_w - 1);
    float tex_y = v * (tex_h - 1);
    int x0 = (int)tex_x;
    int y0 = (int)tex_y;
    int x1 = x0 + 1; if (x1 >= tex_w) x1 = tex_w - 1;
    int y1 = y0 + 1; if (y1 >= tex_h) y1 = tex_h - 1;
    
    float dx = tex_x - x0;
    float dy = tex_y - y0;

    uint8_t p00 = pixels[y0 * tex_w + x0];
    uint8_t p10 = pixels[y0 * tex_w + x1];
    uint8_t p01 = pixels[y1 * tex_w + x0];
    uint8_t p11 = pixels[y1 * tex_w + x1];

    float val = p00 * (1.0f - dx) * (1.0f - dy) +
                p10 * dx * (1.0f - dy) +
                p01 * (1.0f - dx) * dy +
                p11 * dx * dy;
    return (uint8_t)val;
}

void api_render_imgui_data(void* draw_data_ptr) {
    ImDrawData* draw_data = (ImDrawData*)draw_data_ptr;
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* tex_pixels = nullptr;
    int tex_w = 0, tex_h = 0;
    
    io.Fonts->GetTexDataAsAlpha8(&tex_pixels, &tex_w, &tex_h);

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            ImVec4 clip = pcmd->ClipRect;
            
            for (unsigned int i = 0; i < pcmd->ElemCount; i += 3) {
                const ImDrawVert& v1 = vtx_buffer[idx_buffer[pcmd->IdxOffset + i + 0]];
                const ImDrawVert& v2 = vtx_buffer[idx_buffer[pcmd->IdxOffset + i + 1]];
                const ImDrawVert& v3 = vtx_buffer[idx_buffer[pcmd->IdxOffset + i + 2]];

                int min_x = (int)ImMin(v1.pos.x, ImMin(v2.pos.x, v3.pos.x));
                int max_x = (int)ImMax(v1.pos.x, ImMax(v2.pos.x, v3.pos.x));
                int min_y = (int)ImMin(v1.pos.y, ImMin(v2.pos.y, v3.pos.y));
                int max_y = (int)ImMax(v1.pos.y, ImMax(v2.pos.y, v3.pos.y));

                min_x = ImMax(min_x, (int)clip.x); max_x = ImMin(max_x, (int)clip.z);
                min_y = ImMax(min_y, (int)clip.y); max_y = ImMin(max_y, (int)clip.w);

                float area = cross_product(v1.pos, v2.pos, v3.pos);
                if (area == 0) continue;

                for (int py = min_y; py < max_y; py++) {
                    if (py < 0 || py >= (int)screen_h) continue;
                    uint32_t *row = &draw_target[py * screen_w];
                    for (int px = min_x; px < max_x; px++) {
                        if (px < 0 || px >= (int)screen_w) continue;
                        
                        ImVec2 p((float)px, (float)py);
                        float w1 = cross_product(v2.pos, v3.pos, p) / area;
                        float w2 = cross_product(v3.pos, v1.pos, p) / area;
                        float w3 = 1.0f - w1 - w2;

                        if (w1 >= 0 && w2 >= 0 && w3 >= 0) {
                            float tu = w1 * v1.uv.x + w2 * v2.uv.x + w3 * v3.uv.x;
                            float tv = w1 * v1.uv.y + w2 * v2.uv.y + w3 * v3.uv.y;

                            // Сэмплирование шрифта с использованием билинейного фильтра
                            uint8_t tex_alpha = sample_font_bilinear(tex_pixels, tex_w, tex_h, tu, tv);

                            ImU32 col = v1.col;
                            int vertex_alpha = (col >> 24) & 0xFF;
                            int a = (vertex_alpha * tex_alpha) / 255;
                            if (a == 0) continue;

                            int r = (col & 0xFF), g = (col >> 8) & 0xFF, b = (col >> 16) & 0xFF;

                            if (a == 255) {
                                row[px] = (r << 16) | (g << 8) | b;
                            } else {
                                uint32_t bg = row[px];
                                int br = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bb = bg & 0xFF;
                                int res_r = (r * a + br * (255 - a)) >> 8;
                                int res_g = (g * a + bg_g * (255 - a)) >> 8;
                                int res_b = (b * a + bb * (255 - a)) >> 8;
                                row[px] = (res_r << 16) | (res_g << 8) | res_b;
                            }
                        }
                    }
                }
            }
        }
    }
}

void api_tick_audio(void) {}

bool play_wav_file(const char *filename) {
    uint32_t size = 0;
    uint64_t addr = _syscall(2, (uint64_t)filename, (uint64_t)&size, 0, 0, 0);
    if (!addr) return false;
    _syscall(20, addr + 44, size - 44, 0, 0, 0);
    return true;
}

static bool boot_sound_loaded = false;

void api_preload_boot_sound(void) {
    boot_sound_loaded = true; 
}

void api_try_boot_sound(void) {
    static bool s_done = false;
    if (s_done || !boot_sound_loaded) return;
    play_wav_file("res/sysgui/BOOTSOUND.wav");
    s_done = true;
}
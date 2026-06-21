#include "api_gui.h"
#include <equos.h>

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <math.h>
}

#include "imgui/imgui.h"

// Внешние линки из main.cpp
extern uint32_t *draw_target;
extern uint32_t screen_w, screen_h;

// --- УПРАВЛЕНИЕ КЭШЕМ ДЛЯ БЛЮРА ---
static uint32_t *blur_scratch_1 = nullptr;
static uint32_t *blur_scratch_2 = nullptr;
static int scratch_w = 0, scratch_h = 0;

static void ensure_blur_buffers(int w, int h) {
    if (w <= scratch_w && h <= scratch_h && blur_scratch_1) return;
    if (blur_scratch_1) free(blur_scratch_1);
    if (blur_scratch_2) free(blur_scratch_2);
    blur_scratch_1 = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    blur_scratch_2 = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    scratch_w = w; scratch_h = h;
}

// --- ACRYLIC ENGINE: СКОРОСТНОЙ ПРОГРАММНЫЙ БЛЮР ---

// Вспомогательная функция для проверки закругления
static inline bool is_pixel_masked(int tx, int ty, int w, int h, int r) {
    if (tx < r && ty < r) { // Top-left
        int dx = r - tx, dy = r - ty;
        return (dx * dx + dy * dy > r * r);
    }
    if (tx >= w - r && ty < r) { // Top-right
        int dx = tx - (w - r - 1), dy = r - ty;
        return (dx * dx + dy * dy > r * r);
    }
    if (tx < r && ty >= h - r) { // Bottom-left
        int dx = r - tx, dy = ty - (h - r - 1);
        return (dx * dx + dy * dy > r * r);
    }
    if (tx >= w - r && ty >= h - r) { // Bottom-right
        int dx = tx - (w - r - 1), dy = ty - (h - r - 1);
        return (dx * dx + dy * dy > r * r);
    }
    return false;
}

void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb) {
    if (w <= 0 || h <= 0) return;
    
    // 1. Downsampling (x4 ускорение)
    int dsW = w / 4;
    int dsH = h / 4;
    if (dsW < 1) dsW = 1; if (dsH < 1) dsH = 1;
    ensure_blur_buffers(dsW, dsH);

    for (int dy = 0; dy < dsH; dy++) {
        for (int dx = 0; dx < dsW; dx++) {
            int sx = x + dx * 4;
            int sy = y + dy * 4;
            if (sx >= (int)screen_w) sx = screen_w - 1;
            if (sy >= (int)screen_h) sy = screen_h - 1;
            blur_scratch_1[dy * dsW + dx] = draw_target[sy * screen_w + sx];
        }
    }

    // 2. Двухпроходный Box Blur (Horizontal & Vertical)
    int r_blur = 3; // Радиус размытия
    int window = r_blur * 2 + 1;

    // Horiz
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
    // Vert
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

    // 3. Upsampling + Alpha Tint + Noise + Masking
    int alpha = (int)(amount_f * 255.0f);
    int tr = (tint_rgb >> 16) & 0xFF, tg = (tint_rgb >> 8) & 0xFF, tb = tint_rgb & 0xFF;

    for (int ty = 0; ty < h; ty++) {
        int dy = y + ty;
        if (dy < 0 || dy >= (int)screen_h) continue;
        uint32_t *row = &draw_target[dy * screen_w];

        for (int tx = 0; tx < w; tx++) {
            int dx = x + tx;
            if (dx < 0 || dx >= (int)screen_w) continue;

            // Скругление углов
            if (radius > 0 && is_pixel_masked(tx, ty, w, h, radius)) continue;

            // Билинейная выборка из блюр-буфера
            uint32_t blurred = blur_scratch_1[(ty/4) * dsW + (tx/4)];
            int br = (blurred >> 16) & 0xFF, bg = (blurred >> 8) & 0xFF, bb = blurred & 0xFF;

            // Смешивание с тинтом (Acrylic look)
            int fr = (br * (255 - alpha) + tr * alpha) >> 8;
            int fg = (bg * (255 - alpha) + tg * alpha) >> 8;
            int fb = (bb * (255 - alpha) + tb * alpha) >> 8;

            // Добавляем микро-зернистость (Noise)
            int noise = (rand() % 6) - 3;
            fr += noise; fg += noise; fb += noise;
            if (fr < 0) fr = 0; if (fr > 255) fr = 255;
            
            row[dx] = (fr << 16) | (fg << 8) | fb;
        }
    }
}

// --- ПРОГРАММНЫЙ РАСТЕРИЗАТОР DEAR IMGUI ---

static inline float cross_product(ImVec2 a, ImVec2 b, ImVec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

void api_render_imgui_data(void* draw_data_ptr) {
    ImDrawData* draw_data = (ImDrawData*)draw_data_ptr;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            
            // Клиппинг
            ImVec4 clip = pcmd->ClipRect;
            
            for (unsigned int i = 0; i < pcmd->ElemCount; i += 3) {
                const ImDrawVert& v1 = vtx_buffer[idx_buffer[pcmd->IdxOffset + i + 0]];
                const ImDrawVert& v2 = vtx_buffer[idx_buffer[pcmd->IdxOffset + i + 1]];
                const ImDrawVert& v3 = vtx_buffer[idx_buffer[pcmd->IdxOffset + i + 2]];

                // Базовый bounding box треугольника
                int min_x = (int)ImMin(v1.pos.x, ImMin(v2.pos.x, v3.pos.x));
                int max_x = (int)ImMax(v1.pos.x, ImMax(v2.pos.x, v3.pos.x));
                int min_y = (int)ImMin(v1.pos.y, ImMin(v2.pos.y, v3.pos.y));
                int max_y = (int)ImMax(v1.pos.y, ImMax(v2.pos.y, v3.pos.y));

                // Применяем клиппинг окна
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
                            // Интерполяция цвета (RGBA)
                            ImU32 col = v1.col; // Для простоты берем цвет первой вершины (или можно интерполировать)
                            int a = (col >> 24) & 0xFF;
                            if (a == 0) continue;

                            int r = (col & 0xFF), g = (col >> 8) & 0xFF, b = (col >> 16) & 0xFF;

                            if (a == 255) {
                                row[px] = (r << 16) | (g << 8) | b;
                            } else {
                                // Альфа-блендинг с фоном
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

// --- AUDIO HELPERS ---

void api_tick_audio(void) {
    // В EquinoxOS аудио-буферы обычно скармливаются ядру через syscall 20
    // Здесь будет логика стриминга из WAV-кэша, если нужно.
}

bool play_wav_file(const char *filename) {
    uint32_t size = 0;
    uint64_t addr = _syscall(2, (uint64_t)filename, (uint64_t)&size, 0, 0, 0);
    if (!addr) return false;
    _syscall(20, addr + 44, size - 44, 0, 0, 0); // Пропускаем заголовок WAV
    return true;
}
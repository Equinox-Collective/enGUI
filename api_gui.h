#ifndef API_GUI_H
#define API_GUI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void api_preload_boot_sound(void);
void api_try_boot_sound(void);
void api_tick_audio(void);
bool play_wav_file(const char *filename);

// Быстрый программный Acrylic Glass Blur
void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius = 12, uint32_t tint_rgb = 0x1F222B);

#ifdef __cplusplus
}
#endif

#endif // API_GUI_H
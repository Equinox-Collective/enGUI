#ifndef API_GUI_H
#define API_GUI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Настройки темы
struct Theme {
    uint32_t accent_color;
    uint32_t window_bg;
    uint32_t panel_bg;
    uint32_t text_primary;
    uint32_t text_secondary;
    float    blur_strength;
    int      radius;
};

extern Theme g_CurrentTheme;

// Инициализация графического ядра
void api_gui_init();

// Продвинутый блюр с эффектом зернистости (Noise) и тинтом
void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb, bool draw_border = true);

// Вспомогательные функции отрисовки (обертки над ядром для удобства)
void draw_shadow(int x, int y, int w, int h, int strength);

// Звуковой движок
void api_preload_boot_sound(void);
void api_try_boot_sound(void);
void api_tick_audio(void);
bool play_wav_file(const char *filename);

#ifdef __cplusplus
}
#endif

#endif
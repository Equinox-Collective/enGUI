#ifndef API_GUI_H
#define API_GUI_H

#include <stdint.h>
#include <stdbool.h>

// Константы оформления (Sonoma Design Language)
#define COLOR_ACCENT          0x61AFEF
#define COLOR_GLASS_TINT      0x1A1C29
#define COLOR_BORDER_LIGHT    0x4A505C
#define WINDOW_ROUNDING_LARGE 12
#define DOCK_ROUNDING         16

#ifdef __cplusplus
extern "C" {
#endif

/**
 * --- СИСТЕМНЫЙ АУДИО-СТЕК ---
 */

// Фоновая загрузка системных звуков (Boot, Click, Error)
void api_preload_boot_sound(void);
// Проигрывание приветствия (вызывается один раз при старте GUI)
void api_try_boot_sound(void);
// Тик аудио-драйвера (вызывать в главном цикле)
void api_tick_audio(void);
// Проиграть любой WAV файл
bool play_wav_file(const char *filename);


/**
 * --- ГРАФИЧЕСКИЙ ДВИЖЕК (ACRYLIC ENGINE) ---
 */

// Основная функция отрисовки "Акрилового стекла"
// amount_f: интенсивность размытия (0.0 - 1.0)
// radius: скругление углов
// tint_rgb: цвет подложки
void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb);

// Рисует мягкую тень под объектом (программная аппроксимация)
void draw_soft_shadow(int x, int y, int w, int h, int strength);

// Вспомогательная функция для маркировки грязных регионов (оптимизация VRAM)
void sysgui_mark_dirty(int x, int y, int w, int h);


/**
 * --- ИНТЕРФЕЙС РАСТЕРИЗАТОРА IMGUI ---
 */

struct SoftwareTexture {
    uint32_t* pixels;
    int width;
    int height;
};

// Инициализация текстур шрифтов для ImGui
void api_init_imgui_rasterizer(void);
// Рендеринг накопленных данных ImGui в backbuffer
void api_render_imgui_data(void* draw_data);

#ifdef __cplusplus
}
#endif

/**
 * --- КЛАССЫ ПРИЛОЖЕНИЙ (C++ Interface) ---
 */
#ifdef __cplusplus
namespace GUI {
    // Базовый класс для всех оконных приложений
    class App {
    public:
        const char* title;
        bool is_open;
        bool is_focused;

        App(const char* t) : title(t), is_open(false), is_focused(false) {}
        virtual ~App() {}

        virtual void OnRender(float dt) = 0;
        virtual void OnOpen() {}
        virtual void OnClose() {}
    };
}
#endif

#endif // API_GUI_H
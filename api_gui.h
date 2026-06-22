#ifndef API_GUI_H
#define API_GUI_H

#include <stdint.h>
#include <stdbool.h>

// --- EQUINOXOS COSMIC CYBER DESIGN SYSTEM CONSTANTS ---
#define COLOR_ACCENT          0xBD00FF  // Galactic Neon Purple / Violet
#define COLOR_ACCENT_CYAN     0x00E5FF  // Cosmic Cyan
#define COLOR_GLASS_TINT      0x030206  // Deep Space Dark Void Glass Tone
#define COLOR_BORDER_LIGHT    0x3A3F4D  
#define COLOR_BORDER_DARK     0x1A1C25  
#define COLOR_TEXT_PRIMARY    0xF0F5FF  // Cyber white
#define COLOR_TEXT_MUTED      0x708090  // Space slate gray

#define WINDOW_ROUNDING_LARGE 4
#define DOCK_ROUNDING         6
#define WIDGET_ROUNDING       4

#ifdef __cplusplus
extern "C" {
#endif

/**
 * --- СИСТЕМНЫЙ АУДИО-СТЕК ---
 */
void api_preload_boot_sound(void);
void api_try_boot_sound(void);
void api_tick_audio(void);
bool play_wav_file(const char *filename);

/**
 * --- ГРАФИЧЕСКИЙ ДВИЖЕК (ACRYLIC GLASS ENGINE) ---
 */
// Отрисовка размытия с адаптивной субдискретизацией и наложением шума (гранулярности)
void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb);
// Программный расчет мягкой тени (гауссово приближение на основе SDF)
void draw_soft_shadow(int x, int y, int w, int h, int radius, int shadow_radius, float max_alpha, int offset_x, int offset_y);
// Пометка грязных областей экрана (оптимизация обновления VRAM)
void sysgui_mark_dirty(int x, int y, int w, int h);

/**
 * --- ИНТЕРФЕЙС СГЛАЖЕННОГО РАСТЕРИЗАТОРА IMGUI ---
 */
struct SoftwareTexture {
    uint32_t* pixels;
    int width;
    int height;
};

void api_init_imgui_rasterizer(void);
// Рендеринг данных ImGui с использованием билинейной фильтрации шрифтов для борьбы с алиасингом
void api_render_imgui_data(void* draw_data);

#ifdef __cplusplus
}
#endif

/**
 * --- КЛАССЫ ПРИЛОЖЕНИЙ (C++ Object Model) ---
 */
#ifdef __cplusplus
namespace GUI {
    class App {
    public:
        const char* title;
        uint32_t instance_id; // Уникальный ID запущенной копии
        bool is_open;
        bool is_focused;

        App(const char* t, uint32_t id) : title(t), instance_id(id), is_open(false), is_focused(false) {}
        virtual ~App() {}

        virtual void OnRender(float dt) = 0;
        virtual void OnOpen() {}
        virtual void OnClose() {}
    };
}
#endif

#endif // API_GUI_H
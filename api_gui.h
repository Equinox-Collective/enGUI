#ifndef API_GUI_H
#define API_GUI_H

#include <stdint.h>
#include <stdbool.h>

// --- SONOMA DESIGN SYSTEM CONSTANTS ---
#define COLOR_ACCENT          0x007AFF  // Классический macOS Accent Blue
#define COLOR_GLASS_TINT      0x0A0C16  // Глубокий темный стеклянный тон
#define COLOR_BORDER_LIGHT    0x3A3F4D  // Светлая обводка для создания объема
#define COLOR_BORDER_DARK     0x1A1C25  // Темная контрастная граница
#define COLOR_TEXT_PRIMARY    0xF5F5F7  // Яркий контрастный текст (San Francisco style)
#define COLOR_TEXT_MUTED      0x8E8E93  // Приглушенный текст для второстепенных данных

#define WINDOW_ROUNDING_LARGE 16
#define DOCK_ROUNDING         20
#define WIDGET_ROUNDING       18

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
// Программный расчет мягкой тени (гауссово приближение)
void draw_soft_shadow(int x, int y, int w, int h, int strength);
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
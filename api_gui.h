#ifndef API_GUI_H
#define API_GUI_H

#include <stdint.h>
#include <stdbool.h>

#define COLOR_ACCENT          0xFFBD00FF  // Galactic Neon Purple / Violet
#define COLOR_ACCENT_CYAN     0xFF00E5FF  // Cosmic Cyan
#define COLOR_GLASS_TINT      0xFF030206  // Deep Space Dark Void Glass Tone
#define COLOR_TEXT_PRIMARY    0xFFF0F5FF  // Cyber white
#define COLOR_TEXT_MUTED      0xFF708090  // Slate gray

#define WINDOW_ROUNDING_LARGE 8
#define DOCK_ROUNDING         10
#define WIDGET_ROUNDING       8

#ifdef __cplusplus
extern "C" {
#endif

// --- АУДИО-СТЕК ---
void api_preload_boot_sound(void);
void api_try_boot_sound(void);
void api_tick_audio(void);
bool play_wav_file(const char *filename);

// --- ГРАФИЧЕСКИЙ ДВИЖОК ---
void draw_acrylic_blur(int x, int y, int w, int h, float amount_f, int radius, uint32_t tint_rgb);
void draw_soft_shadow(int x, int y, int w, int h, int radius, int shadow_radius, float max_alpha, int offset_x, int offset_y);
void sysgui_mark_dirty(int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace GUI {

    // Легковесный нативный рисовальщик для замены тяжелого ImDrawList
    class Painter {
    public:
        uint32_t* target;
        uint32_t width;
        uint32_t height;

        Painter(uint32_t* t, uint32_t w, uint32_t h) : target(t), width(w), height(h) {}

        void FillRect(int x, int y, int w, int h, uint32_t color);
        void FillRectAlpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
        void RoundedRect(int x, int y, int w, int h, int r, uint32_t color);
        void RoundedRectAlpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
        void DrawRect(int x, int y, int w, int h, uint32_t color, int thickness = 1);
        void DrawRoundedRect(int x, int y, int w, int h, int r, uint32_t color, int thickness = 1);
        void Line(int x1, int y1, int x2, int y2, uint32_t color, int thickness = 1);
        void Text(const char* str, int x, int y, uint32_t color);
        void Circle(int cx, int cy, int r, uint32_t color, int thickness = 1);
        void CircleFilled(int cx, int cy, int r, uint32_t color);
        void GradientRect(int x, int y, int w, int h, uint32_t col1, uint32_t col2, bool vertical);
    };

    class App {
    public:
        const char* title;
        uint32_t instance_id;
        bool is_open;
        bool is_focused;
        int x, y, w, h;
        bool dragging;
        int drag_off_x, drag_off_y;

        App(const char* t, uint32_t id, int start_x, int start_y, int start_w, int start_h) 
            : title(t), instance_id(id), is_open(false), is_focused(false),
              x(start_x), y(start_y), w(start_w), h(start_h),
              dragging(false), drag_off_x(0), drag_off_y(0) {}
              
        virtual ~App() {}

        virtual void OnRender(Painter& p, float dt) = 0;
        virtual void OnOpen() {}
        virtual void OnClose() {}
        virtual void OnKeyEvent(uint16_t key) { (void)key; }
    };
}
#endif

#endif // API_GUI_H
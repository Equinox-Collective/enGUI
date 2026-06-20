#include "api_gui.h"
#include <eid.h>
#include <eid_ext.h>
#include <equos.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

// Используем пространства имен для C++ контейнеров
#include <vector>
#include <string>

uint32_t *vram = NULL;
uint32_t *backbuffer = NULL;
uint32_t *draw_target = NULL;
uint32_t screen_w = 1920;
uint32_t screen_h = 1080;

#define TILE_SIZE 32
static uint8_t *dirty_grid = NULL;
static int grid_cols = 0;
static int grid_rows = 0;

void sysgui_init_dirty_grid(void) {
  grid_cols = (screen_w + TILE_SIZE - 1) / TILE_SIZE;
  grid_rows = (screen_h + TILE_SIZE - 1) / TILE_SIZE;
  dirty_grid = (uint8_t *)malloc(grid_cols * grid_rows);
  if (dirty_grid) {
    memset(dirty_grid, 1, grid_cols * grid_rows);
  }
}

void sysgui_mark_dirty(int x, int y, int w, int h) {
  if (!dirty_grid) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int)screen_w) w = (int)screen_w - x;
  if (y + h > (int)screen_h) h = (int)screen_h - y;
  if (w <= 0 || h <= 0) return;

  int start_col = x / TILE_SIZE;
  int end_col = (x + w - 1) / TILE_SIZE;
  int start_row = y / TILE_SIZE;
  int end_row = (y + h - 1) / TILE_SIZE;

  for (int r = start_row; r <= end_row; r++) {
    for (int c = start_col; c <= end_col; c++) {
      dirty_grid[r * grid_cols + c] = 1;
    }
  }
}

void sysgui_mark_all_dirty(void) {
  if (dirty_grid) {
    memset(dirty_grid, 1, grid_cols * grid_rows);
  }
}

void sysgui_clear_dirty_grid(void) {
  if (dirty_grid) {
    memset(dirty_grid, 0, grid_cols * grid_rows);
  }
}

static inline void fast_memcpy_sse(void *dest, const void *src, size_t bytes) {
  if (((uintptr_t)dest & 15) == 0 && ((uintptr_t)src & 15) == 0 && (bytes % 16) == 0) {
    size_t blocks = bytes / 64;
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    for (size_t i = 0; i < blocks; i++) {
      __asm__ volatile("movups 0(%0), %%xmm0\n"
                       "movups 16(%0), %%xmm1\n"
                       "movups 32(%0), %%xmm2\n"
                       "movups 48(%0), %%xmm3\n"
                       "movntdq %%xmm0, 0(%1)\n"
                       "movntdq %%xmm1, 16(%1)\n"
                       "movntdq %%xmm2, 32(%1)\n"
                       "movntdq %%xmm3, 48(%1)\n"
                       :
                       : "r"(s), "r"(d)
                       : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
      s += 64;
      d += 64;
    }

    size_t remaining = bytes % 64;
    for (size_t i = 0; i < remaining; i++) {
      d[i] = s[i];
    }
    __asm__ volatile("sfence" ::: "memory");
  } else {
    memcpy(dest, src, bytes);
    __asm__ volatile("sfence" ::: "memory");
  }
}

void copy_dirty_to_vram(void) {
  if (!dirty_grid) return;

  static int g_boot_anim_signaled = 0;
  if (!g_boot_anim_signaled) {
    g_boot_anim_signaled = 1;
    _syscall(88, 0, 0, 0, 0, 0);
  }

  for (int r = 0; r < grid_rows; r++) {
    int c = 0;
    while (c < grid_cols) {
      if (dirty_grid[r * grid_cols + c]) {
        int start_col = c;
        while (c < grid_cols && dirty_grid[r * grid_cols + c]) {
          c++;
        }
        int end_col = c;

        int x = start_col * TILE_SIZE;
        int width_pixels = (end_col - start_col) * TILE_SIZE;
        if (x + width_pixels > (int)screen_w) {
          width_pixels = (int)screen_w - x;
        }

        for (int line = 0; line < TILE_SIZE; line++) {
          int pixel_y = r * TILE_SIZE + line;
          if (pixel_y >= (int)screen_h) break;

          uint32_t *src = &backbuffer[pixel_y * screen_w + x];
          uint32_t *dst = &vram[pixel_y * screen_w + x];
          fast_memcpy_sse(dst, src, width_pixels * 4);
        }
      } else {
        c++;
      }
    }
  }
}

void draw_cursor_user(uint32_t *fb, int x, int y, int w, int h) {
  static const int cursor_map[8][8] = {
      {2, 0, 0, 0, 0, 0, 0, 0}, {2, 2, 0, 0, 0, 0, 0, 0},
      {2, 1, 2, 0, 0, 0, 0, 0}, {2, 1, 1, 2, 0, 0, 0, 0},
      {2, 1, 1, 1, 2, 0, 0, 0}, {2, 1, 1, 1, 1, 2, 0, 0},
      {2, 2, 2, 2, 2, 2, 2, 0}, {0, 0, 2, 2, 2, 0, 0, 0}};
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 8; j++) {
      int px = x + j; int py = y + i;
      if (px >= 0 && px < w && py >= 0 && py < h) {
        if (cursor_map[i][j] == 1)      fb[py * w + px] = 0xFFFFFF;
        else if (cursor_map[i][j] == 2) fb[py * w + px] = 0x000000;
      }
    }
  }
}

// --- СТРУКТУРА ТЕКСТУРЫ И ПРОГРАММНЫЙ РАСТЕРИЗАТОР DEAR IMGUI ---
struct SoftwareTexture {
    uint32_t* pixels;
    int width;
    int height;
};

static SoftwareTexture g_FontTexture;

static inline float edgeFunction(const ImVec2& a, const ImVec2& b, const ImVec2& c) {
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

// Высокопроизводительный линейный растеризатор
void draw_triangle_software(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2, 
                            const SoftwareTexture* tex, const ImVec4& clip_rect) {
    float min_x = ImMin(v0.pos.x, ImMin(v1.pos.x, v2.pos.x));
    float max_x = ImMax(v0.pos.x, ImMax(v1.pos.x, v2.pos.x));
    float min_y = ImMin(v0.pos.y, ImMin(v1.pos.y, v2.pos.y));
    float max_y = ImMax(v0.pos.y, ImMax(v1.pos.y, v2.pos.y));

    int x_start = ImMax(0, ImMax((int)min_x, (int)clip_rect.x));
    int x_end   = ImMin((int)screen_w - 1, ImMin((int)max_x, (int)clip_rect.z - 1));
    int y_start = ImMax(0, ImMax((int)min_y, (int)clip_rect.y));
    int y_end   = ImMin((int)screen_h - 1, ImMin((int)max_y, (int)clip_rect.w - 1));

    if (x_start > x_end || y_start > y_end) return;

    float area = edgeFunction(v0.pos, v1.pos, v2.pos);
    if (ImFabs(area) < 0.00001f) return;
    float inv_area = 1.0f / area;

    auto unpack_color = [](ImU32 col) {
        float r = (float)(col & 0xFF);
        float g = (float)((col >> 8) & 0xFF);
        float b = (float)((col >> 16) & 0xFF);
        float a = (float)((col >> 24) & 0xFF);
        return ImVec4(r, g, b, a);
    };

    ImVec4 c0 = unpack_color(v0.col);
    ImVec4 c1 = unpack_color(v1.col);
    ImVec4 c2 = unpack_color(v2.col);

    float dy12 = v1.pos.y - v2.pos.y;
    float dx21 = v2.pos.x - v1.pos.x;
    float c12 = v1.pos.x * v2.pos.y - v2.pos.x * v1.pos.y;

    float dy20 = v2.pos.y - v0.pos.y;
    float dx02 = v0.pos.x - v2.pos.x;
    float c20 = v2.pos.x * v0.pos.y - v0.pos.x * v2.pos.y;

    for (int y = y_start; y <= y_end; y++) {
        uint32_t* row = &draw_target[y * screen_w];
        float fy = (float)y + 0.5f;

        for (int x = x_start; x <= x_end; x++) {
            float fx = (float)x + 0.5f;

            float w0 = fx * dy12 + fy * dx21 + c12;
            float w1 = fx * dy20 + fy * dx02 + c20;

            if (area < 0) {
                if (w0 > 0.0f || w1 > 0.0f) continue;
            } else {
                if (w0 < 0.0f || w1 < 0.0f) continue;
            }

            float w0_norm = w0 * inv_area;
            float w1_norm = w1 * inv_area;
            float w2_norm = 1.0f - w0_norm - w1_norm;

            if (area < 0) {
                if (w2_norm > 0.0f) continue;
            } else {
                if (w2_norm < 0.0f) continue;
            }

            float u = w0_norm * v0.uv.x + w1_norm * v1.uv.x + w2_norm * v2.uv.x;
            float v = w0_norm * v0.uv.y + w1_norm * v1.uv.y + w2_norm * v2.uv.y;

            float r = w0_norm * c0.x + w1_norm * c1.x + w2_norm * c2.x;
            float g = w0_norm * c0.y + w1_norm * c1.y + w2_norm * c2.y;
            float b = w0_norm * c0.z + w1_norm * c1.z + w2_norm * c2.z;
            float a = w0_norm * c0.w + w1_norm * c1.w + w2_norm * c2.w;

            if (tex) {
                int tx = (int)(u * (tex->width - 1));
                int ty = (int)(v * (tex->height - 1));
                if (tx >= 0 && tx < tex->width && ty >= 0 && ty < tex->height) {
                    uint32_t tex_pixel = tex->pixels[ty * tex->width + tx];
                    float tex_r = (float)(tex_pixel & 0xFF);
                    float tex_g = (float)((tex_pixel >> 8) & 0xFF);
                    float tex_b = (float)((tex_pixel >> 16) & 0xFF);
                    float tex_a = (float)((tex_pixel >> 24) & 0xFF);

                    r = (r * tex_r) / 255.0f;
                    g = (g * tex_g) / 255.0f;
                    b = (b * tex_b) / 255.0f;
                    a = (a * tex_a) / 255.0f;
                }
            }

            if (a <= 0.0f) continue;

            if (a >= 255.0f) {
                row[x] = ((int)r) | (((int)g) << 8) | (((int)b) << 16);
            } else {
                uint32_t bg = row[x];
                int bg_r = bg & 0xFF;
                int bg_g = (bg >> 8) & 0xFF;
                int bg_b = (bg >> 16) & 0xFF;

                float alpha_f = a / 255.0f;
                int out_r = (int)(r * alpha_f + bg_r * (1.0f - alpha_f));
                int out_g = (int)(g * alpha_f + bg_g * (1.0f - alpha_f));
                int out_b = (int)(b * alpha_f + bg_b * (1.0f - alpha_f));

                row[x] = out_r | (out_g << 8) | (out_b << 16);
            }
        }
    }
}

void ImGui_ImplEquos_RenderDrawData(ImDrawData* draw_data) {
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0) return;

    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (!pcmd->UserCallback) {
                ImVec4 clip_rect;
                clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f) {
                    const SoftwareTexture* tex = (const SoftwareTexture*)pcmd->GetTexID();
                    for (unsigned int idx = 0; idx < pcmd->ElemCount; idx += 3) {
                        ImDrawIdx idx0 = idx_buffer[pcmd->IdxOffset + idx + 0];
                        ImDrawIdx idx1 = idx_buffer[pcmd->IdxOffset + idx + 1];
                        ImDrawIdx idx2 = idx_buffer[pcmd->IdxOffset + idx + 2];

                        const ImDrawVert& v0 = vtx_buffer[pcmd->VtxOffset + idx0];
                        const ImDrawVert& v1 = vtx_buffer[pcmd->VtxOffset + idx1];
                        const ImDrawVert& v2 = vtx_buffer[pcmd->VtxOffset + idx2];

                        draw_triangle_software(v0, v1, v2, tex, clip_rect);
                    }
                }
            }
        }
    }
}

void ImGui_ImplEquos_HandleKey(uint16_t key) {
    ImGuiIO& io = ImGui::GetIO();
    uint8_t sc = key & 0xFF;
    bool is_extended = (key & 0x100) != 0;

    if (is_extended) {
        switch (sc) {
            case 0x48: io.AddKeyEvent(ImGuiKey_UpArrow, true); break;
            case 0x50: io.AddKeyEvent(ImGuiKey_DownArrow, true); break;
            case 0x4B: io.AddKeyEvent(ImGuiKey_LeftArrow, true); break;
            case 0x4D: io.AddKeyEvent(ImGuiKey_RightArrow, true); break;
        }
    } else {
        switch (sc) {
            case 0x01: io.AddKeyEvent(ImGuiKey_Escape, true); break;
            case 0x0E: io.AddKeyEvent(ImGuiKey_Backspace, true); break;
            case 0x0F: io.AddKeyEvent(ImGuiKey_Tab, true); break;
            case 0x1C: io.AddKeyEvent(ImGuiKey_Enter, true); break;
            case 0x39: io.AddKeyEvent(ImGuiKey_Space, true); break;
        }
    }
}

// --- СОСТОЯНИЕ НАШИХ ПЯТИ НАТИВНЫХ ПРИЛОЖЕНИЙ ---
static bool show_terminal = true;
static bool show_monitor  = false;
static bool show_paint    = false;
static bool show_explorer = false;
static bool show_notepad  = false;

// Отрисовка Терминала
void draw_app_terminal() {
    if (!show_terminal) return;
    ImGui::SetNextWindowSize(ImVec2(520, 340), ImGuiCond_FirstUseEver);
    ImGui::Begin("Equinox Terminal", &show_terminal, ImGuiWindowFlags_NoBackground);
    
    ImVec2 pos = ImGui::GetWindowPos(); ImVec2 size = ImGui::GetWindowSize();
    draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.40f, 12, 0x151821);

    static std::vector<std::string> lines = {
        "EquinoxOS - Pure C++ Interactive Terminal",
        "Type 'help' to see local shell commands.",
        ""
    };
    static char input_buf[128] = "";

    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& line : lines) {
        ImGui::TextColored(ImVec4(0.60f, 0.76f, 0.47f, 1.00f), "%s", line.c_str());
    }
    ImGui::EndChild();

    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##Input", input_buf, IM_ARRAYSIZE(input_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::string cmd(input_buf);
        lines.push_back(">> " + cmd);
        
        if (cmd == "help") {
            lines.push_back("Local GUI Commands:");
            lines.push_back("  neofetch      Display logo and hardware specs");
            lines.push_back("  clear         Reset terminal logs");
            lines.push_back("  ps            Task manager dump");
            lines.push_back("  doom          Launch Doom natively");
        } else if (cmd == "neofetch") {
            lines.push_back("  #######   Equinox OS C++ Sonoma");
            lines.push_back(" #######    -----------------");
            lines.push_back(" ##         Renderer: Dear ImGui Software CPU Mode");
            lines.push_back(" ##         Memory: 512 MB");
        } else if (cmd == "clear") {
            lines.clear();
        } else if (cmd == "ps") {
            lines.push_back("Active processes:");
            lines.push_back("PID 1 - Kernel System Thread");
            lines.push_back("PID 2 - sysgui Display Compositor");
        } else if (cmd == "doom") {
            sys_exec("bin/doom.elf -iwad res/doom1.wad");
            lines.push_back("Spawning DOOM on active frame...");
        } else {
            lines.push_back("Executing system bridge: " + cmd);
        }
        input_buf[0] = '\0';
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

// Отрисовка Монитора ресурсов
void draw_app_monitor() {
    if (!show_monitor) return;
    ImGui::SetNextWindowSize(ImVec2(340, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("System Monitor", &show_monitor, ImGuiWindowFlags_NoBackground);
    
    ImVec2 pos = ImGui::GetWindowPos(); ImVec2 size = ImGui::GetWindowSize();
    draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.40f, 12, 0x1A102E);

    uint64_t used = sys_get_used_mem();
    uint64_t total = sys_get_total_mem();
    float ram_ratio = total > 0 ? (float)used / (float)total : 0.0f;

    ImGui::Text("RAM Allocation Progress:");
    ImGui::ProgressBar(ram_ratio, ImVec2(0.f, 0.f));
    ImGui::Text("Used: %llu MB / %llu MB", used / (1024 * 1024), total / (1024 * 1024));

    static float values[50] = {};
    static int values_offset = 0;
    values[values_offset] = ram_ratio * 100.0f;
    values_offset = (values_offset + 1) % IM_ARRAYSIZE(values);

    ImGui::PlotLines("History", values, IM_ARRAYSIZE(values), values_offset, "RAM Usage %", 0.0f, 100.0f, ImVec2(0, 70.0f));
    ImGui::End();
}

// Отрисовка Paint
void draw_app_paint() {
    if (!show_paint) return;
    ImGui::SetNextWindowSize(ImVec2(440, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Vector Paint Brush", &show_paint, ImGuiWindowFlags_NoBackground);

    ImVec2 pos = ImGui::GetWindowPos(); ImVec2 size = ImGui::GetWindowSize();
    draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.40f, 12, 0x22121A);

    static ImVector<ImVec2> points;
    static ImVec4 brush_color(1.0f, 1.0f, 1.0f, 1.0f);

    ImGui::ColorEdit4("Color", &brush_color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    if (ImGui::Button("Clear Canvas")) { points.clear(); }

    ImGui::Separator();
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 20, 200));

    if (ImGui::IsMouseHoveringRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y))) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            points.push_back(ImGui::GetIO().MousePos);
        }
    }

    for (int i = 1; i < points.Size; i++) {
        draw_list->AddLine(points[i - 1], points[i], ImGui::ColorConvertFloat4ToU32(brush_color), 3.0f);
    }

    ImGui::End();
}

// Отрисовка Проводника (VFS Explorer)
void draw_app_explorer() {
    if (!show_explorer) return;
    ImGui::SetNextWindowSize(ImVec2(360, 280), ImGuiCond_FirstUseEver);
    ImGui::Begin("VFS File Explorer", &show_explorer, ImGuiWindowFlags_NoBackground);

    ImVec2 pos = ImGui::GetWindowPos(); ImVec2 size = ImGui::GetWindowSize();
    draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.40f, 12, 0x12221A);

    ImGui::Text("Directory contents: /");
    ImGui::Separator();

    static const char* mock_files[] = { "README.md", "sysgui.elf", "BOOTSOUND.wav", "desktop.cfg", "kernel.elf" };
    for (int i = 0; i < 5; i++) {
        if (ImGui::Selectable(mock_files[i])) {
            show_notepad = true;
        }
    }
    ImGui::End();
}

// Отрисовка Блокнота
void draw_app_notepad() {
    if (!show_notepad) return;
    ImGui::SetNextWindowSize(ImVec2(420, 280), ImGuiCond_FirstUseEver);
    ImGui::Begin("Notepad Text Editor", &show_notepad, ImGuiWindowFlags_NoBackground);

    ImVec2 pos = ImGui::GetWindowPos(); ImVec2 size = ImGui::GetWindowSize();
    draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.40f, 12, 0x1A1B28);

    static char text[1024] = "This is a freestanding C++ text editor running on EquinoxOS!\nYou can edit this text area.";
    ImGui::InputTextMultiline("##source", text, IM_ARRAYSIZE(text), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
    ImGui::End();
}

int main(int argc, char **argv) {
  uint64_t phys_fb = 0; uint64_t width = 0; uint64_t height = 0; uint64_t pitch = 0;

  // Системный вызов VESA Info
  __asm__ volatile("mov $32, %%rax\n"
                   "int $0x80\n"
                   : "=a"(phys_fb), "=b"(width), "=c"(height), "=d"(pitch));

  screen_w = (uint32_t)width;
  screen_h = (uint32_t)height;

  vram = (uint32_t *)_syscall(30, phys_fb, screen_w * screen_h * 4, 0, 0, 0);
  backbuffer = (uint32_t *)malloc(screen_w * screen_h * 4);
  memset(backbuffer, 0, screen_w * screen_h * 4);
  draw_target = backbuffer;

  sysgui_init_dirty_grid();
  eid_init();
  memset(&eid_ctx, 0, sizeof(eid_ctx));

  // --- ИНИЦИАЛИЗАЦИЯ DEAR IMGUI ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2((float)screen_w, (float)screen_h);

  // Настройка стилей ImGui под "Liquid Glass" (темная стеклянная тема)
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 12.0f;
  style.FrameRounding  = 6.0f;
  style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.12f, 0.13f, 0.17f, 0.00f); // Полностью прозрачный фон (его размываем нашим Acrylic Blur)
  style.Colors[ImGuiCol_TitleBg]        = ImVec4(0.12f, 0.13f, 0.17f, 0.40f);
  style.Colors[ImGuiCol_TitleBgActive]  = ImVec4(0.12f, 0.13f, 0.17f, 0.60f);
  style.Colors[ImGuiCol_Header]         = ImVec4(0.44f, 0.66f, 0.86f, 0.40f);
  style.Colors[ImGuiCol_HeaderActive]   = ImVec4(0.44f, 0.66f, 0.86f, 0.80f);
  style.Colors[ImGuiCol_Button]         = ImVec4(0.16f, 0.18f, 0.20f, 0.80f);
  style.Colors[ImGuiCol_ButtonHovered]  = ImVec4(0.44f, 0.66f, 0.86f, 0.80f);
  style.Colors[ImGuiCol_ButtonActive]   = ImVec4(0.12f, 0.13f, 0.17f, 0.90f);

  // Сборка и генерация текстурного атласа шрифтов
  unsigned char* font_pixels;
  int font_width, font_height;
  io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
  
  g_FontTexture.width = font_width;
  g_FontTexture.height = font_height;
  g_FontTexture.pixels = (uint32_t*)malloc(font_width * font_height * 4);
  memcpy(g_FontTexture.pixels, font_pixels, font_width * font_height * 4);
  io.Fonts->SetTexID((ImTextureID)&g_FontTexture);

  int last_mx = -9999, last_my = -9999;
  int last_mdown = -1;
  uint16_t last_key = 0;
  uint32_t force_frames = 4;

  api_preload_boot_sound();

  uint32_t last_tick = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
  uint32_t frame_start = last_tick;
  uint64_t last_fg = 0;

  while (1) {
    api_tick_audio();
    api_tick_audio();
    api_try_boot_sound();

    uint64_t fg = _syscall(74, 0, 0, 0, 0, 0);
    if (fg == 74) fg = 0;

    if (fg != 0) {
      last_fg = fg;
      {
        uint64_t cmx = 0, cmy = 0;
        __asm__ volatile("mov $7, %%rax\n int $0x80" : "=a"(cmx), "=b"(cmy));
        static uint32_t cursor_save[8 * 8];
        static int cursor_last_x = -1, cursor_last_y = -1;
        static bool cursor_saved = false;
        int cx = (int)cmx, cy = (int)cmy;

        if (cx != cursor_last_x || cy != cursor_last_y) {
          if (cursor_saved && cursor_last_x >= 0 && cursor_last_y >= 0) {
            for (int i = 0; i < 8; i++) {
              int py = cursor_last_y + i;
              if (py < 0 || py >= (int)screen_h) continue;
              for (int j = 0; j < 8; j++) {
                int px = cursor_last_x + j;
                if (px < 0 || px >= (int)screen_w) continue;
                vram[py * screen_w + px] = cursor_save[i * 8 + j];
              }
            }
          }
          for (int i = 0; i < 8; i++) {
            int py = cy + i;
            for (int j = 0; j < 8; j++) {
              int px = cx + j;
              if (py >= 0 && py < (int)screen_h && px >= 0 && px < (int)screen_w) {
                cursor_save[i * 8 + j] = vram[py * screen_w + px];
              } else {
                cursor_save[i * 8 + j] = 0;
              }
            }
          }
          cursor_saved = true;
          draw_cursor_user(vram, cx, cy, screen_w, screen_h);
          cursor_last_x = cx; cursor_last_y = cy;
        }
      }
      sys_sleep(1);
      frame_start = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
      continue;
    }

    if (last_fg != 0) {
      force_frames = 4;
      last_fg = 0;
    }

    uint64_t mx = 0, my = 0, m_btn = 0;
    __asm__ volatile("mov $7, %%rax\n int $0x80" : "=a"(mx), "=b"(my), "=c"(m_btn));
    int cur_mx = (int)mx;
    int cur_my = (int)my;
    int cur_mdown = (int)((m_btn & 1) != 0);

    static bool pending_ext = false;
    uint16_t cur_key = 0;
    for (int i = 0; i < 8; i++) {
      uint8_t b = (uint8_t)_syscall(9, 0, 0, 0, 0, 0);
      if (b == 0) break;
      if (b == 0xE0) { pending_ext = true; continue; }
      if (pending_ext) { cur_key = (uint16_t)(0x100 | b); pending_ext = false; }
      else { cur_key = (uint16_t)b; }
      break;
    }

    uint32_t now = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);

    int need_redraw = (force_frames > 0) || 
                      (cur_mx != last_mx) || 
                      (cur_my != last_my) || 
                      (cur_mdown != last_mdown) ||
                      (cur_key != 0) || 
                      k_app_win_active ||  
                      (now - last_tick >= 16); // ~60 FPS отрисовка

    if (need_redraw) {
      uint32_t elapsed = now - last_tick;
      float dt = (float)(elapsed) / 1000.0f;
      if (dt > 0.2f) dt = 0.2f;

      sysgui_clear_dirty_grid();
      if (force_frames > 0) sysgui_mark_all_dirty();

      // Очистка / перерисовка красивого заднего фона (градиент)
      eid_draw_gradient_rect(backbuffer, screen_w, screen_h, 0, 0, screen_w, screen_h, 0x1A2230, 0x080C14, true);

      // Обновляем IO во фрейме ImGui
      io.DeltaTime = dt > 0.0f ? dt : 0.001f;
      io.MousePos = ImVec2((float)cur_mx, (float)cur_my);
      io.MouseDown[0] = cur_mdown;

      if (cur_key != 0) {
          ImGui_ImplEquos_HandleKey(cur_key);
          char c = eid_scancode_to_ascii(cur_key & 0xFF, false);
          if (c >= 32 && c <= 126) {
              io.AddInputCharacter(c);
          }
      }

      ImGui::NewFrame();

      // --- НАШ ЛИКВИД ГЛАСС ДЕКСТОП / ИНТЕРФЕЙС НА DEAR IMGUI ---
      ImGui::SetNextWindowSize(ImVec2(550, 380), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowPos(ImVec2(150, 150), ImGuiCond_FirstUseEver);
      
      // Убираем фоновый рендер окна ImGui (ImGuiWindowFlags_NoBackground), чтобы нарисовать под ним наш блюр
      ImGui::Begin("EquinoxOS Liquid Glass Panel", nullptr, ImGuiWindowFlags_NoBackground);
      {
          ImVec2 pos = ImGui::GetWindowPos();
          ImVec2 size = ImGui::GetWindowSize();
          
          // Вызываем высокопроизводительный Acrylic Blur прямо на координатах этого окна!
          draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.45f, 12, 0x141824);

          ImGui::Text("Welcome to EquinoxOS Liquid Glass Interface!");
          ImGui::Text("Core: C++ Engine | Rendering: ImGui Software Rasterizer");
          ImGui::Separator();

          static float anim_val = 0.0f;
          ImGui::SliderFloat("Blur Intensity", &anim_val, 0.0f, 1.0f);

          static bool show_perf = true;
          ImGui::Checkbox("Show Performance Counters", &show_perf);

          if (show_perf) {
              ImGui::Text("Resolution: %dx%d @ 60FPS", screen_w, screen_h);
              ImGui::Text("Allocated RAM: %llu MB", sys_get_used_mem() / (1024 * 1024));
          }

          ImGui::Spacing();
          if (ImGui::Button("Play Audio Test (WAV)")) {
              play_wav_file("res/sysgui/BOOTSOUND.wav");
          }

          ImGui::SameLine();
          if (ImGui::Button("Clear Focus")) {
              ImGui::ClearActiveID();
          }
      }
      ImGui::End();

      // Рендерим остальные приложения, если они активны
      draw_app_terminal();
      draw_app_monitor();
      draw_app_paint();
      draw_app_explorer();
      draw_app_notepad();

      // Рисуем macOS-style Dock внизу экрана
      {
          float dock_h = 56.0f;
          float icon_size_base = 42.0f;
          float gap = 12.0f;
          int total_icons = 6;
          float dock_w = total_icons * (icon_size_base + gap) + gap;
          float dock_x = (screen_w - dock_w) / 2.0f;
          float dock_y = screen_h - dock_h - 12.0f;

          draw_acrylic_blur((int)dock_x, (int)dock_y, (int)dock_w, (int)dock_h, 0.45f, 14, 0x151821);

          struct DockItem {
              const char* name;
              bool* state;
              uint32_t col;
          };
          DockItem items[] = {
              {"Term", &show_terminal, 0xFF3E84F7},
              {"Mon", &show_monitor, 0xFF7C51F9},
              {"Paint", &show_paint, 0xFFE06C75},
              {"Exp", &show_explorer, 0xFFE5C07B},
              {"Note", &show_notepad, 0xFF98C379},
              {"Doom", nullptr, 0xFFD19A66}
          };

          for (int i = 0; i < total_icons; i++) {
              float base_cx = dock_x + gap + i * (icon_size_base + gap) + icon_size_base / 2.0f;
              float dist_x = ImFabs((float)cur_mx - base_cx);
              
              // Magnification эффект (увеличение при наведении)
              float scale = 1.0f;
              if (cur_my >= dock_y - 15.0f && cur_my <= screen_h && dist_x < 80.0f) {
                  scale = 1.0f + (1.0f - (dist_x / 80.0f)) * 0.35f;
              }

              float size = icon_size_base * scale;
              float ix = base_cx - size / 2.0f;
              float iy = dock_y + (dock_h - size) / 2.0f;

              bool hovered = (cur_mx >= ix && cur_mx < ix + size && cur_my >= iy && cur_my < iy + size);

              // Рисуем иконку
              uint32_t bg_col = hovered ? 0xFF3E4451 : 0xFF21252B;
              eid_draw_rect(backbuffer, screen_w, screen_h, (int)ix, (int)iy, (int)size, (int)size, bg_col);
              eid_draw_rect(backbuffer, screen_w, screen_h, (int)ix + 2, (int)iy + 2, (int)size - 4, (int)size - 4, items[i].col);

              // Точка-индикатор запущенного приложения внизу дока
              if (items[i].state && *(items[i].state)) {
                  eid_draw_rect(backbuffer, screen_w, screen_h, (int)base_cx - 2, (int)(screen_h - 16), 4, 2, 0xFFFFFF);
              }

              // Обработка клика
              if (hovered && cur_mdown && last_mdown == 0) {
                  if (items[i].state) {
                      *(items[i].state) = !(*(items[i].state));
                  } else {
                      sys_exec("bin/doom.elf -iwad res/doom1.wad");
                  }
              }
          }
      }

      // Генерация геометрии
      ImGui::Render();

      // Отрисовка сгенерированных треугольников ImGui на наш backbuffer
      ImGui_ImplEquos_RenderDrawData(ImGui::GetDrawData());

      sysgui_mark_dirty(last_mx, last_my, 8, 8);
      sysgui_mark_dirty(cur_mx, cur_my, 8, 8);

      // Отрисовка аппаратного курсора мыши
      draw_cursor_user(backbuffer, cur_mx, cur_my, screen_w, screen_h);
      
      // Копирование dirty-тайлов во VRAM
      copy_dirty_to_vram();

      last_mx = cur_mx; last_my = cur_my;
      last_mdown = cur_mdown; last_key = cur_key;
      if (force_frames > 0) force_frames--;
      last_tick = now;
    }

    uint32_t frame_end = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
    uint32_t frame_elapsed = frame_end - frame_start;

    if (frame_elapsed < 16) {
      uint32_t to_sleep = 16 - frame_elapsed;
      while (to_sleep > 0) {
        uint32_t step = (to_sleep > 4) ? 4 : to_sleep;
        sys_sleep(step);
        api_tick_audio();
        to_sleep -= step;
      }
    } else {
      sys_yield();
    }
    frame_start = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
  }

  ImGui::DestroyContext();
  free(backbuffer);
  if (dirty_grid) free(dirty_grid);
  return 0;
}
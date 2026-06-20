#include "api_gui.h"
#include "gui/desktop.h"
#include "gui/panel.h"
#include "gui/dock.h"
#include "gui/win_manager.h"
#include <eid.h>
#include <eid_ext.h>
#include <equos.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

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

// --- ПРОГРАММНЫЙ РАСТЕРИЗАТОР DEAR IMGUI ---
struct SoftwareTexture {
    uint32_t* pixels;
    int width;
    int height;
};

static SoftwareTexture g_FontTexture;

static inline float edgeFunction(const ImVec2& a, const ImVec2& b, const ImVec2& c) {
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

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

    for (int y = y_start; y <= y_end; y++) {
        uint32_t* row = &draw_target[y * screen_w];
        for (int x = x_start; x <= x_end; x++) {
            ImVec2 p((float)x + 0.5f, (float)y + 0.5f);
            float w0 = edgeFunction(v1.pos, v2.pos, p);
            float w1 = edgeFunction(v2.pos, v0.pos, p);
            float w2 = edgeFunction(v0.pos, v1.pos, p);

            if (area < 0) {
                if (w0 > 0 || w1 > 0 || w2 > 0) continue;
            } else {
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            }

            w0 *= inv_area;
            w1 *= inv_area;
            float w2_norm = 1.0f - w0 - w1;

            float u = w0 * v0.uv.x + w1 * v1.uv.x + w2_norm * v2.uv.x;
            float v = w0 * v0.uv.y + w1 * v1.uv.y + w2_norm * v2.uv.y;

            float r = w0 * c0.x + w1 * c1.x + w2_norm * c2.x;
            float g = w0 * c0.y + w1 * c1.y + w2_norm * c2.y;
            float b = w0 * c0.z + w1 * c1.z + w2_norm * c2.z;
            float a = w0 * c0.w + w1 * c1.w + w2_norm * c2.w;

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

                int out_r = (int)((r * a + bg_r * (255.0f - a)) / 255.0f);
                int out_g = (int)((g * a + bg_g * (255.0f - a)) / 255.0f);
                int out_b = (int)((b * a + bg_b * (255.0f - a)) / 255.0f);

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

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  uint64_t phys_fb = 0; uint64_t width = 0; uint64_t height = 0; uint64_t pitch = 0;

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

  // --- ИНИЦИАЛИЗАЦИЯ И СЕТАП МОДУЛЕЙ GUI ---
  GUI::InitDesktop();
  GUI::InitDock();
  GUI::InitWindowManager();

  // ИНИЦИАЛИЗАЦИЯ DEAR IMGUI
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2((float)screen_w, (float)screen_h);

  // Стили ImGui под стеклянное скругление macOS
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 12.0f;
  style.FrameRounding  = 6.0f;
  style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.12f, 0.14f, 0.18f, 0.00f); // Окна прозрачны (бэкграунд размывает Акрил)
  style.Colors[ImGuiCol_TitleBg]        = ImVec4(0.12f, 0.14f, 0.18f, 0.40f);
  style.Colors[ImGuiCol_TitleBgActive]  = ImVec4(0.12f, 0.14f, 0.18f, 0.60f);

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
  bool start_menu_open = false;

  api_preload_boot_sound();

  uint32_t last_tick = (uint32_t)_syscall(6, 0, 0, 0, 0, 0);
  uint32_t frame_start = last_tick;

  while (1) {
    api_tick_audio();
    api_try_boot_sound();

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
                      (now - last_tick >= 16);

    if (need_redraw) {
      uint32_t elapsed = now - last_tick;
      float dt = (float)(elapsed) / 1000.0f;
      if (dt > 0.2f) dt = 0.2f;

      sysgui_clear_dirty_grid();
      if (force_frames > 0) sysgui_mark_all_dirty();

      // Обновляем состояние Desktop
      GUI::UpdateDesktop(dt, cur_mx, cur_my, cur_mdown, cur_key);

      // Шаг 1: Рендерим обои / скринсейвер
      GUI::RenderDesktop();

      if (!GUI::IsScreensaverActive()) {
          // Инициализируем кадр ImGui
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

          // Шаг 2: Отрисовка всех C++ окон
          GUI::RenderWindows(cur_mx, cur_my, cur_mdown, dt);

          // Шаг 3: Меню "EQ" (Системное выпадающее меню)
          if (start_menu_open) {
              ImGui::SetNextWindowPos(ImVec2(10, 28));
              ImGui::SetNextWindowSize(ImVec2(240, 120));
              ImGui::Begin("AppleMenu", &start_menu_open, ImGuiWindowFlags_NoDecoration);
              {
                  ImVec2 pos = ImGui::GetWindowPos();
                  ImVec2 size = ImGui::GetWindowSize();
                  draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.65f, 10, 0x1E222B);

                  if (ImGui::Button("Switch Sonoma Theme", ImVec2(220, 24))) {
                      GUI::NextTheme();
                      start_menu_open = false;
                  }
                  if (ImGui::Button("Open Terminal Console", ImVec2(220, 24))) {
                      GUI::OpenAppWindow("Terminal Console");
                      start_menu_open = false;
                  }
                  ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "  Active Tasks: %d", (int)_syscall(70, 0, 0, 0, 0, 0));
              }
              ImGui::End();
          }

          // Шаг 4: Верхняя строка панелей меню (Panel)
          GUI::RenderTopPanel(start_menu_open);

          // Шаг 5: Док-панель macOS-Style
          GUI::RenderDock(cur_mx, cur_my, cur_mdown);

          // Рендерим и собираем геометрию ImGui в буфер
          ImGui::Render();
          ImGui_ImplEquos_RenderDrawData(ImGui::GetDrawData());
      }

      sysgui_mark_dirty(last_mx, last_my, 8, 8);
      sysgui_mark_dirty(cur_mx, cur_my, 8, 8);

      // Накладываем курсор
      draw_cursor_user(backbuffer, cur_mx, cur_my, screen_w, screen_h);
      
      // Копируем на экран
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
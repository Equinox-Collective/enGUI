#include "win_manager.h"
#include "../api_gui.h"
#include "../imgui/imgui.h"
#include <equos.h>

extern "C" {
#include <stdlib.h>
#include <string.h>
}

extern uint32_t screen_w, screen_h;
const char* g_ActiveWindowTitle = nullptr;

namespace GUI {
    static WindowState g_Windows[] = {
        { "Terminal Console",   false, false },
        { "System Monitor",     false, false },
        { "VFS File Explorer",  false, false },
        { "Vector Paint Brush", false, false },
        { "Notepad Editor",     false, false },
        { "Doom (Ring 3)",      false, false }
    };
    static const int WINDOWS_COUNT = sizeof(g_Windows) / sizeof(WindowState);

    // Собственный безопасный хелпер дублирования строк
    static char* local_strdup(const char* s) {
        size_t len = strlen(s) + 1;
        char* d = (char*)malloc(len);
        if (d) {
            memcpy(d, s, len);
        }
        return d;
    }

    void InitWindowManager() {}

    void OpenAppWindow(const char* title) {
        if (!title) return;
        for (int i = 0; i < WINDOWS_COUNT; i++) {
            if (strcmp(g_Windows[i].title, title) == 0) {
                g_Windows[i].active = true;
                g_Windows[i].minimized = false;
                g_ActiveWindowTitle = g_Windows[i].title;
                break;
            }
        }
    }

    bool IsAppActive(const char* title) {
        if (!title) return false;
        for (int i = 0; i < WINDOWS_COUNT; i++) {
            if (strcmp(g_Windows[i].title, title) == 0) {
                return g_Windows[i].active;
            }
        }
        return false;
    }

    // --- ОТРИСОВКА ВНУТРЕННИХ ПРИЛОЖЕНИЙ (БЕЗ ИСПОЛЬЗОВАНИЯ STL) ---

    static void DrawTerminal() {
        // Статический буфер строк лога вместо std::vector
        static const char* log_lines[128] = {
            "EquinoxOS Modular C++ Terminal Console v1.0",
            "Type 'help' to see local GUI system utilities.",
            ""
        };
        static int log_size = 3;
        static char input_buf[128] = "";

        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (int i = 0; i < log_size; i++) {
            if (log_lines[i]) {
                ImGui::TextUnformatted(log_lines[i]);
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        
        auto append_log = [](const char* text) {
            if (log_size < 128) {
                log_lines[log_size++] = local_strdup(text);
            } else {
                // Кольцевой сдвиг лога при переполнении
                free((void*)log_lines[0]);
                for (int idx = 1; idx < 128; idx++) {
                    log_lines[idx - 1] = log_lines[idx];
                }
                log_lines[127] = local_strdup(text);
            }
        };

        bool reclaim_focus = false;
        if (ImGui::InputText("Input", input_buf, sizeof(input_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (input_buf[0] != '\0') {
                append_log(input_buf);
                
                // Простая командная логика
                if (strcmp(input_buf, "help") == 0) {
                    append_log("  help       Show this help window");
                    append_log("  clear      Clear screen buffer");
                    append_log("  neofetch   Display system specifications");
                } else if (strcmp(input_buf, "clear") == 0) {
                    for (int i = 0; i < log_size; i++) {
                        if (log_lines[i]) free((void*)log_lines[i]);
                        log_lines[i] = nullptr;
                    }
                    log_size = 0;
                } else if (strcmp(input_buf, "neofetch") == 0) {
                    append_log("  #######   EquinoxOS Ring 3 Terminal");
                    append_log("  #######   ------------------------");
                    append_log("  ##        Graphics: ImGui Software Renderer");
                    append_log("  ##        Blur: 4x SSE-Box-Blur Acrylic Engine");
                } else {
                    append_log("  Unknown command. Try 'help'.");
                }
                
                input_buf[0] = '\0';
                reclaim_focus = true;
            }
        }
        ImGui::SetItemDefaultFocus();
        if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);
    }

    static void DrawMonitor() {
        ImGui::Text("Active Ring 3 Process Tree");
        ImGui::Separator();

        static float ram_hist[50] = {};
        static int offset = 0;
        ram_hist[offset] = (float)sys_get_used_mem() / (1024.0f * 1024.0f);
        offset = (offset + 1) % 50;

        ImGui::PlotLines("Memory Usage (MB)", ram_hist, 50, offset, nullptr, 0.0f, 256.0f, ImVec2(0, 80));
    }

    static void DrawExplorer() {
        ImGui::Text("VFS Root Directory Listing:");
        ImGui::Separator();
        ImGui::Text("  [File]  res/BOOTSOUND.wav  (423 KB) -> Disk Storage");
        ImGui::Text("  [File]  bin/sysgui.elf     (345 KB) -> Sys Binaries");
        ImGui::Text("  [File]  NOTES.TXT          (2 KB)   -> User Buffer");
    }

    static void DrawPaint() {
        // Статический буфер точек рисования вместо std::vector
        static ImVec2 paint_points[1024];
        static int paint_points_count = 0;

        ImGui::Text("Hold Left Mouse Button to Paint on Canvas:");
        ImGui::Separator();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();

        draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 0xFF14161D, 4.0f);

        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            ImVec2 m_pos = ImGui::GetMousePos();
            if (m_pos.x >= canvas_pos.x && m_pos.x < canvas_pos.x + canvas_size.x &&
                m_pos.y >= canvas_pos.y && m_pos.y < canvas_pos.y + canvas_size.y) {
                if (paint_points_count < 1024) {
                    paint_points[paint_points_count++] = m_pos;
                }
            }
        }

        for (int i = 1; i < paint_points_count; i++) {
            draw_list->AddLine(paint_points[i-1], paint_points[i], 0xFF61AFEF, 2.0f);
        }

        if (ImGui::Button("Clear Canvas")) {
            paint_points_count = 0;
        }
    }

    static void DrawNotepad() {
        static char text[1024] = "This is a simple notepad file. Type your thoughts here!\nBuilt on C++ and Dear ImGui.";
        ImGui::InputTextMultiline("##source", text, sizeof(text), ImVec2(-FLT_MIN, -ImGui::GetFrameHeightWithSpacing()));
    }

    void RenderWindows(int mx, int my, bool mdown, float dt) {
        (void)mx; (void)my; (void)mdown; (void)dt;
        
        bool any_focused = false;

        for (int i = 0; i < WINDOWS_COUNT; i++) {
            WindowState& win = g_Windows[i];
            if (!win.active || win.minimized) continue;

            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            
            ImGui::Begin(win.title, &win.active, ImGuiWindowFlags_NoBackground);
            {
                if (ImGui::IsWindowFocused()) {
                    g_ActiveWindowTitle = win.title;
                    any_focused = true;
                }

                ImVec2 pos = ImGui::GetWindowPos();
                ImVec2 size = ImGui::GetWindowSize();

                draw_acrylic_blur((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, 0.45f, 12, 0x1A1E29);

                if (strcmp(win.title, "Terminal Console") == 0) DrawTerminal();
                else if (strcmp(win.title, "System Monitor") == 0) DrawMonitor();
                else if (strcmp(win.title, "VFS File Explorer") == 0) DrawExplorer();
                else if (strcmp(win.title, "Vector Paint Brush") == 0) DrawPaint();
                else if (strcmp(win.title, "Notepad Editor") == 0) DrawNotepad();
                else if (strcmp(win.title, "Doom (Ring 3)") == 0) {
                    ImGui::Text("Doom is rendering out-of-process.");
                    ImGui::Text("Overlaying VRAM buffer directly inside container...");
                    
                    _syscall(36, (int)pos.x, (int)pos.y + 24, (int)size.x, (int)size.y - 24, 0);
                }
            }
            ImGui::End();
        }

        if (!any_focused) {
            g_ActiveWindowTitle = "Desktop";
        }
    }
}
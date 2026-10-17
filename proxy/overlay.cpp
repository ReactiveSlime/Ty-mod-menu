#include "overlay.h"
#include "log.h"
#include "noclip.h"
#include "health.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"

namespace tymod {
    static bool s_inited = false;
    static bool s_open = false;

    bool OverlayIsOpen() { return s_open; }
    void OverlayToggle() { s_open = !s_open; }

    static void BuildMenu()
    {
        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("TyMod", &s_open))
        {
            ImGui::TextUnformatted("TY the Tasmanian Tiger");
            ImGui::Separator();
            ImGui::Checkbox("No-Clip", &g_noclipEnabled);
            ImGui::Checkbox("Infinite Health", &g_infiniteHealth);
            ImGui::SliderFloat("Speed", &g_speed, 50.0f, 5000.0f, "%.0f u/s");

            float p[3];
            NoClipGetPos(p);
            ImGui::Text("Pos  %.1f, %.1f, %.1f", p[0], p[1], p[2]);

            ImGui::Separator();
            ImGui::TextDisabled("WASD move, Space/Ctrl up/down");
            ImGui::TextDisabled("Shift = fast, E/Q turn heading");
            ImGui::TextDisabled("INSERT toggles this menu");
            ImGui::TextDisabled("Hold Shift/Alt to play while open");
        }
        ImGui::End();
    }

    bool OverlayInit(void* hwnd)
    {
        if (s_inited) return true;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplWin32_Init(hwnd)) { Log("ImGui_ImplWin32_Init failed"); return false; }
        if (!ImGui_ImplOpenGL3_Init(nullptr)) { Log("ImGui_ImplOpenGL3_Init failed"); return false; }

        s_inited = true;
        Log("ImGui initialized");
        return true;
    }

    void OverlayNewFrame()
    {
        if (!s_inited) return;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (s_open) BuildMenu();
        ImGui::Render();
    }

    void OverlayRender()
    {
        if (!s_inited) return;
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

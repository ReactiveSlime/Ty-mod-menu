#include "overlay.h"
#include "log.h"
#include "noclip.h"
#include "health.h"
#include "cheats.h"
#include "esp.h"
#include "settings.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"

#include <windows.h>
#include <string.h>
#include <stdio.h>

namespace tymod {
    static bool s_inited = false;
    static bool s_open = false;
    static int  s_warpIndex = 0;

    // Key binding state: index of the hotkeys slot waiting for a key, or -1.
    // Captured by polling the raw key state each frame (independent of the
    // ImGui key system / the game's input swallowing).
    static int s_binding = -1;
    static bool s_vkPrev[256] = {};
    static void UpdateKeyBinding()
    {
        if (s_binding < 0)
        {
            memset(s_vkPrev, 0, sizeof(s_vkPrev));
            return;
        }
        for (int vk = 1; vk < 256; vk++)
        {
            bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            bool press = down && !s_vkPrev[vk];
            s_vkPrev[vk] = down;
            if (press && vk != VK_SHIFT && vk != VK_MENU && vk != VK_CONTROL)
            {
                if (vk == VK_ESCAPE) (&g_keys.menu)[s_binding] = 0;
                else                 (&g_keys.menu)[s_binding] = vk;
                s_binding = -1;
                return;
            }
        }
    }

    static void KeyBindButton(const char* label, int slot)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(100.0f);
        char btn[48];
        if (s_binding == slot)
        {
            if (ImGui::Button("press a key (ESC = clear)"))
                s_binding = -1;
        }
        else
        {
            sprintf_s(btn, sizeof(btn), "%s   ###hk%d",
                KeyName((&g_keys.menu)[slot]), slot);
            if (ImGui::Button(btn)) s_binding = slot;
        }
    }

    bool OverlayIsOpen() { return s_open; }
    void OverlayToggle() { s_open = !s_open; }

    // While open, the menu takes full control of keyboard + mouse. Holding
    // SHIFT or ALT hands control back to the game (menu stays open).
    bool OverlayControlReleased()
    {
        if (!s_open) return false;
        return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
               (GetAsyncKeyState(VK_MENU)  & 0x8000) != 0;
    }

    static void BuildMenu()
    {
        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("TyMod", &s_open))
        {
            UpdateKeyBinding();
            ImGui::TextUnformatted("TY the Tasmanian Tiger");
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Cheats"))
            {
                ImGui::Checkbox("Infinite Lives", &g_infiniteLives);
                ImGui::Checkbox("Infinite Breath", &g_infiniteBreath);
                ImGui::Checkbox("Invincible", &g_invincible);
                ImGui::Checkbox("Super Speed", &g_superSpeed);
                if (g_superSpeed)
                    ImGui::SliderFloat("Multiplier", &g_superMult, 1.0f, 10.0f, "%.1fx");
                ImGui::Checkbox("No-Clip", &g_noclipEnabled);
                ImGui::SliderFloat("Speed", &g_speed, 50.0f, 5000.0f, "%.0f u/s");
            }

            if (ImGui::CollapsingHeader("Visuals"))
            {
                ImGui::Checkbox("Set FOV", &g_fovOn);
                if (g_fovOn)
                    ImGui::SliderFloat("FOV", &g_fovY, 40.0f, 120.0f, "%.0f deg");
            }

            if (ImGui::CollapsingHeader("Hotkeys"))
            {
                KeyBindButton("Menu", 0);
                KeyBindButton("No-Clip", 1);
                KeyBindButton("ESP", 2);
                KeyBindButton("Super Speed", 3);
            }

            if (ImGui::CollapsingHeader("World"))
            {
                // Keep the combo's selection in sync with the actual level
                // whenever the level changes; otherwise it shows the pending
                // pick (so it updates before you warp, not after).
                static UINT s_lastLvl = 0xFFFFFFFF;
                UINT cur = tymod::CheatsLevelId();
                if (cur != s_lastLvl)
                {
                    s_lastLvl = cur;
                    int i = tymod::CheatsLevelIndexById(cur);
                    if (i >= 0) s_warpIndex = i;
                }
                int idx = tymod::CheatsLevelIndexById(cur);
                ImGui::Text("Current: %s",
                    idx >= 0 ? tymod::CheatsLevelName(idx) : "Menu / unknown");
                if (ImGui::BeginCombo("Level", tymod::CheatsLevelName(s_warpIndex)))
                {
                    for (int i = 0; i < tymod::CheatsLevelCount(); i++)
                    {
                        bool sel = (i == s_warpIndex);
                        if (ImGui::Selectable(tymod::CheatsLevelName(i), &sel))
                            s_warpIndex = i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                bool inLevel = tymod::CheatsInLevel();
                if (!inLevel) ImGui::BeginDisabled();
                if (ImGui::Button("Warp") && idx >= 0)
                    tymod::CheatsWarp(tymod::CheatsLevelIdOf(s_warpIndex));
                if (!inLevel) { ImGui::EndDisabled(); ImGui::TextDisabled("(in-game only)"); }
            }

            float p[3];
            NoClipGetPos(p);
            ImGui::Text("Pos  %.1f, %.1f, %.1f", p[0], p[1], p[2]);
            ImGui::Text("Lives %d   Opals %d", tymod::CheatsLives(), tymod::CheatsOpals());

            if (ImGui::CollapsingHeader("Collectible ESP"))
                EspMenu();

            ImGui::Separator();
            ImGui::TextDisabled("WASD move, Space/Ctrl up/down");
            ImGui::TextDisabled("Shift = fast, E/Q turn heading");
            ImGui::TextDisabled("INSERT toggles this menu");
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
        // ESP draws every frame (menu open or not) via the foreground draw
        // list, right before the frame is submitted.
        EspDraw();
        ImGui::Render();
    }

    void OverlayRender()
    {
        if (!s_inited) return;
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

#include "gamepad.h"
#include "log.h"
#include "overlay.h"
#include "noclip.h"

#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <MinHook.h>

#include "imgui.h"

// Controller support. TY.exe statically imports XInput9_1_0 (XInputGetState /
// XInputSetState), so hooking those two functions lets the menu capture every
// pad read the game makes. We poll through the trampoline, so our own reads
// stay real while the game sees a neutral pad.
//
//   R3 + L3        toggle the menu
//   D-pad / stick  arrow keys (ImGui moves focus; it repeats held keys itself)
//   A              activate (Enter), B = Escape
//   RB             No-Clip 3x speed (same as Shift)

typedef DWORD (WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI* XInputSetState_t)(DWORD, const XINPUT_VIBRATION*);

static XInputGetState_t o_XInputGetState = nullptr;
static XInputSetState_t o_XInputSetState = nullptr;
static XInputGetState_t s_rawGetState = nullptr;  // fallback if the hook failed

static int   s_pad = -1;            // active controller index, sticky
static DWORD s_buttons = 0;         // last real buttons of the active pad
static float s_move[3] = {0, 0, 0}; // last real movement axes (fwd, strafe, up)
static bool  s_prevCombo = false;
static bool  s_prevNav[5] = {};     // up/down/left/right/Enter edge tracking
static bool  s_prevB = false;

static DWORD WINAPI hkXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState);
static DWORD WINAPI hkXInputSetState(DWORD dwUserIndex, const XINPUT_VIBRATION* pVibration);

namespace tymod {
    // The game must not read pad movement while the menu is open (input would
    // leak into the menu) or while No-Clip is flying (the game would fight our
    // frozen position: gravity, ground audio, its own stick movement). Menu
    // open => fully neutral pad. No-Clip (menu closed) => neutral movement, but
    // the RIGHT stick is left intact so the game camera still turns (No-Clip
    // derives its heading from that camera). Our own reads use the trampoline,
    // so GamepadTick still sees the real values.
    bool GamepadCaptured() { return OverlayIsOpen() || g_noclipEnabled; }

    bool GamepadFastHeld()
    {
        return s_pad >= 0 && (s_buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    }

    void GamepadMoveAxes(float move[3])
    {
        move[0] = s_move[0];
        move[1] = s_move[1];
        move[2] = s_move[2];
    }

    void GamepadInit()
    {
        HMODULE xin = GetModuleHandleA("XInput9_1_0.dll");
        if (!xin) xin = LoadLibraryA("XInput9_1_0.dll");
        if (!xin) { Log("xinput9_1_0 not loaded, controller disabled"); return; }

        void* gs = (void*)GetProcAddress(xin, "XInputGetState");
        void* ss = (void*)GetProcAddress(xin, "XInputSetState");
        s_rawGetState = (XInputGetState_t)gs;

        if (gs && MH_CreateHook(gs, &hkXInputGetState, (void**)&o_XInputGetState) == MH_OK)
        { MH_EnableHook(gs); Log("XInputGetState hook installed @ %p", gs); }
        else Log("XInputGetState hook failed");

        if (ss && MH_CreateHook(ss, &hkXInputSetState, (void**)&o_XInputSetState) == MH_OK)
        { MH_EnableHook(ss); Log("XInputSetState hook installed @ %p", ss); }
        else Log("XInputSetState hook failed");
    }

    void GamepadTick()
    {
        XInputGetState_t fn = o_XInputGetState ? o_XInputGetState : s_rawGetState;
        if (!fn) return;
        if (!ImGui::GetCurrentContext()) return;   // overlay not up yet

        // Sticky active pad: first connected, re-picked on disconnect.
        XINPUT_STATE st = {};
        if (s_pad < 0 || fn((DWORD)s_pad, &st) != ERROR_SUCCESS)
        {
            s_pad = -1;
            for (DWORD i = 0; i < 4; i++)
            {
                if (fn(i, &st) == ERROR_SUCCESS) { s_pad = (int)i; break; }
            }
            if (s_pad < 0)
            {
                s_buttons = 0; s_prevCombo = false; s_prevB = false;
                s_move[0] = s_move[1] = s_move[2] = 0.0f;
                for (int i = 0; i < 5; i++) s_prevNav[i] = false;
                return;
            }
        }
        s_buttons = st.Gamepad.wButtons;

        // Movement axes for No-Clip flight, from the real (un-zeroed) pad read.
        // Left stick = forward/strafe; triggers = down/up. Apply a deadzone.
        {
            const float dz = (float)XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
            auto dz1 = [dz](float v) { return (v > -dz && v < dz) ? 0.0f : v / 32767.0f; };
            s_move[0] = dz1((float)st.Gamepad.sThumbLY);   // +forward
            s_move[1] = dz1((float)st.Gamepad.sThumbLX);   // +strafe right
            float rt = (float)st.Gamepad.bRightTrigger / 255.0f;  // up
            float lt = (float)st.Gamepad.bLeftTrigger  / 255.0f;  // down
            s_move[2] = (rt > 0.15f ? rt : 0.0f) - (lt > 0.15f ? lt : 0.0f);
        }

        // R3 + L3 toggles the menu (edge-triggered; XInput has no separate
        // L3/R3 flags, just the two THUMB bits).
        bool combo = (s_buttons & (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB))
                     == (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB);
        if (combo && !s_prevCombo)
        {
            OverlayToggle();
            Log("menu %s (pad)", OverlayIsOpen() ? "opened" : "closed");
        }
        s_prevCombo = combo;

        static const ImGuiKey kKeys[5] = {
            ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow,
            ImGuiKey_RightArrow, ImGuiKey_Enter
        };
        ImGuiIO& io = ImGui::GetIO();

        if (!OverlayIsOpen())
        {
            // Release any pad-held keys so ImGui doesn't see them stuck down.
            for (int i = 0; i < 5; i++)
                if (s_prevNav[i]) { io.AddKeyEvent(kKeys[i], false); s_prevNav[i] = false; }
            if (s_prevB) { io.AddKeyEvent(ImGuiKey_Escape, false); s_prevB = false; }
            return;
        }

        // Menu navigation: D-pad or left stick -> arrow keys, A -> Enter,
        // B -> Escape. ImGui moves focus and repeats held keys internally, so
        // this never touches the mouse and coexists with real mouse input.
        float sx = (float)st.Gamepad.sThumbLX;
        float sy = (float)st.Gamepad.sThumbLY;
        float mag = sqrtf(sx * sx + sy * sy);
        if (mag > 2.0f * (float)XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        {
            sx /= mag; sy /= mag;             // unit vector, sign is what matters
        }
        else { sx = 0.0f; sy = 0.0f; }        // a bit past the deadzone so a
                                              // resting thumb can't move focus
        bool nav[5];
        nav[0] = (s_buttons & XINPUT_GAMEPAD_DPAD_UP)    != 0 || sy >  0.5f;
        nav[1] = (s_buttons & XINPUT_GAMEPAD_DPAD_DOWN)  != 0 || sy < -0.5f;
        nav[2] = (s_buttons & XINPUT_GAMEPAD_DPAD_LEFT)  != 0 || sx < -0.5f;
        nav[3] = (s_buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0 || sx >  0.5f;
        nav[4] = (s_buttons & XINPUT_GAMEPAD_A)          != 0;
        for (int i = 0; i < 5; i++)
            if (nav[i] != s_prevNav[i]) { io.AddKeyEvent(kKeys[i], nav[i]); s_prevNav[i] = nav[i]; }

        bool b = (s_buttons & XINPUT_GAMEPAD_B) != 0;
        if (b != s_prevB) { io.AddKeyEvent(ImGuiKey_Escape, b); s_prevB = b; }
    }
}

// The game must not see pad movement while the menu is open or No-Clip is
// active: feed it a connected but neutral controller (a fake disconnect could
// make the game drop to its own menu or spam reconnect handling). We always
// neutralise the movement controls; the right stick is only zeroed while the
// menu is open, so the No-Clip camera still turns.
static DWORD WINAPI hkXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    DWORD r = o_XInputGetState(dwUserIndex, pState);
    if (r == ERROR_SUCCESS && pState && tymod::GamepadCaptured())
    {
        pState->Gamepad.wButtons = 0;
        pState->Gamepad.bLeftTrigger = 0;
        pState->Gamepad.bRightTrigger = 0;
        pState->Gamepad.sThumbLX = 0;
        pState->Gamepad.sThumbLY = 0;
        if (tymod::OverlayIsOpen())
        {
            pState->Gamepad.sThumbRX = 0;
            pState->Gamepad.sThumbRY = 0;
        }
    }
    return r;
}

// No rumble while the player is in the menu.
static DWORD WINAPI hkXInputSetState(DWORD dwUserIndex, const XINPUT_VIBRATION* pVibration)
{
    XINPUT_VIBRATION zero = { 0, 0 };
    if (tymod::GamepadCaptured())
        pVibration = &zero;
    return o_XInputSetState(dwUserIndex, pVibration);
}

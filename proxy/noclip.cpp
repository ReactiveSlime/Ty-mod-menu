#include "noclip.h"
#include "log.h"
#include "camera.h"

#include <windows.h>
#include <cmath>

// In-process no-clip for TY the Tasmanian Tiger.
// Ported from the external TyNoClip trainer:
//   - player position lives at TY.exe + 0x270B78 (X, Y, Z floats), Y-up
//   - a mirror copy sits at pos + 0x10 and must be written too
//   - we keep our own 'frozen' position and write it back every frame
//   - movement is relative to the game camera's heading (mouse-controlled)
//   - E / Q nudge the heading while flying; Shift = fast

namespace tymod {
    bool  g_noclipEnabled = false;
    float g_speed = 750.0f;   // units / second

    static long  s_posAddr = 0;
    static float s_frozen[3] = {0, 0, 0};
    static float s_heading = 0.0f;         // horizontal forward angle (radians)
    static float s_ref[3] = {0, 0, 0};     // position in memory at end of last frame
    static bool  s_haveRef = false;
    static bool  s_wasOn = false;
    static bool  s_prevAdd = false, s_prevSub = false;
    static HWND  s_wnd = nullptr;

    static LARGE_INTEGER s_freq{};
    static LARGE_INTEGER s_prev{};
    static bool s_timerInit = false;

    static bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    // The up axis is Y (index 1); horizontal axes are X (0) and Z (2).
    static const int H0 = 0, H1 = 2, UP = 1;

    static bool Sane(float v) { return v == v && v > -1.0e6f && v < 1.0e6f; }

    void NoClipSetWindow(void* h) { s_wnd = (HWND)h; }
    long NoClipAddress() { return s_posAddr; }

    void NoClipGetFrozen(float out[3])
    {
        if (!(g_noclipEnabled && s_wasOn) || !Sane(s_frozen[0]) || !Sane(s_frozen[1]) || !Sane(s_frozen[2]))
        {
            NoClipGetPos(out);
            return;
        }
        out[0] = s_frozen[0]; out[1] = s_frozen[1]; out[2] = s_frozen[2];
    }

    void NoClipGetPos(float out[3])
    {
        if (!s_posAddr) { out[0] = out[1] = out[2] = 0; return; }
        volatile float* p = (volatile float*)s_posAddr;
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }

    void NoClipInit()
    {
        long base = (long)(ULONG_PTR)GetModuleHandleA(nullptr);
        s_posAddr = base + 0x270B78;
        Log("NoClipInit: moduleBase=0x%X posAddr=0x%X", (unsigned)base, (unsigned)s_posAddr);
    }

    void NoClipTick()
    {
        if (!s_posAddr) return;

        if (!s_timerInit) { QueryPerformanceFrequency(&s_freq); QueryPerformanceCounter(&s_prev); s_timerInit = true; }
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - s_prev.QuadPart) / (float)s_freq.QuadPart;
        s_prev = now;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;

        volatile float* p = (volatile float*)s_posAddr;
        float pos[3] = { p[0], p[1], p[2] };
        bool focused = s_wnd ? (GetForegroundWindow() == s_wnd) : true;

        // Heading from the game camera's own forward (mouse-controlled)
        if (g_noclipEnabled)
        {
            float gf[3];
            if (CameraGetGameFwd(gf) && (gf[0]*gf[0] + gf[2]*gf[2]) > 0.0025f)
                s_heading = atan2f(gf[2], gf[0]);
        }

        if (!g_noclipEnabled)
        {
            s_ref[H0] = pos[H0]; s_ref[H1] = pos[H1]; s_ref[UP] = pos[UP];
            s_haveRef = true;
            s_wasOn = false;
            return;
        }

        if (!s_wasOn)
        {
            if (!Sane(pos[0]) || !Sane(pos[1]) || !Sane(pos[2])) return;
            for (int i = 0; i < 3; i++) s_frozen[i] = pos[i];
            s_wasOn = true;
            Log("NoClip ON at (%.1f, %.1f, %.1f)", s_frozen[0], s_frozen[1], s_frozen[2]);
        }

        if (focused)
        {
            bool a = Key(VK_ADD) || Key(VK_OEM_PLUS);
            bool s = Key(VK_SUBTRACT) || Key(VK_OEM_MINUS);
            if (a && !s_prevAdd) g_speed += 25.0f;
            if (s && !s_prevSub) g_speed = (g_speed > 35.0f) ? g_speed - 25.0f : 10.0f;
            s_prevAdd = a; s_prevSub = s;

            float step = g_speed * (Key(VK_SHIFT) ? 3.0f : 1.0f) * dt;
            float fwd  = (float)((Key('W') ? 1 : 0) - (Key('S') ? 1 : 0));
            float side = (float)((Key('D') ? 1 : 0) - (Key('A') ? 1 : 0));
            float vert = (float)((Key(VK_SPACE) ? 1 : 0) - (Key(VK_CONTROL) ? 1 : 0));

            if (Key('E')) s_heading -= 1.5f * dt;   // E turns left
            if (Key('Q')) s_heading += 1.5f * dt;   // Q turns right

            float c = cosf(s_heading), sn = sinf(s_heading);
            s_frozen[H0] += step * (fwd * c + side * sn);
            s_frozen[H1] += step * (fwd * sn - side * c);
            s_frozen[UP] += step * vert;
        }

        if (Sane(s_frozen[0]) && Sane(s_frozen[1]) && Sane(s_frozen[2]))
        {
            volatile float* dst = (volatile float*)s_posAddr;
            dst[0] = s_frozen[0]; dst[1] = s_frozen[1]; dst[2] = s_frozen[2];
            volatile float* mir = (volatile float*)(s_posAddr + 0x10);
            mir[0] = s_frozen[0]; mir[1] = s_frozen[1]; mir[2] = s_frozen[2];
            s_ref[0] = s_frozen[0]; s_ref[1] = s_frozen[1]; s_ref[2] = s_frozen[2];
            s_haveRef = true;
        }
    }
}

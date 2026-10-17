#include "mod.h"
#include "log.h"
#include "overlay.h"
#include "noclip.h"
#include "health.h"
#include "camera.h"

#include <windows.h>
#include <string.h>
#include <math.h>
#include <MinHook.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

// imgui_impl_win32.h intentionally #if 0's this declaration; declare it here.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Frame-present hook (GDI SwapBuffers) + ImGui overlay ---

typedef BOOL(WINAPI* SwapBuffers_t)(HDC);
typedef HDC(WINAPI* wglGetCurrentDC_t)();
typedef LRESULT(CALLBACK* WNDPROC_t)(HWND, UINT, WPARAM, LPARAM);
typedef PROC(WINAPI* wglGetProcAddress_t)(LPCSTR);
typedef void (APIENTRY* glUniformMatrix4fv_t)(int, int, unsigned char, const float*);
typedef void (APIENTRY* glGetIntegerv_t)(unsigned int, int*);
typedef void (APIENTRY* glUseProgram_t)(unsigned int);

typedef void (APIENTRY* glUniformMatrix4fv_t)(int, int, unsigned char, const float*);
typedef void (APIENTRY* glGetIntegerv_t)(unsigned int, int*);
typedef void (APIENTRY* glUseProgram_t)(unsigned int);
typedef void (APIENTRY* glProgramUniformMatrix4fv_t)(unsigned int, int, int, unsigned char, const float*);
typedef void (APIENTRY* glUniformMatrix3fv_t)(int, int, unsigned char, const float*);
typedef BOOL(WINAPI* GetKeyboardState_t)(PBYTE);
typedef BOOL(WINAPI* GetCursorPos_t)(LPPOINT);
typedef BOOL(WINAPI* SetCursorPos_t)(int, int);

static GetKeyboardState_t o_GetKeyboardState = nullptr;
static GetCursorPos_t     o_GetCursorPos = nullptr;
static SetCursorPos_t     o_SetCursorPos = nullptr;

static SwapBuffers_t      o_SwapBuffers = nullptr;
static wglGetCurrentDC_t  p_wglGetCurrentDC = nullptr;
static WNDPROC_t          o_WndProc = nullptr;
static wglGetProcAddress_t o_wglGetProcAddress = nullptr;
static glUniformMatrix4fv_t o_glUniformMatrix4fv = nullptr;
static glGetIntegerv_t      p_glGetIntegerv = nullptr;
static glUseProgram_t       o_glUseProgram = nullptr;
static glUseProgram_t       o_glUseProgramARB = nullptr;
static glProgramUniformMatrix4fv_t o_glProgramUniformMatrix4fv = nullptr;
static glUniformMatrix3fv_t        o_glUniformMatrix3fv = nullptr;

static bool g_u4Hooked = false;
static bool g_inLevel = false;
static int  g_u4Frame = 0;
static int  g_u4Logged = 0;
static unsigned int g_curProgram = 0;

#define RESOLVE_N 5
static void* g_resolve[RESOLVE_N] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static const char* g_glNames[RESOLVE_N] = {"glUniformMatrix4fv", "glUseProgram", "glUseProgramObjectARB",
                                           "glProgramUniformMatrix4fv", "glUniformMatrix3fv"};

// TEMP recon: log only (kind,prog,loc,matrix) uploads that CHANGE, first per
// frame, so a constant projection logs once while a moving view logs every frame.
#define U4_MAX 500
static struct { int kind; unsigned int prog; int loc; int lastFrame; float m[16]; } g_tab[U4_MAX];
static int g_tabN = 0;
bool g_recon = false;   // log matrix uploads (recon)

static void APIENTRY hkglUseProgram(unsigned int p)    { g_curProgram = p; o_glUseProgram(p); }
static void APIENTRY hkglUseProgramARB(unsigned int p) { g_curProgram = p; o_glUseProgramARB(p); }

// A rigid transform (rotation+translation, no scale) -- what a camera/view or
// a static model matrix looks like. Column-major array: last row = [3],[7],[11],[15].
static bool IsRigid(const float* v)
{
    if (fabsf(v[3]) > 1e-3f || fabsf(v[7]) > 1e-3f || fabsf(v[11]) > 1e-3f || fabsf(v[15] - 1.0f) > 1e-3f)
        return false;
    float c0 = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    float c1 = v[4]*v[4] + v[5]*v[5] + v[6]*v[6];
    float c2 = v[8]*v[8] + v[9]*v[9] + v[10]*v[10];
    if (fabsf(c0 - 1.0f) > 1e-2f || fabsf(c1 - 1.0f) > 1e-2f || fabsf(c2 - 1.0f) > 1e-2f)
        return false;
    float d01 = v[0]*v[4] + v[1]*v[5] + v[2]*v[6];
    float d02 = v[0]*v[8] + v[1]*v[9] + v[2]*v[10];
    float d12 = v[4]*v[8] + v[5]*v[9] + v[6]*v[10];
    if (fabsf(d01) > 1e-2f || fabsf(d02) > 1e-2f || fabsf(d12) > 1e-2f)
        return false;
    return true;
}

static void LogMatrix(int kind, const char* tag, unsigned int prog, int loc, int count, const float* value)
{
    if (!value || g_u4Logged >= 200000) return;
    int idx = -1;
    for (int i = 0; i < g_tabN; i++)
        if (g_tab[i].kind == kind && g_tab[i].prog == prog && g_tab[i].loc == loc) { idx = i; break; }
    if (idx < 0 && g_tabN < U4_MAX)
    {
        idx = g_tabN++;
        g_tab[idx].kind = kind; g_tab[idx].prog = prog; g_tab[idx].loc = loc; g_tab[idx].lastFrame = -1;
        for (int k = 0; k < 16; k++) g_tab[idx].m[k] = 0.0f;
    }
    if (idx < 0) return;
    if (g_tab[idx].lastFrame == g_u4Frame) return;   // first upload this frame only
    g_tab[idx].lastFrame = g_u4Frame;
    bool changed = false;
    for (int k = 0; k < 16; k++) if (g_tab[idx].m[k] != value[k]) { changed = true; break; }
    if (!changed) return;
    for (int k = 0; k < 16; k++) g_tab[idx].m[k] = value[k];
    g_u4Logged++;
    // Fingerprint: transform the player world position by this matrix. A true
    // view/view-projection maps the player to ~(0,0,-dist) (centered on screen).
    float wx = 0, wy = 0, wz = 0, ww = 1;
    float* pp = (float*)tymod::NoClipAddress();
    if (pp && (fabsf(pp[0]) + fabsf(pp[1]) + fabsf(pp[2]) > 1.0f))
    {
        wx = value[0]*pp[0] + value[4]*pp[1] + value[8]*pp[2]  + value[12];
        wy = value[1]*pp[0] + value[5]*pp[1] + value[9]*pp[2]  + value[13];
        wz = value[2]*pp[0] + value[6]*pp[1] + value[10]*pp[2] + value[14];
        ww = value[3]*pp[0] + value[7]*pp[1] + value[11]*pp[2] + value[15];
        if (fabsf(ww) < 1e-4f) ww = 1.0f;
    }
    tymod::Log("%s f=%d prog=%u loc=%d c=%d [%.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f] wp=(%.2f %.2f %.2f)/%.2f",
        tag, g_u4Frame, prog, loc, count,
        value[0], value[1], value[2], value[3],
        value[4], value[5], value[6], value[7],
        value[8], value[9], value[10], value[11],
        value[12], value[13], value[14], value[15],
        wx, wy, wz, ww);
}

// The main camera is the view-projection that (a) is non-affine (its bottom
// row is not (0,0,0,1) like a model/identity/ortho matrix) and (b) horizontally
// centers the player in clip space (|wx/w| ~ 0). Robust to camera orientation,
// unlike a v[15] magnitude test which breaks when looking up/down.
static bool IsCameraMatrix(const float* v)
{
    if (!g_inLevel) return false;
    float bxyz = fabsf(v[3]) + fabsf(v[7]) + fabsf(v[11]);
    if (bxyz < 0.5f) return false;
    float* p = (float*)tymod::NoClipAddress();
    if (!p) return false;
    if (p[0]*p[0] + p[1]*p[1] + p[2]*p[2] < 1.0f) return false;
    float ww = v[3]*p[0] + v[7]*p[1] + v[11]*p[2] + v[15];
    if (fabsf(ww) < 1e-3f) return false;
    float wx = v[0]*p[0] + v[4]*p[1] + v[8]*p[2] + v[12];
    return fabsf(wx / ww) < 0.15f;
}

static void APIENTRY hkglUniformMatrix4fv(int location, int count, unsigned char transpose, const float* value)
{
    // No-Clip chase camera: replace the game's view-projection with our own.
    if (value && count == 1 && IsCameraMatrix(value))
    {
        if (tymod::g_noclipEnabled) tymod::CameraNoteGame(value);
        float tmp[16];
        if (tymod::CameraApply(value, tmp))
        {
            o_glUniformMatrix4fv(location, count, transpose, tmp);
            return;
        }
    }

    if (g_recon && g_inLevel && value)
    {
        if (count == 1) LogMatrix(0, "U4", g_curProgram, location, 1, value);
        else            LogMatrix(3, "U4ARR", g_curProgram, location, count, value);
    }
    o_glUniformMatrix4fv(location, count, transpose, value);
}

static void APIENTRY hkglProgramUniformMatrix4fv(unsigned int program, int location, int count, unsigned char transpose, const float* value)
{
    if (g_recon && g_inLevel && value && g_u4Logged < 200000)
        LogMatrix(1, "PU4", program, location, count, value);
    o_glProgramUniformMatrix4fv(program, location, count, transpose, value);
}

static void APIENTRY hkglUniformMatrix3fv(int location, int count, unsigned char transpose, const float* value)
{
    if (g_recon && g_inLevel && value && g_u4Logged < 200000)
    {
        float m[16] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
        m[0]=value[0]; m[1]=value[1]; m[2]=value[2];
        m[4]=value[3]; m[5]=value[4]; m[6]=value[5];
        m[8]=value[6]; m[9]=value[7]; m[10]=value[8];
        LogMatrix(2, "M3", g_curProgram, location, count, m);
    }
    o_glUniformMatrix3fv(location, count, transpose, value);
}

static BOOL WINAPI hkGetKeyboardState(PBYTE keys)
{
    BOOL r = o_GetKeyboardState(keys);
    if (r && keys && tymod::g_noclipEnabled)
    {
        static const int kMoveKeys[] = { 'W', 'A', 'S', 'D', VK_SPACE, VK_CONTROL };
        for (int vk : kMoveKeys) keys[vk] = 0;
    }
    return r;
}

static BOOL WINAPI hkGetCursorPos(LPPOINT lpPoint)
{
    BOOL r = o_GetCursorPos(lpPoint);
    if (r && lpPoint && tymod::OverlayIsOpen())
    {
        // Keep returning the last position so the game doesn't see movement
        // but we also need to ensure ImGui sees it; ImGui gets WM_MOUSEMOVE.
        // Returning the cached position prevents camera from spinning.
        static POINT last = {0,0};
        if (last.x != 0 || last.y != 0)
        {
            *lpPoint = last;
        }
        else if (r)
        {
            last = *lpPoint;
        }
        return TRUE;
    }
    if (r && lpPoint)
    {
        static POINT last = {0,0};
        last = *lpPoint;
    }
    return r;
}

static BOOL WINAPI hkSetCursorPos(int x, int y)
{
    if (tymod::OverlayIsOpen())
        return TRUE;
    return o_SetCursorPos(x, y);
}

static DWORD WINAPI InstallGLHookThread(LPVOID)
{
    if (g_resolve[0] && MH_CreateHook(g_resolve[0], &hkglUniformMatrix4fv, (void**)&o_glUniformMatrix4fv) == MH_OK)
        { MH_EnableHook(g_resolve[0]); tymod::Log("hooked glUniformMatrix4fv @ %p", g_resolve[0]); }
    else tymod::Log("hook glUniformMatrix4fv failed");

    if (g_resolve[1] && MH_CreateHook(g_resolve[1], &hkglUseProgram, (void**)&o_glUseProgram) == MH_OK)
        { MH_EnableHook(g_resolve[1]); tymod::Log("hooked glUseProgram @ %p", g_resolve[1]); }
    else tymod::Log("glUseProgram not hooked (%p)", g_resolve[1]);

    if (g_resolve[2] && MH_CreateHook(g_resolve[2], &hkglUseProgramARB, (void**)&o_glUseProgramARB) == MH_OK)
        { MH_EnableHook(g_resolve[2]); tymod::Log("hooked glUseProgramObjectARB @ %p", g_resolve[2]); }
    else tymod::Log("glUseProgramObjectARB not hooked (%p)", g_resolve[2]);

    if (g_resolve[3] && MH_CreateHook(g_resolve[3], &hkglProgramUniformMatrix4fv, (void**)&o_glProgramUniformMatrix4fv) == MH_OK)
        { MH_EnableHook(g_resolve[3]); tymod::Log("hooked glProgramUniformMatrix4fv @ %p", g_resolve[3]); }
    else tymod::Log("glProgramUniformMatrix4fv not hooked (%p)", g_resolve[3]);

    if (g_resolve[4] && MH_CreateHook(g_resolve[4], &hkglUniformMatrix3fv, (void**)&o_glUniformMatrix3fv) == MH_OK)
        { MH_EnableHook(g_resolve[4]); tymod::Log("hooked glUniformMatrix3fv @ %p", g_resolve[4]); }
    else tymod::Log("glUniformMatrix3fv not hooked (%p)", g_resolve[4]);
    return 0;
}

static void TryHookGLUniform()
{
    if (g_u4Hooked || !o_wglGetProcAddress) return;
    g_u4Hooked = true;
    for (int i = 0; i < RESOLVE_N; i++) g_resolve[i] = (void*)o_wglGetProcAddress(g_glNames[i]);
    tymod::Log("resolve: u4=%p use=%p useARB=%p pu4=%p m3=%p", g_resolve[0], g_resolve[1], g_resolve[2], g_resolve[3], g_resolve[4]);
    HANDLE h = CreateThread(nullptr, 0, InstallGLHookThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

// TEMP recon: log which GL functions the game asks for, so we know how it
// sets up its camera (shader uniforms vs fixed-function matrices).
static char g_seen[700][64];
static int  g_seenN = 0;

static PROC WINAPI hkwglGetProcAddress(LPCSTR name)
{
    PROC r = o_wglGetProcAddress(name);
    if (name)
    {
        bool seen = false;
        for (int i = 0; i < g_seenN; i++)
            if (strcmp(g_seen[i], name) == 0) { seen = true; break; }
        if (!seen && g_seenN < 700)
        {
            strncpy(g_seen[g_seenN], name, 63);
            g_seen[g_seenN][63] = 0;
            g_seenN++;
            tymod::Log("wglProc: %s", name);
        }
    }
    return r;
}

static HWND  g_hwnd = nullptr;
static bool  g_glLogged = false;
static bool  g_overlayReady = false;
static bool  g_cursorManaged = false;
static bool  g_prevInsert = false;

static void LogGLInfo()
{
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (!gl) { tymod::Log("opengl32.dll not loaded"); return; }
    auto glGetString = (const char*(WINAPI*)(unsigned int))GetProcAddress(gl, "glGetString");
    if (!glGetString) { tymod::Log("glGetString not found"); return; }
    tymod::Log("GL_VERSION = %s", glGetString(0x1F02) ? glGetString(0x1F02) : "?");
    tymod::Log("GL_RENDERER= %s", glGetString(0x1F01) ? glGetString(0x1F01) : "?");
    tymod::Log("GLSL       = %s", glGetString(0x8B8C) ? glGetString(0x8B8C) : "?");
}

// Feed input to ImGui; while the menu is open, swallow game input so the player
// doesn't move while you click around the UI. While flying (noclip on, menu
// closed) swallow the movement keys too, so the game doesn't walk Ty as well.
static LRESULT CALLBACK hkWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    ImGui_ImplWin32_WndProcHandler(h, msg, w, l);

    if (tymod::OverlayIsOpen())
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHOVER:
        case WM_MOUSELEAVE:
        case WM_NCMOUSEMOVE:
        case WM_NCLBUTTONDOWN: case WM_NCLBUTTONUP: case WM_NCLBUTTONDBLCLK:
        case WM_NCRBUTTONDOWN: case WM_NCRBUTTONUP: case WM_NCRBUTTONDBLCLK:
        case WM_INPUT:
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR:
            return 0;
        }
    }
    else if (tymod::g_noclipEnabled)
    {
        // Swallow only vertical keys so the game doesn't also jump/dive while
        // flying. WASD is left alone so we can observe the game's camera-relative
        // move and use it as the forward direction.
        bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        bool up   = (msg == WM_KEYUP   || msg == WM_SYSKEYUP);
        if ((down || up) && (w == VK_SPACE || w == VK_CONTROL))
            return 0;
    }
    return CallWindowProcA(o_WndProc, h, msg, w, l);
}

static void EnsureOverlay()
{
    if (g_overlayReady) return;

    if (!p_wglGetCurrentDC) return;
    HDC dc = p_wglGetCurrentDC();
    HWND hwnd = dc ? WindowFromDC(dc) : nullptr;
    if (!hwnd) hwnd = GetForegroundWindow();
    if (!hwnd) return;

    g_hwnd = hwnd;
    o_WndProc = (WNDPROC_t)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

    if (tymod::OverlayInit(hwnd))
    {
        g_overlayReady = true;
        tymod::NoClipSetWindow(hwnd);
        tymod::Log("overlay ready, hwnd=%p", hwnd);
    }
}

static void PollMenuHotkey()
{
    bool now = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (now && !g_prevInsert)
    {
        tymod::OverlayToggle();
        tymod::Log("menu %s", tymod::OverlayIsOpen() ? "opened" : "closed");
    }
    g_prevInsert = now;

    static bool prevF7 = false;
    bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7 && !prevF7)
    {
        g_recon = !g_recon;
        tymod::Log("recon %s", g_recon ? "ON" : "OFF");
    }
    prevF7 = f7;
}

static void ManageCursor()
{
    bool open = tymod::OverlayIsOpen();
    if (open)
    {
        ClipCursor(nullptr);
        if (!g_cursorManaged)
        {
            int n = ShowCursor(TRUE);
            while (n < 0) n = ShowCursor(TRUE);
            g_cursorManaged = true;
        }
    }
    else if (g_cursorManaged)
    {
        int n = ShowCursor(FALSE);
        while (n >= 0) n = ShowCursor(FALSE);
        g_cursorManaged = false;
    }
}

static BOOL WINAPI hkSwapBuffers(HDC hdc)
{
    if (!g_glLogged) { g_glLogged = true; LogGLInfo(); }

    if (!g_u4Hooked) TryHookGLUniform();
    {
        float* pp = (float*)tymod::NoClipAddress();
        if (pp)
        {
            bool was = g_inLevel;
            g_inLevel = (pp[0]*pp[0] + pp[1]*pp[1] + pp[2]*pp[2]) > 1.0f;
            if (g_inLevel != was)
                tymod::Log("LEVEL %s f=%d pos=%.1f %.1f %.1f", g_inLevel ? "enter" : "exit", g_u4Frame, pp[0], pp[1], pp[2]);
            if (g_inLevel && (g_u4Frame % 120) == 0)
                tymod::Log("POS f=%d %.2f %.2f %.2f", g_u4Frame, pp[0], pp[1], pp[2]);
        }
    }
    g_u4Frame++;

    EnsureOverlay();
    PollMenuHotkey();
    if (g_overlayReady)
    {
        ManageCursor();
        tymod::NoClipTick();
        tymod::HealthTick();
        tymod::OverlayNewFrame();
        tymod::OverlayRender();
    }

    return o_SwapBuffers(hdc);
}

void tymod::ModInit()
{
    Log("ModInit start");

    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed"); return; }

    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (gl)
    {
        p_wglGetCurrentDC = (wglGetCurrentDC_t)GetProcAddress(gl, "wglGetCurrentDC");
        p_glGetIntegerv   = (glGetIntegerv_t)GetProcAddress(gl, "glGetIntegerv");
        tymod::Log("glGetIntegerv = %p", p_glGetIntegerv);
    }

    if (gl)
    {
        void* wgl = (void*)GetProcAddress(gl, "wglGetProcAddress");
        if (wgl && MH_CreateHook(wgl, &hkwglGetProcAddress, (void**)&o_wglGetProcAddress) == MH_OK)
        {
            MH_EnableHook(wgl);
            Log("wglGetProcAddress hook installed");
        }
        else Log("wglGetProcAddress hook failed");
    }

    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    if (!gdi) { Log("gdi32.dll not loaded"); return; }

    void* target = (void*)GetProcAddress(gdi, "SwapBuffers");
    Log("SwapBuffers = %p", target);
    if (!target) { Log("SwapBuffers not found"); return; }

    HMODULE user = GetModuleHandleA("user32.dll");
    if (user)
    {
        void* gks = (void*)GetProcAddress(user, "GetKeyboardState");
        if (gks && MH_CreateHook(gks, &hkGetKeyboardState, (void**)&o_GetKeyboardState) == MH_OK)
        { MH_EnableHook(gks); Log("GetKeyboardState hook installed @ %p", gks); }
        else Log("GetKeyboardState hook failed");

        void* gcp = (void*)GetProcAddress(user, "GetCursorPos");
        if (gcp && MH_CreateHook(gcp, &hkGetCursorPos, (void**)&o_GetCursorPos) == MH_OK)
        { MH_EnableHook(gcp); Log("GetCursorPos hook installed @ %p", gcp); }
        else Log("GetCursorPos hook failed");

        void* scp = (void*)GetProcAddress(user, "SetCursorPos");
        if (scp && MH_CreateHook(scp, &hkSetCursorPos, (void**)&o_SetCursorPos) == MH_OK)
        { MH_EnableHook(scp); Log("SetCursorPos hook installed @ %p", scp); }
        else Log("SetCursorPos hook failed");
    }

    if (MH_CreateHook(target, &hkSwapBuffers, (void**)&o_SwapBuffers) != MH_OK)
    {
        Log("MH_CreateHook failed");
        return;
    }
    if (MH_EnableHook(target) != MH_OK) { Log("MH_EnableHook failed"); return; }

    NoClipInit();
    tymod::HealthInitAuto((unsigned)tymod::NoClipAddress() - 0x270B78);
    Log("SwapBuffers hook installed");
}

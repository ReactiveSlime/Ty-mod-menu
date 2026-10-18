#include "esp.h"
#include "log.h"
#include "noclip.h"
#include "camera.h"
#include <cmath>
#include <cstring>
#include <stdio.h>

extern "C" {
#include "MinHook.h"
}

namespace tymod {
static UINT g_moduleBase = 0;
static float g_vp[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static bool g_espEnabled = false;
static bool g_drawLabels = true;
static bool g_drawDistance = true;
static bool g_drawOffscreen = true;
static float g_maxDist = 1000.0f;

enum ECats { C_OPAL, C_CRATE, C_BILBY, C_INVIS, C_COG, C_TE, C_RAIN, CAT_MAX };
static const char* kCatName[CAT_MAX] = {
    "Opals", "Crates", "Caged Bilbies", "Invisible Crates", "Golden Cogs", "Thunder Eggs", "Rainbow Scales"
};
static ImVec4 kCatColor[CAT_MAX] = {
    ImVec4(0.8f,0.2f,1.0f,1.0f),
    ImVec4(0.9f,0.7f,0.2f,1.0f),
    ImVec4(0.2f,0.9f,0.9f,1.0f),
    ImVec4(0.6f,0.6f,0.9f,1.0f),
    ImVec4(1.0f,0.8f,0.2f,1.0f),
    ImVec4(0.95f,0.35f,0.1f,1.0f),
    ImVec4(0.2f,1.0f,0.4f,1.0f),
};

struct StaticList g_static[CAT_MAX] = {};
bool ReadPos(UINT addr, float* out3);

// The position field offsets inside each struct were never confirmed (MTP only
// syncs the state bytes), so we auto-detect them: find the stride word where a
// large fraction of objects hold a plausible world position. Notably the
// position is NOT always at a fixed offset across categories.
static int  g_posOff[CAT_MAX]     = {-1,-1,-1,-1,-1,-1,-1};
// Level for which detection succeeded, per category. Chains often resolve a
// frame or two late after a level change (or a failed resolve on the transition
// frame), so a one-shot "first frame of new level" scan misses them — instead
// we retry until detection succeeds for the current level.
static UINT g_detDoneLvl[CAT_MAX] = {0xFFFFFFFE,0xFFFFFFFE,0xFFFFFFFE,0xFFFFFFFE,0xFFFFFFFE,0xFFFFFFFE,0xFFFFFFFE};

// Position-like word: all-sane floats, |x| and |z| > 1 (rules out zeroed or
// tiny fields; Y is often legitimately small). Re-run per level.
static void DetectPosOffset(int c, UINT arr, int stride, UINT lvl)
{
    const char* name = kCatName[c];
    int count = g_static[c].count;
    int   bestOff = -1;
    float bestFrac = 0.0f;
    for (int off = 0; off + 12 <= stride; off += 4)
    {
        int good = 0, tried = 0;
        for (int i = 1; i < 40 && i < count; i++)
        {
            UINT obj = arr + (UINT)(i * stride);
            float p0[3];
            if (ReadPos(obj + off, p0))
            {
                tried++;
                if (fabsf(p0[0]) > 1.0f && fabsf(p0[1]) < 100000.0f && fabsf(p0[2]) > 1.0f)
                    good++;
            }
        }
        float frac = tried ? (float)good / (float)tried : 0.0f;
        if (frac > bestFrac) { bestFrac = frac; bestOff = off; }
    }
    if (bestFrac >= 0.5f && bestOff >= 0)
    {
        g_posOff[c] = bestOff;
        Log("ESP[%s] posOff detected 0x%X (frac %.0f%%, stride=%d)",
            name, bestOff, bestFrac * 100.0f, stride);
    }
    else
    {
        g_posOff[c] = -1;
        // Retries run every ~0.5s until success — only log the failure once
        // per (category, level) so the log doesn't fill with retries.
        static UINT s_noLogged[CAT_MAX] = {0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF};
        if (s_noLogged[c] != lvl)
        {
            s_noLogged[c] = lvl;
            Log("ESP[%s] no posOff found (best 0x%X frac %.0f%%, stride=%d)",
                name, bestOff, bestFrac * 100.0f, stride);
        }
    }
}

static int sl_count(int c) { return g_static[c].count; }

inline bool Sane(float v) {
    // ponytail: simple NaN check
    return v == v && v > -500000.0f && v < 500000.0f;
}

bool ReadU32(UINT addr, UINT* out) {
    if (!addr) return false;
    __try {
        *out = *(UINT*)(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadPos(UINT addr, float* out3) {
    if (!addr) return false;
    __try {
        out3[0] = *(float*)(addr + 0);
        out3[1] = *(float*)(addr + 4);
        out3[2] = *(float*)(addr + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!Sane(out3[0]) || !Sane(out3[1]) || !Sane(out3[2])) return false;
    return true;
}

UINT ResolveChain(const StaticList& sl) {
    if (!g_moduleBase) return 0;
    UINT base = g_moduleBase + sl.base;
    for (int i = 0; i < sl.nDeref && i < 4; ++i) {
        UINT p = 0;
        if (!ReadU32(base, &p) || !p) return 0;
        base = p + sl.derefs[i];
    }
    return base;
}

static void WorldToScreen(const float* w, float& sx, float& sy, bool& behind) {
    // VP * vec4(w,1)
    float x = g_vp[0]*w[0] + g_vp[4]*w[1] + g_vp[8]*w[2] + g_vp[12];
    float y = g_vp[1]*w[0] + g_vp[5]*w[1] + g_vp[9]*w[2] + g_vp[13];
    float z = g_vp[2]*w[0] + g_vp[6]*w[1] + g_vp[10]*w[2] + g_vp[14];
    float wv = g_vp[3]*w[0] + g_vp[7]*w[1] + g_vp[11]*w[2] + g_vp[15];
    behind = (wv <= 0.001f);
    if (wv == 0.0f) wv = 1.0f;
    float nx = x / wv;
    float ny = y / wv;
    const ImVec2& sz = ImGui::GetIO().DisplaySize;
    sx = (nx * 0.5f + 0.5f) * sz.x;
    sy = (-ny * 0.5f + 0.5f) * sz.y;
}

void EspSetViewProj(const float* vp) {
    memcpy(g_vp, vp, 16 * sizeof(float));
    // Diagnostic: confirm the camera hook is capturing a real projection
    // (1 frame in 120, so the log doesn't explode).
    static int s_vpN = 0;
    if ((++s_vpN % 120) == 1)
        Log("EspVP: %.2f %.2f %.2f %.2f | %.2f (frame #%d)",
            vp[0], vp[1], vp[2], vp[3], vp[15], s_vpN);
}

void EspLoadMtpPresets() {
    // Opals C_OPAL: base 0x28AB7C, derefs {0,0}, nDeref 2, count 300, state 0x78, stride 0x114, pos 0x08
    g_static[C_OPAL] = {0x0028AB7C, {0,0,0,0}, 2, 300, 0x78, 0x114, 0x08, 0,0,false,false,true,true};
    // Crates C_CRATE: 0x254CB8, {0}, nDeref 1, count 31, state 0x114, stride 0x1C0, pos 0x08
    g_static[C_CRATE] = {0x00254CB8, {0,0,0,0}, 1, 31, 0x114, 0x1C0, 0x08, 0,0,false,false,true,true};
    // Bilbies C_BILBY: 0x27D608, {0,0}, 2, 5, 0x34, 0x134, 0x74
    g_static[C_BILBY] = {0x0027D608, {0,0,0,0}, 2, 5, 0x34, 0x134, 0x74, 0,0,false,false,true,true};
    // Invis C_INVIS: 0x254DB0, {0}, 1, 9, state 0x114, stride 0x1DC, pos 0x08
    g_static[C_INVIS] = {0x00254DB0, {0,0,0,0}, 1, 9, 0x114, 0x1DC, 0x08, 0,0,false,false,true,true};
    // Cogs C_COG: 0x270424, {0}, 1, 10, state 0xC4, stride 0x144, pos 0x7C
    g_static[C_COG] = {0x00270424, {0,0,0,0}, 1, 10, 0xC4, 0x144, 0x7C, 0,0,false,false,true,true};
    // Thunder Eggs C_TE: 0x270280, {0}, 1, 8, state 0xC4, stride 0x144 (same
    // struct layout as cogs); pos auto-detected, seeded with the cog layout.
    g_static[C_TE]   = {0x00270280, {0,0,0,0}, 1, 8, 0xC4, 0x144, 0x44, 0,0,false,false,true,true};
    // Rainbow C_RAIN: 0x28AB7C, {0,0}, 2, 25, state 0x78, stride 0x114, pos 0x08
    g_static[C_RAIN] = {0x0028AB7C, {0,0,0,0}, 2, 25, 0x78, 0x114, 0x08, 0,0,false,false,true,true};
}

void EspInit(UINT moduleBase) {
    g_moduleBase = moduleBase;
    EspLoadMtpPresets();
    Log("EspInit: moduleBase=0x%X", moduleBase);
}

void EspDraw() {
    if (!g_espEnabled) return;
    static int s_drawN = 0;
    bool diags = (s_drawN++ % 120) == 1;

    UINT lvl = 0;
    ReadU32(g_moduleBase + 0x280594, &lvl); // current level ID
    if (diags) Log("EspDraw: enabled lvl=%d", lvl);

    // Background draw list: renders underneath the ImGui window, so the mod
    // menu is the topmost thing on screen while the ESP stays visible.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    int drawn = 0;
    for (int c = 0; c < CAT_MAX; ++c) {
        // ponytail: level gates — opals, cogs & thunder eggs live only in
        // playable levels; rainbow scales only in the Rainbow Cliffs hub (lvl 0).
        if ((c == C_OPAL || c == C_COG || c == C_TE) && lvl == 0) continue;
        if (c == C_RAIN && lvl != 0) continue;
        StaticList sl = g_static[c];
        if (c == C_CRATE && lvl == 10) {
            sl.base = 0x00255190; // Outback Safari
        }
        UINT arr = ResolveChain(sl);
        if (!arr) { if (diags) Log("ESP[%s] unresolved (base=0x%X deref=%d)", kCatName[c], sl.base, sl.nDeref); continue; }
        int count = sl.count;
        int stride = sl.stride;
        int posOff = sl.posOff;
        int stateOff = sl.stateOff;
        if (stride < 0x8) stride = 0x10;

        // Retry the position auto-detect until it succeeds for this level
        // (throttled to every ~0.5s of game frames so late-resolving chains
        // still get scanned, without farming the log).
        if (g_detDoneLvl[c] != lvl) {
            static int s_retry[CAT_MAX] = {0};
            if ((s_retry[c]++ % 30) == 0)
                DetectPosOffset(c, arr, stride, lvl);
            if (g_posOff[c] >= 0)
                g_detDoneLvl[c] = lvl;
        }
        if (g_posOff[c] >= 0) posOff = g_posOff[c];
        for (int i = 0; i < count; ++i) {
            UINT obj = arr + (UINT)(i * stride);
            UINT s = 0;
            if (!ReadU32(obj + stateOff, &s)) continue;
            if (diags && i == 0) {
                float pp0[3] = {0,0,0};
                ReadPos(obj + posOff, pp0);
                Log("ESP[%s] i0 obj=0x%X state=%d pos=%.1f %.1f %.1f",
                    kCatName[c], obj, s, pp0[0], pp0[1], pp0[2]);
            }
            bool draw = false;
            switch (c) {
                case C_OPAL: case C_COG: case C_TE: case C_RAIN: draw = (s != 5); break;
                case C_BILBY: draw = (s == 1); break;
                case C_CRATE: draw = (s == 1); break;
                case C_INVIS: draw = (s != 2); break;
                default: draw = false; break;
            }
            if (!draw) continue;
            float p[3] = {0,0,0};
            if (!ReadPos(obj + posOff, p)) continue;
            // Empty slots are zeroed structs: state 0 + position (0,0,0).
            // No real collectible sits exactly at the world origin.
            if (p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f) continue;

            // 3D distance to the player (drives the Max Dist filter / labels).
            float pp[3];
            NoClipGetPos(pp);
            float ddx = p[0]-pp[0], ddy = p[1]-pp[1], ddz = p[2]-pp[2];
            float dist3d = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz);
            if (dist3d > g_maxDist) continue;

            float sx=0, sy=0; bool behind=false;
            WorldToScreen(p, sx, sy, behind);
            // The VP's w-row is not a reliable behind test (game matrix
            // conventions vary), so use the real camera: dot(item - eye, fwd).
            float eye[3], rt[3], up[3], fwd[3];
            bool haveBasis = tymod::CameraGetViewEyeBasis(eye, rt, up, fwd);
            float tox = 0.0f, toy = 0.0f, toz = 0.0f;
            if (haveBasis)
            {
                tox = p[0] - eye[0]; toy = p[1] - eye[1]; toz = p[2] - eye[2];
                behind = (tox*fwd[0] + toy*fwd[1] + toz*fwd[2]) <= 0.0f;
            }
            if (behind && !g_drawOffscreen) continue;
            if (diags && drawn < 3)
                Log("ESP[%s] i=%d drawn at (%.0f, %.0f) behind=%d d3d=%.0f",
                    kCatName[c], i, sx, sy, behind, dist3d);
            drawn++;

            const ImVec2& scr = ImGui::GetIO().DisplaySize;
            float cx = scr.x * 0.5f, cy = scr.y * 0.5f;
            const float margin = 30.0f;

            bool onscreen = !behind &&
                sx >= margin && sx <= scr.x - margin &&
                sy >= margin && sy <= scr.y - margin;
            if (!onscreen)
            {
                if (!g_drawOffscreen) continue;
                // Border hint: arrow pinned to the screen border, sweeping
                // around the edges as the camera turns — never across the
                // center. Direction comes from the camera-space direction of
                // the item (its components along the camera's right/up axes)
                // for behind items, and from the projection for edge fringes.
                float ax = 0.0f, ay = 1.0f; // arrow direction (unit)
                if (behind && haveBasis)
                {
                    float ds = tox*rt[0] + toy*rt[1] + toz*rt[2];
                    float du = tox*up[0] + toy*up[1] + toz*up[2];
                    ax = ds; ay = -du;
                }
                else
                {
                    ax = sx - cx; ay = sy - cy;
                }
                float l = sqrtf(ax*ax + ay*ay);
                if (l < 1e-4f) { ax = 0.0f; ay = 1.0f; l = 1.0f; }
                ax /= l; ay /= l;
                float kx = fabsf(ax) > 1e-6f ? (cx - margin) / fabsf(ax) : 1e30f;
                float ky = fabsf(ay) > 1e-6f ? (cy - margin) / fabsf(ay) : 1e30f;
                float k = kx < ky ? kx : ky;
                if (k > 1e29f) k = 0.0f;
                sx = cx + ax * k;
                sy = cy + ay * k;
                float px = -ay, py = ax;
                const float a = 9.0f;
                ImU32 arrowCol = ImColor(kCatColor[c]);
                dl->AddTriangleFilled(
                    ImVec2(sx + ax*12.0f, sy + ay*12.0f),
                    ImVec2(sx - ax*4.0f + px*a, sy - ay*4.0f + py*a),
                    ImVec2(sx - ax*4.0f - px*a, sy - ay*4.0f - py*a),
                    arrowCol);
                if (g_drawLabels || g_drawDistance)
                {
                    ImU32 bg = IM_COL32(0, 0, 0, 160);
                    char buf[64];
                    if (g_drawLabels && g_drawDistance)
                        sprintf_s(buf, sizeof(buf), "%s %.0fm", kCatName[c], dist3d);
                    else if (g_drawLabels)
                        sprintf_s(buf, sizeof(buf), "%s", kCatName[c]);
                    else
                        sprintf_s(buf, sizeof(buf), "%.0fm", dist3d);
                    float lx = sx - 10.0f, ly = sy + 12.0f;
                    if (ly > scr.y - 14.0f) { ly = sy - 16.0f; }
                    if (lx > scr.x - ImGui::CalcTextSize(buf).x - 6.0f)
                        lx = scr.x - ImGui::CalcTextSize(buf).x - 6.0f;
                    ImVec2 tsz = ImGui::CalcTextSize(buf);
                    dl->AddRectFilled(ImVec2(lx-1, ly), ImVec2(lx+1+tsz.x, ly+tsz.y), bg, 2.0f);
                    dl->AddText(ImVec2(lx, ly), arrowCol, buf);
                }
                continue;
            }

            // Marker sized by distance: shrinks with range so far items don't
            // merge into a blob, but stays visible at close range.
            float r = 2400.0f / dist3d;
            if (r < 3.0f) r = 3.0f;
            if (r > 14.0f) r = 14.0f;
            ImVec4 col = kCatColor[c];
            ImU32 glow  = ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, 0.25f));
            ImU32 black = IM_COL32(0, 0, 0, 200);
            // dark backing -> translucent glow -> bright ring -> solid core
            dl->AddCircleFilled(ImVec2(sx,sy), r * 1.8f, glow, 12);
            dl->AddCircleFilled(ImVec2(sx,sy), r,       black, 12);
            dl->AddCircle(ImVec2(sx,sy), r, ImColor(col), 12, 2.5f);
            dl->AddCircleFilled(ImVec2(sx,sy), 1.5f, ImColor(col), 8);
            if (g_drawLabels || g_drawDistance)
            {
                ImU32 bg = IM_COL32(0, 0, 0, 160);
                float ty = sy + r + 2.0f;
                if (g_drawLabels && g_drawDistance)
                {
                    ImVec2 tsz = ImGui::CalcTextSize(kCatName[c]);
                    dl->AddRectFilled(ImVec2(sx+5, ty), ImVec2(sx+7+tsz.x, ty+tsz.y), bg, 2.0f);
                    dl->AddText(ImVec2(sx+6, ty), ImColor(col), kCatName[c]);
                }
                if (g_drawDistance)
                {
                    char buf[48];
                    sprintf_s(buf, sizeof(buf), "%.0fm", dist3d);
                    ImU32 white = IM_COL32(255, 255, 255, 255);
                    if (g_drawLabels) ty += 13.0f;
                    ImVec2 tsz = ImGui::CalcTextSize(buf);
                    dl->AddRectFilled(ImVec2(sx+5, ty), ImVec2(sx+7+tsz.x, ty+tsz.y), bg, 2.0f);
                    dl->AddText(ImVec2(sx+6, ty), white, buf);
                }
            }
        }
        if (diags) Log("ESP[%s] drawn=%d", kCatName[c], drawn);
    }
    if (diags) Log("EspDraw: total drawn=%d frame=%d", drawn, s_drawN);
}

void EspSetEnabled(bool on)
{
    if (g_espEnabled == on) return;
    g_espEnabled = on;
    Log("ESP %s", on ? "ON" : "OFF");
}

bool EspIsEnabled() { return g_espEnabled; }

void EspExportPersist(EspPersist& out)
{
    out.on = g_espEnabled;
    out.labels = g_drawLabels;
    out.dist = g_drawDistance;
    out.offscreen = g_drawOffscreen;
    out.maxDist = g_maxDist;
    for (int i = 0; i < ESP_CAT_MAX && i < CAT_MAX; i++)
        for (int k = 0; k < 3; k++)
            out.colors[i][k] = (&kCatColor[i].x)[k];
}

void EspImportPersist(const EspPersist& in)
{
    g_espEnabled = in.on;
    g_drawLabels = in.labels;
    g_drawDistance = in.dist;
    g_drawOffscreen = in.offscreen;
    g_maxDist = in.maxDist;
    for (int i = 0; i < ESP_CAT_MAX && i < CAT_MAX; i++)
        for (int k = 0; k < 3; k++)
            (&kCatColor[i].x)[k] = in.colors[i][k];
}

void EspMenu() {
    if (ImGui::Checkbox("Enable", &g_espEnabled)) {}
    ImGui::SameLine();
    ImGui::Checkbox("Labels", &g_drawLabels);
    ImGui::Checkbox("Distance", &g_drawDistance);
    ImGui::SameLine();
    ImGui::Checkbox("Offscreen markers", &g_drawOffscreen);
    ImGui::SliderFloat("Max Dist", &g_maxDist, 100.0f, 100000.0f, "%.0f",
                       ImGuiSliderFlags_Logarithmic);
    for (int c = 0; c < CAT_MAX; ++c) {
        ImGui::ColorEdit3(kCatName[c], (float*)&kCatColor[c]);
    }
}
}

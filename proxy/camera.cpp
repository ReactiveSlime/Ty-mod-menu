#include "camera.h"
#include "log.h"
#include "noclip.h"

#include <windows.h>
#include <math.h>

// No-Clip chase camera for TY the Tasmanian Tiger.
//
// The game uploads its view-projection matrix (P*V) to glUniformMatrix4fv
// location 0 in every world shader. It is uniquely identifiable: because it is
// a *perspective* camera it has a large v[15] (== -viewTranslationZ), whereas
// identity/UI/model matrices have v[15]==1 and orthographic shadow cameras have
// v[15]==1 too.
//
// From that matrix C we can recover, per frame:
//   Rg (3x3, world->view rotation) and tg (view translation) via
//     Rg.row0 = (v0,v4,v8)/|.| , Rg.row1 = (v1,v5,v9)/|.| , Rg.row2 = -(v3,v7,v11)
//     tg      = (v12/|row0|, v13/|row1|, -v15)
//   eye = -Rg^T * tg
//   forward = -Rg.row2
//
// To place our own camera (eye = p, world->view rotation Ro) we replace
//   C' = C * M ,  M = Vg^-1 * Vo
// where Vg = [Rg|tg], Vo = [Ro|to], to = -Ro*p.  M is affine:
//   M3 = Rg^T * Ro ,  Mt = Rg^T * (to - tg).

namespace tymod {
    static bool  s_followInit = false;
    static bool  s_followPrev = false;

    static float s_gameFwd[3] = {0, 0, 1};
    static bool  s_haveGameFwd = false;

    static float s_smoothYaw = 0.0f, s_smoothPitch = 0.0f;
    static bool  s_smoothInit = false;
    static float s_headH = 80.0f;   // look-at height above the player's origin
    static float s_dist  = 300.0f;  // orbit distance from the player

    static void Extract(const float* v, float Rg[3][3], float tg[3])
    {
        float a = sqrtf(v[0]*v[0] + v[4]*v[4] + v[8]*v[8]);
        float b = sqrtf(v[1]*v[1] + v[5]*v[5] + v[9]*v[9]);
        if (a < 1e-6f) a = 1e-6f;
        if (b < 1e-6f) b = 1e-6f;
        Rg[0][0] = v[0]/a; Rg[0][1] = v[4]/a; Rg[0][2] = v[8]/a;
        Rg[1][0] = v[1]/b; Rg[1][1] = v[5]/b; Rg[1][2] = v[9]/b;
        Rg[2][0] = v[3];   Rg[2][1] = v[7];   Rg[2][2] = v[11];
        tg[0] = v[12]/a; tg[1] = v[13]/b; tg[2] = v[15];
    }

    static void AnglesFromForward(const float f[3], float& yaw, float& pitch)
    {
        float fl = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
        if (fl < 1e-6f) { yaw = 0.0f; pitch = 0.0f; return; }
        float x = f[0]/fl, y = f[1]/fl, z = f[2]/fl;
        yaw = atan2f(x, z);
        if (y > 1.0f) y = 1.0f;
        if (y < -1.0f) y = -1.0f;
        pitch = asinf(y);
    }

    // Game uses a left-handed view convention (looks down +Z), so:
    //   forward = (cp*sy, sp, cp*cy)
    //   right   = cross(worldUp, forward) = (fz, 0, -fx)
    //   up      = cross(forward, right)
    //   basis rows (world->view) = (right, up, forward)
    static void BasisFromAngles(float yaw, float pitch, float right[3], float up[3], float fwd[3])
    {
        float cp = cosf(pitch), sp = sinf(pitch);
        float cy = cosf(yaw),  sy = sinf(yaw);
        fwd[0] = cp*sy; fwd[1] = sp; fwd[2] = cp*cy;
        right[0] = fwd[2]; right[1] = 0.0f; right[2] = -fwd[0];
        float rl = sqrtf(right[0]*right[0] + right[2]*right[2]);
        if (rl < 1e-5f) { right[0] = 1.0f; right[1] = 0.0f; right[2] = 0.0f; rl = 1.0f; }
        right[0] /= rl; right[2] /= rl;
        up[0] = fwd[1]*right[2] - fwd[2]*right[1];
        up[1] = fwd[2]*right[0] - fwd[0]*right[2];
        up[2] = fwd[0]*right[1] - fwd[1]*right[0];
    }

    bool CameraGetGameFwd(float out[3])
    {
        if (!s_haveGameFwd) return false;
        out[0] = s_gameFwd[0]; out[1] = s_gameFwd[1]; out[2] = s_gameFwd[2];
        return true;
    }

    // Record the game's own main-camera forward (for No-Clip movement heading)
    // without touching the matrix. Rejects far-away cameras (reflections/sky).
    void CameraNoteGame(const float* in)
    {
        float Rg[3][3], tg[3];
        Extract(in, Rg, tg);
        float eye[3];
        eye[0] = -(Rg[0][0]*tg[0] + Rg[1][0]*tg[1] + Rg[2][0]*tg[2]);
        eye[1] = -(Rg[0][1]*tg[0] + Rg[1][1]*tg[1] + Rg[2][1]*tg[2]);
        eye[2] = -(Rg[0][2]*tg[0] + Rg[1][2]*tg[1] + Rg[2][2]*tg[2]);

        float pp[3];
        NoClipGetFrozen(pp);
        float dx = eye[0]-pp[0], dy = eye[1]-pp[1], dz = eye[2]-pp[2];
        if (dx*dx + dy*dy + dz*dz > 1000000.0f) return;

        s_gameFwd[0] = Rg[2][0]; s_gameFwd[1] = Rg[2][1]; s_gameFwd[2] = Rg[2][2];
        s_haveGameFwd = true;
    }

    // Build C' = C * M so the rendered camera sits at 'pos' looking along (yaw,pitch),
    // replacing both the game camera's position and its (often stuttering) rotation.
    static void BuildOverride(const float in[16], const float Rg[3][3], const float tg[3],
                              const float pos[3], float yaw, float pitch, float out[16])
    {
        float right[3], up[3], fwd[3];
        BasisFromAngles(yaw, pitch, right, up, fwd);
        float Rok[3][3] = {
            { right[0], right[1], right[2] },
            { up[0],    up[1],    up[2]    },
            { fwd[0],   fwd[1],   fwd[2]   }
        };
        float to[3];
        to[0] = -(Rok[0][0]*pos[0] + Rok[0][1]*pos[1] + Rok[0][2]*pos[2]);
        to[1] = -(Rok[1][0]*pos[0] + Rok[1][1]*pos[1] + Rok[1][2]*pos[2]);
        to[2] = -(Rok[2][0]*pos[0] + Rok[2][1]*pos[1] + Rok[2][2]*pos[2]);

        float d[3] = { to[0]-tg[0], to[1]-tg[1], to[2]-tg[2] };

        float M[16];
        for (int c = 0; c < 3; c++)
            for (int r = 0; r < 3; r++)
            {
                float s = 0.0f;
                for (int k = 0; k < 3; k++) s += Rg[k][r] * Rok[k][c];
                M[4*c + r] = s;
            }
        for (int r = 0; r < 3; r++)
        {
            float s = 0.0f;
            for (int k = 0; k < 3; k++) s += Rg[k][r] * d[k];
            M[12 + r] = s;
        }
        M[3] = M[7] = M[11] = 0.0f;
        M[15] = 1.0f;

        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; k++) s += in[4*k + r] * M[4*c + k];
                out[4*c + r] = s;
            }
    }

    bool CameraApply(const float* in, float* out)
    {
        if (!in || !out) return false;

        bool follow = g_noclipEnabled;
        if (follow != s_followPrev) { s_followInit = false; s_smoothInit = false; s_followPrev = follow; }
        if (!follow) return false;

        float Rg[3][3], tg[3];
        Extract(in, Rg, tg);

        float gameEye[3];
        gameEye[0] = -(Rg[0][0]*tg[0] + Rg[1][0]*tg[1] + Rg[2][0]*tg[2]);
        gameEye[1] = -(Rg[0][1]*tg[0] + Rg[1][1]*tg[1] + Rg[2][1]*tg[2]);
        gameEye[2] = -(Rg[0][2]*tg[0] + Rg[1][2]*tg[1] + Rg[2][2]*tg[2]);

        float pp[3];
        NoClipGetFrozen(pp);
        float dx = gameEye[0]-pp[0], dy = gameEye[1]-pp[1], dz = gameEye[2]-pp[2];
        if (dx*dx + dy*dy + dz*dz > 1000000.0f) return false;   // not the main camera

        // Lightly low-pass the game camera's look direction. The game refreshes
        // its camera every few frames, so the raw rotation stutters. Keep the
        // time constant small: the game culls with ITS frustum, so if our view
        // diverges too far the world visibly unloads.
        float fy, fp;
        AnglesFromForward(Rg[2], fy, fp);
        static LARGE_INTEGER fq{}, pv{}; static bool tinit = false;
        if (!tinit) { QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&pv); tinit = true; }
        LARGE_INTEGER nw; QueryPerformanceCounter(&nw);
        float dt = (float)(nw.QuadPart - pv.QuadPart) / (float)fq.QuadPart;
        pv = nw;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        if (!s_smoothInit) { s_smoothYaw = fy; s_smoothPitch = fp; s_smoothInit = true; }
        else
        {
            float a = 1.0f - expf(-dt / 0.05f);
            float dyaw = fy - s_smoothYaw;
            while (dyaw >  3.14159265f) dyaw -= 6.28318531f;
            while (dyaw < -3.14159265f) dyaw += 6.28318531f;
            s_smoothYaw   += dyaw * a;
            s_smoothPitch += (fp - s_smoothPitch) * a;
        }
        float yaw = s_smoothYaw, pitch = s_smoothPitch;

        if (!s_followInit)
        {
            // Height the game camera aims at = Ty's head. Recover it from the
            // eye->player line so we can keep his head dead-centre.
            float hl = sqrtf(dx*dx + dz*dz);
            float fl = sqrtf(Rg[2][0]*Rg[2][0] + Rg[2][2]*Rg[2][2]);
            if (fl < 1e-4f) fl = 1e-4f;
            s_headH = dy + (hl / fl) * Rg[2][1];
            if (s_headH < 0.0f || s_headH > 500.0f) s_headH = 80.0f;

            float ax = gameEye[0]-pp[0], ay = gameEye[1]-(pp[1]+s_headH), az = gameEye[2]-pp[2];
            s_dist = sqrtf(ax*ax + ay*ay + az*az);
            if (s_dist < 20.0f) s_dist = 300.0f;

            s_followInit = true;
            return false;
        }

        // Orbit around Ty and always aim at his head, so the framing stays
        // centred while looking up/down/around just orbits the camera.
        float pr[3], pu[3], pf[3];
        BasisFromAngles(yaw, pitch, pr, pu, pf);
        float head[3] = { pp[0], pp[1] + s_headH, pp[2] };
        float ne[3];
        ne[0] = head[0] - s_dist * pf[0];
        ne[1] = head[1] - s_dist * pf[1];
        ne[2] = head[2] - s_dist * pf[2];

        BuildOverride(in, Rg, tg, ne, yaw, pitch, out);
        return true;
    }
}

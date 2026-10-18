#include "settings.h"
#include "log.h"
#include "noclip.h"
#include "cheats.h"
#include "esp.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace tymod {
    Hotkeys g_keys = { VK_INSERT, 0, 0, 0 };

    bool  g_fovOn = false;
    float g_fovY  = 70.0f;

    // One POD snapshot of every persisted setting, for change detection.
    struct Snap
    {
        int   keys[4];
        bool  fovOn, noclip;
        bool  lives, breath, inv, superOn;
        bool  espOn, espLabels, espDist, espOffscreen;
        float fov, speed, superMult, espMaxDist;
        float colors[7][3];
    };

    static Snap g_last = {};   // last saved / loaded state
    static bool g_lastValid = false;
    static int  g_saveCooldown = 0;

    static void GetPath(char* out, size_t n)
    {
        char exe[MAX_PATH] = "";
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        char* slash = strrchr(exe, '\\');
        if (slash) *slash = 0;
        _snprintf_s(out, n, _TRUNCATE, "%s\\tymod.ini", exe);
    }

    static void ApplySnapshot(const Snap& s)
    {
        g_keys.menu       = s.keys[0];
        g_keys.noclip     = s.keys[1];
        g_keys.esp        = s.keys[2];
        g_keys.superSpeed = s.keys[3];
        g_fovOn = s.fovOn;  g_fovY = s.fov;
        g_noclipEnabled = s.noclip;  g_speed = s.speed;
        g_infiniteLives = s.lives;  g_infiniteBreath = s.breath;  g_invincible = s.inv;
        g_superSpeed = s.superOn;   g_superMult = s.superMult;
        EspPersist ep;
        ep.on = s.espOn; ep.labels = s.espLabels; ep.dist = s.espDist;
        ep.offscreen = s.espOffscreen; ep.maxDist = s.espMaxDist;
        for (int i = 0; i < 7; i++)
            for (int k = 0; k < 3; k++) ep.colors[i][k] = s.colors[i][k];
        EspImportPersist(ep);
    }

    static void ReadSnapshot(Snap& s)
    {
        memset(&s, 0, sizeof(s));
        s.keys[0] = g_keys.menu; s.keys[1] = g_keys.noclip;
        s.keys[2] = g_keys.esp;  s.keys[3] = g_keys.superSpeed;
        s.fovOn = g_fovOn; s.fov = g_fovY;
        s.noclip = g_noclipEnabled; s.speed = g_speed;
        s.lives = g_infiniteLives; s.breath = g_infiniteBreath; s.inv = g_invincible;
        s.superOn = g_superSpeed; s.superMult = g_superMult;
        EspPersist ep;
        EspExportPersist(ep);
        s.espOn = ep.on; s.espLabels = ep.labels; s.espDist = ep.dist;
        s.espOffscreen = ep.offscreen; s.espMaxDist = ep.maxDist;
        for (int i = 0; i < 7; i++)
            for (int k = 0; k < 3; k++) s.colors[i][k] = ep.colors[i][k];
    }

    static void Save(const Snap& s)
    {
        char path[MAX_PATH];
        GetPath(path, sizeof(path));
        FILE* f = nullptr;
        if (fopen_s(&f, path, "w") != 0 || !f) { Log("settings save failed: %s", path); return; }
        fprintf(f, "menuKey=%d\nnoclipKey=%d\nespKey=%d\nsuperKey=%d\n",
            s.keys[0], s.keys[1], s.keys[2], s.keys[3]);
        fprintf(f, "fovOn=%d\nfov=%.1f\n", s.fovOn ? 1 : 0, s.fov);
        fprintf(f, "noclip=%d\nspeed=%.1f\n", s.noclip ? 1 : 0, s.speed);
        fprintf(f, "lives=%d\nbreath=%d\ninv=%d\nsuper=%d\nsuperMult=%.2f\n",
            s.lives ? 1 : 0, s.breath ? 1 : 0, s.inv ? 1 : 0, s.superOn ? 1 : 0, s.superMult);
        fprintf(f, "espOn=%d\nespLabels=%d\nespDist=%d\nespOffscreen=%d\nespMaxDist=%.1f\n",
            s.espOn ? 1 : 0, s.espLabels ? 1 : 0, s.espDist ? 1 : 0,
            s.espOffscreen ? 1 : 0, s.espMaxDist);
        for (int i = 0; i < 7; i++)
            fprintf(f, "col%d=%.3f,%.3f,%.3f\n", i, s.colors[i][0], s.colors[i][1], s.colors[i][2]);
        fclose(f);
        g_last = s;
        g_lastValid = true;
    }

    static void LoadDefaults(Snap& s)
    {
        memset(&s, 0, sizeof(s));
        s.keys[0] = VK_INSERT;
        s.speed = 250.0f;
        s.superMult = 3.0f;
        s.fov = 70.0f;
        s.espMaxDist = 1000.0f;
        s.espOn = true;
        s.espLabels = true; s.espDist = true; s.espOffscreen = true;
        static const float def[7][3] = {
            {0.8f,0.2f,1.0f}, {0.9f,0.7f,0.2f}, {0.2f,0.9f,0.9f}, {0.6f,0.6f,0.9f},
            {1.0f,0.8f,0.2f}, {0.95f,0.35f,0.1f}, {0.2f,1.0f,0.4f},
        };
        for (int i = 0; i < 7; i++)
            for (int k = 0; k < 3; k++) s.colors[i][k] = def[i][k];
    }

    void SettingsInit()
    {
        Snap s;
        LoadDefaults(s);
        char path[MAX_PATH];
        GetPath(path, sizeof(path));
        FILE* f = nullptr;
        if (fopen_s(&f, path, "r") == 0 && f)
        {
            char line[128];
            while (fgets(line, sizeof(line), f))
            {
                int b = 0, vi = 0, ci = 0;
                float vf = 0;
                if (1 == sscanf_s(line, "menuKey=%d", &vi))       s.keys[0] = vi;
                else if (1 == sscanf_s(line, "noclipKey=%d", &vi)) s.keys[1] = vi;
                else if (1 == sscanf_s(line, "espKey=%d", &vi))    s.keys[2] = vi;
                else if (1 == sscanf_s(line, "superKey=%d", &vi))  s.keys[3] = vi;
                else if (1 == sscanf_s(line, "fovOn=%d", &b))      s.fovOn = b != 0;
                else if (1 == sscanf_s(line, "fov=%f", &vf))       s.fov = vf;
                else if (1 == sscanf_s(line, "noclip=%d", &b))     s.noclip = b != 0;
                else if (1 == sscanf_s(line, "speed=%f", &vf))     s.speed = vf;
                else if (1 == sscanf_s(line, "lives=%d", &b))      s.lives = b != 0;
                else if (1 == sscanf_s(line, "breath=%d", &b))     s.breath = b != 0;
                else if (1 == sscanf_s(line, "inv=%d", &b))        s.inv = b != 0;
                else if (1 == sscanf_s(line, "super=%d", &b))      s.superOn = b != 0;
                else if (1 == sscanf_s(line, "superMult=%f", &vf)) s.superMult = vf;
                else if (1 == sscanf_s(line, "espOn=%d", &b))      s.espOn = b != 0;
                else if (1 == sscanf_s(line, "espLabels=%d", &b))  s.espLabels = b != 0;
                else if (1 == sscanf_s(line, "espDist=%d", &b))    s.espDist = b != 0;
                else if (1 == sscanf_s(line, "espOffscreen=%d", &b)) s.espOffscreen = b != 0;
                else if (1 == sscanf_s(line, "espMaxDist=%f", &vf)) s.espMaxDist = vf;
                else if (3 == sscanf_s(line, "col%d=%f,%f,%f", &ci, &vf, &vf, &vf) && ci >= 0 && ci < 7)
                {
                    // rescan the three floats after the '='
                    const char* eq = strchr(line, '=');
                    if (eq) sscanf_s(eq + 1, "%f,%f,%f", &s.colors[ci][0], &s.colors[ci][1], &s.colors[ci][2]);
                }
            }
            fclose(f);
            Log("settings loaded: %s", path);
        }
        ApplySnapshot(s);
        g_last = s;
        g_lastValid = true;
    }

    void SettingsTick()
    {
        // Saves every ~0.5 s of frames once something changed. Cheap: the ESP
        // exports happen via plain copies from process memory.
        if (++g_saveCooldown < 30) return;
        g_saveCooldown = 0;
        Snap s;
        ReadSnapshot(s);
        if (g_lastValid && memcmp(&s, &g_last, sizeof(Snap)) == 0) return;
        if (!g_lastValid) { g_last = s; g_lastValid = true; return; }
        static int s_logged = 0;
        if (s_logged++ < 3) Log("settings changed -> saving");
        Save(s);
    }

    const char* KeyName(int vk)
    {
        static char buf[16];
        if (!vk) return "None";
        if (vk >= 'A' && vk <= 'Z') { _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%c", vk); return buf; }
        if (vk >= '0' && vk <= '9') { _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%c", vk); return buf; }
        if (vk >= VK_F1 && vk <= VK_F12) { _snprintf_s(buf, sizeof(buf), _TRUNCATE, "F%d", vk - VK_F1 + 1); return buf; }
        switch (vk)
        {
        case VK_INSERT:  return "INSERT";
        case VK_DELETE:  return "DELETE";
        case VK_HOME:    return "HOME";
        case VK_END:     return "END";
        case VK_PRIOR:   return "PGUP";
        case VK_NEXT:    return "PGDN";
        case VK_NUMPAD0: return "NUM0";
        case VK_NUMPAD1: return "NUM1";
        case VK_NUMPAD2: return "NUM2";
        case VK_NUMPAD3: return "NUM3";
        case VK_NUMPAD4: return "NUM4";
        case VK_NUMPAD5: return "NUM5";
        case VK_NUMPAD6: return "NUM6";
        case VK_NUMPAD7: return "NUM7";
        case VK_NUMPAD8: return "NUM8";
        case VK_NUMPAD9: return "NUM9";
        case VK_MULTIPLY: return "NUM*";
        case VK_ADD:      return "NUM+";
        case VK_SUBTRACT: return "NUM-";
        case VK_UP:    return "UP";
        case VK_DOWN:  return "DOWN";
        case VK_LEFT:  return "LEFT";
        case VK_RIGHT: return "RIGHT";
        case VK_PAUSE: return "PAUSE";
        case VK_SCROLL: return "SCROLL";
        case VK_TAB:   return "TAB";
        case VK_BACK:  return "BKSP";
        case VK_OEM_1: return ";";
        case VK_OEM_2: return "/";
        case VK_OEM_3: return "`";
        case VK_OEM_4: return "[";
        case VK_OEM_5: return "\\";
        case VK_OEM_6: return "]";
        case VK_OEM_7: return "'";
        case VK_OEM_COMMA:  return ",";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_MINUS:  return "-";
        case VK_OEM_PLUS:   return "=";
        default: _snprintf_s(buf, sizeof(buf), _TRUNCATE, "0x%02X", vk); return buf;
        }
    }
}

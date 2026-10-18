#include "cheats.h"
#include "log.h"

#include <windows.h>

// Game cheats for TY the Tasmanian Tiger, using offsets confirmed by the
// Mul-Ty-Player reverse-engineering project (same TY.exe build TyMod targets,
// and it also reads position at +0x270B78 / health at +0x2737CC, like us).
//
// Key MTP-derived offsets (module-relative):
//   Lives        read  +0x2654FC, write via pointer chain [0x2888D8] + 0xAD0
//   Opals (int)  +0x26547C
//   Water/breath +0x2737F0
//   Invincible   bool  +0x288A55
//   Level id     +0x280594
//   Loading/menu +0x27EBF0 (0 = on menu or loading screen)
//   Typed AIRS/hero states etc. left for later.
//
// MTP is an external trainer, so all its writes need the module base; we run
// in-process and can just dereference base+offset directly. Pointer reads are
// wrapped in SEH so a bad save struct can't take the game down.

namespace tymod {
    bool g_infiniteLives  = false;
    bool g_infiniteBreath = false;
    bool g_invincible     = false;
    bool g_superSpeed     = false;
    float g_superMult     = 3.0f;

    static unsigned s_base = 0;

    static const unsigned OFF_LIVES        = 0x2654FC;
    static const unsigned OFF_LIVES_PTR    = 0x2888D8;
    static const unsigned OFF_LIVES_DISP   = 0xAD0;
    static const unsigned OFF_OPALS        = 0x26547C;
    static const unsigned OFF_WATER_HEALTH = 0x2737F0;
    static const unsigned OFF_INVINCIBLE   = 0x288A55;
    static const unsigned OFF_LEVEL_ID     = 0x280594;
    static const unsigned OFF_LOADING      = 0x27EBF0;

    // Hero speed fields confirmed by MTP HeroHandler (SetRunSpeed & co).
    static const unsigned OFF_RUN_SPEED   = 0x288914;   // default 10
    static const unsigned OFF_SWIM_SPEED  = 0x1F982C;   // default 20
    static const unsigned OFF_AIR_SPEED   = 0x288920;   // default 10
    static const unsigned OFF_GLIDE_SPEED = 0x288928;   // default 7

    // ChangeLevel(): fade out, poke the level state machine, trigger the load.
    static const unsigned OFF_WARP_FADEOUT = 0x264248;  // float scale (0.01)
    static const unsigned OFF_WARP_CLEAR   = 0x286C54;  // byte 0
    static const unsigned OFF_WARP_LEVEL   = 0x289028;  // int level id
    static const unsigned OFF_WARP_TRIGGER = 0x289050;  // byte 1

    static const int LIVES_KEEP = 20;   // what "infinite lives" pins to
    static const int BREATH_FALLBACK = 100;

    // Levels with ids from MTP's Levels.cs (id -> name).
    struct LevelEntry { int id; const char* name; };
    static const LevelEntry kLevels[] = {
        { 0, "Rainbow Cliffs" },
        { 4, "Two Up" },
        { 5, "Walk In The Park" },
        { 6, "Ship Rex" },
        { 7, "Bull's Pen" },
        { 8, "Bridge On The River Ty" },
        { 9, "Snow Worries" },
        { 10, "Outback Safari" },
        { 12, "Lyre, Lyre Pants On Fire" },
        { 13, "Beyond The Black Stump" },
        { 14, "Rex Marks The Spot" },
        { 15, "Fluffy's Fjord" },
        { 16, "End Game" },
        { 17, "Cass' Crest" },
        { 19, "Crikeys Cove" },
        { 20, "Cass' Pass" },
        { 21, "Bonus World 1" },
        { 22, "Bonus World 2" },
        { 23, "Final Battle" },
    };
    static const int kLevelN = (int)(sizeof(kLevels) / sizeof(kLevels[0]));

    // Safe helpers -----------------------------------------------------------
    static unsigned ReadU32(unsigned addr)
    {
        __try { return *(volatile unsigned*)addr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    // MTP's GetPointerAddress: walk [ptr] + offset, starting from base+offset.
    static unsigned Follow(unsigned offset, unsigned extraOff)
    {
        if (!s_base) return 0;
        unsigned p = ReadU32(s_base + offset);
        if (!p || p < 0x10000 || p >= 0x7FFF0000 || (p & 3)) return 0;
        unsigned t = p + extraOff;
        if (t < 0x10000 || t >= 0x7FFF0000 || (t & 3)) return 0;
        return t;
    }

    static bool InSaneRange(unsigned a)
    {
        return a >= 0x10000 && a < 0x7FFF0000;
    }

    // Public reads -----------------------------------------------------------
    int CheatsLives()   { return s_base ? (int)ReadU32(s_base + OFF_LIVES) : 0; }
    int CheatsOpals()   { return s_base ? (int)ReadU32(s_base + OFF_OPALS) : 0; }
    int CheatsLevelId() { return s_base ? (int)ReadU32(s_base + OFF_LEVEL_ID) : -1; }

    bool CheatsInLevel()
    {
        if (!s_base) return false;
        return ReadU32(s_base + OFF_LOADING) != 0;
    }

    int CheatsLevelCount() { return kLevelN; }

    const char* CheatsLevelName(int i)
    {
        if (i < 0 || i >= kLevelN) return "?";
        return kLevels[i].name;
    }

    int CheatsLevelIdOf(int i)
    {
        if (i < 0 || i >= kLevelN) return -1;
        return kLevels[i].id;
    }

    int CheatsLevelIndexById(int id)
    {
        for (int i = 0; i < kLevelN; i++)
            if (kLevels[i].id == id) return i;
        return -1;
    }

    // Init / tick ------------------------------------------------------------
    void CheatsInit(unsigned moduleBase)
    {
        s_base = moduleBase;
        if (!s_base) { Log("CheatsInit: no base"); return; }
        Log("CheatsInit: base=0x%X lives=%d opals=%d level=%d",
            s_base, CheatsLives(), CheatsOpals(), CheatsLevelId());
    }

    // Frame tick: apply every enabled cheat. Invincibility is a single flag in
    // the game, so only write it when the toggle changes (s_invPrev).
    void CheatsTick()
    {
        if (!s_base) return;
        __try
        {
            static bool s_invPrev = false;
            if (g_invincible != s_invPrev)
            {
                *(volatile unsigned char*)(s_base + OFF_INVINCIBLE) = g_invincible ? 0x1 : 0x0;
                s_invPrev = g_invincible;
                Log("Invincible %s", g_invincible ? "ON" : "OFF");
            }

            // Infinite lives: the game counts them down in its save struct
            // (mirror at +0x2654FC), so top up through the save pointer.
            if (g_infiniteLives && CheatsLives() < LIVES_KEEP)
            {
                unsigned t = Follow(OFF_LIVES_PTR, OFF_LIVES_DISP);
                if (InSaneRange(t))
                    *(volatile unsigned*)t = (unsigned)LIVES_KEEP;
            }

            if (g_infiniteBreath)
                *(volatile int*)(s_base + OFF_WATER_HEALTH) = BREATH_FALLBACK;

            // Super Speed: rewrite the hero speed fields every frame while on
            // (level loads reset them is unconfirmed — cheap to just rewrite),
            // and restore MTP's documented defaults on the fall-edge.
            static bool s_superPrev = false;
            if (g_superSpeed)
            {
                float m = g_superMult > 0.05f ? g_superMult : 0.05f;
                *(volatile float*)(s_base + OFF_RUN_SPEED)   = 10.0f * m;
                *(volatile float*)(s_base + OFF_SWIM_SPEED)  = 20.0f * m;
                *(volatile float*)(s_base + OFF_AIR_SPEED)   = 10.0f * m;
                *(volatile float*)(s_base + OFF_GLIDE_SPEED) = 7.0f * m;
                if (!s_superPrev) { Log("SuperSpeed ON x%.1f", m); s_superPrev = true; }
            }
            else if (s_superPrev)
            {
                *(volatile float*)(s_base + OFF_RUN_SPEED)   = 10.0f;
                *(volatile float*)(s_base + OFF_SWIM_SPEED)  = 20.0f;
                *(volatile float*)(s_base + OFF_AIR_SPEED)   = 10.0f;
                *(volatile float*)(s_base + OFF_GLIDE_SPEED) = 7.0f;
                Log("SuperSpeed OFF");
                s_superPrev = false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool CheatsWarp(int levelId)
    {
        if (!s_base || !CheatsInLevel())
            return false;
        __try
        {
            *(volatile float*)(s_base + OFF_WARP_FADEOUT) = 0.01f;
            *(volatile unsigned char*)(s_base + OFF_WARP_CLEAR) = 0x0;
            *(volatile int*)(s_base + OFF_WARP_LEVEL) = levelId;
            *(volatile unsigned char*)(s_base + OFF_WARP_TRIGGER) = 0x1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        Log("CheatsWarp: level %d", levelId);
        return true;
    }

}

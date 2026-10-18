#include "health.h"
#include "log.h"

#include <windows.h>

// Health finder for TY the Tasmanian Tiger.
//
// Ty has 4 hits per life. Whatever the game stores (int or float) counts down
// 4 -> 3 -> 2 -> 1, so the value is small and known. We do a cheap
// Cheat-Engine-style exact-value scan over all writable memory, then filter as
// the value drops. We track both the 32-bit int and the float interpretation of
// every word, so we don't need to know which one the game uses.

namespace tymod {
    static const int HMAX = 2000000;

    int g_healthScanValue = 4;
    bool g_infiniteHealth = false;

    // Known location of Ty's hit counter (max 4), as an offset from the module
    // base. Found in-game with the exact-value scan below.
    // Mul-Ty-Player confirmed the build also has a Bull/Boonie health slot
    // (+0x273814) used in Bull's Pen / Outback Safari, selected by the bool at
    // +0x27E544. Without this, Infinite Health would be wrong there.
    static const unsigned HEALTH_OFFSET       = 0x2737CC;
    static const unsigned HEALTH_OFFSET_BULL  = 0x273814;
    static const unsigned BULL_FLAG_OFFSET    = 0x27E544;
    static const unsigned LEVEL_ID_OFFSET     = 0x280594;   // 10 = Outback Safari
    static const int      OUTBACK_LEVEL_ID    = 10;
    static const int      HEALTH_MAX = 4;
    static unsigned       s_autoAddr = 0;
    static unsigned       s_base     = 0;

    static unsigned       h_ea[HMAX];
    static float          h_val[HMAX];   // value seen at the last scan/filter step
    static float          h_max[HMAX];   // highest value seen (= full hits)
    static unsigned char  h_fl[HMAX];    // bit0: 1 = float, 0 = int32
    static int            h_n = 0;

    static unsigned h_target = 0;
    static bool     h_hasTarget = false;
    static bool     h_tFloat = false;
    static float    h_tMax = 0.0f;
    static bool     h_frozen = false;

    static bool Writable(unsigned p)
    {
        return (p & (PAGE_READWRITE | PAGE_WRITECOPY |
                     PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    void HealthInitAuto(unsigned moduleBase)
    {
        s_base = moduleBase;
        s_autoAddr = moduleBase ? (moduleBase + HEALTH_OFFSET) : 0;
        if (!s_base) { Log("HealthInitAuto: no base"); return; }
        int v = 0;
        __try { v = *(volatile int*)s_autoAddr; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("HealthInitAuto: addr=0x%X value=%d", s_autoAddr, v);
    }

    void HealthScan()
    {
        h_n = 0;
        int want = g_healthScanValue;
        float fwant = (float)want;

        unsigned char* p = nullptr;
        while (p < (unsigned char*)0x7FFF0000)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            unsigned char* next = (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
            if (next <= p) break;

            if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && Writable(mbi.Protect))
            {
                unsigned start = (unsigned)(ULONG_PTR)mbi.BaseAddress;
                unsigned end   = start + (unsigned)mbi.RegionSize;
                __try
                {
                    for (unsigned a = (start + 3) & ~3u; a + 4 <= end; a += 4)
                    {
                        if (h_n >= HMAX) break;
                        unsigned raw = *(volatile unsigned*)a;
                        float f = *(volatile float*)&raw;
                        int   iv = (int)raw;

                        if (iv == want)
                        {
                            h_ea[h_n] = a; h_val[h_n] = (float)iv; h_max[h_n] = (float)iv; h_fl[h_n] = 0; h_n++;
                        }
                        if (f == fwant)
                        {
                            h_ea[h_n] = a; h_val[h_n] = f; h_max[h_n] = f; h_fl[h_n] = 1; h_n++;
                        }
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            p = next;
        }

        Log("HealthScan(=%d): %d candidates", want, h_n);
    }

    static void FilterMode(int mode)   // 0 = less, 1 = greater, 2 = changed
    {
        int m = 0;
        __try
        {
            for (int i = 0; i < h_n; i++)
            {
                unsigned raw = *(volatile unsigned*)h_ea[i];
                float cur = (h_fl[i] & 1) ? *(volatile float*)&raw : (float)*(volatile int*)&raw;
                if (cur != cur) continue;

                if (cur > h_max[i]) h_max[i] = cur;
                bool keep =
                    (mode == 0) ? (cur <  h_val[i] - 0.0001f) :
                    (mode == 1) ? (cur >  h_val[i] + 0.0001f) :
                                  (cur != h_val[i]);
                if (keep)
                {
                    h_ea[m] = h_ea[i]; h_val[m] = cur; h_max[m] = h_max[i]; h_fl[m] = h_fl[i]; m++;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        h_n = m;
        Log("HealthFilter(mode=%d): %d candidates", mode, h_n);
    }

    void HealthFilterLess()    { FilterMode(0); }
    void HealthFilterGreater() { FilterMode(1); }
    void HealthFilterChanged() { FilterMode(2); }

    int HealthCount() { return h_n; }

    bool HealthGet(int i, unsigned& addr, float& val, bool& isFloat)
    {
        if (i < 0 || i >= h_n) return false;
        addr = h_ea[i];
        isFloat = (h_fl[i] & 1) != 0;
        unsigned raw = *(volatile unsigned*)h_ea[i];
        val = isFloat ? *(volatile float*)&raw : (float)*(volatile int*)&raw;
        if (val != val) val = 0.0f;
        return true;
    }

    void HealthPick(int i)
    {
        if (i < 0 || i >= h_n) return;
        h_target = h_ea[i];
        h_tFloat = (h_fl[i] & 1) != 0;
        h_tMax = h_max[i];
        h_hasTarget = true;
        h_frozen = true;
        Log("HealthPick: 0x%X val=%.3f freezeMax=%.3f float=%d", h_target, h_val[i], h_tMax, (int)h_tFloat);
    }

    unsigned HealthTarget() { return h_hasTarget ? h_target : 0; }
    void HealthSetFrozen(bool on) { h_frozen = on; }
    bool HealthIsFrozen() { return h_frozen; }

    void HealthTick()
    {
        __try
        {
            if (g_infiniteHealth && s_base)
            {
                // Pick the right health slot for the current transform. MTP's
                // IsBull() bool (+0x27E544) covers Bull's Pen, but in Outback
                // Safari (level 10) Ty rides the bull without that flag set,
                // and MTP writes +0x273814 explicitly there, so also key off
                // the level id.
                bool bull = false;
                __try
                {
                    bull = ((*(volatile unsigned char*)(s_base + BULL_FLAG_OFFSET) & 1) != 0) ||
                           ((int)(*(volatile unsigned*)(s_base + LEVEL_ID_OFFSET)) == OUTBACK_LEVEL_ID);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
                unsigned addr = s_base + (bull ? HEALTH_OFFSET_BULL : HEALTH_OFFSET);
                *(volatile int*)addr = HEALTH_MAX;
            }

            if (h_frozen && h_hasTarget)
            {
                if (h_tFloat) *(volatile float*)h_target = h_tMax;
                else          *(volatile int*)h_target = (int)h_tMax;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

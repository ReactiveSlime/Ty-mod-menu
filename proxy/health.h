#pragma once

namespace tymod {
    // In-game health finder + freeze.
    // Health is tiny (4 hits per life), so we scan all writable memory for an
    // exact value (default 4), then filter as it drops when you take damage.
    extern int g_healthScanValue;   // value to search for (default 4)
    extern bool g_infiniteHealth;   // freeze the known health value at 4

    void HealthInitAuto(unsigned moduleBase);   // resolve the known health address

    void HealthScan();              // find every int/float == g_healthScanValue
    void HealthFilterLess();        // keep candidates whose value went down
    void HealthFilterGreater();     // keep candidates whose value went up
    void HealthFilterChanged();     // keep candidates whose value changed

    int  HealthCount();
    bool HealthGet(int i, unsigned& addr, float& val, bool& isFloat);
    void HealthPick(int i);
    unsigned HealthTarget();
    void HealthSetFrozen(bool on);
    bool HealthIsFrozen();
    void HealthTick();
}

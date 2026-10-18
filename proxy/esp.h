#pragma once
#include <windows.h>
#include <imgui.h>

namespace tymod {
struct StaticList {
    UINT base;
    UINT derefs[4];
    int  nDeref;
    int  count;
    int  stateOff;
    int  stride;
    int  posOff;
    int  idOff;
    UINT idValue;
    bool sm, sv, stable, autoPosOff;
};

static const int ESP_CAT_MAX = 7;

// ESP settings snapshot for persistence.
struct EspPersist {
    bool  on, labels, dist, offscreen;
    bool  cats[ESP_CAT_MAX];          // per-category draw toggles
    float maxDist;
    float colors[ESP_CAT_MAX][3];
};

void EspInit(UINT moduleBase);
void EspSetViewProj(const float* vp);
void EspDraw();
void EspMenu();
void EspSetEnabled(bool on);
bool EspIsEnabled();
void EspLoadMtpPresets();

// Persistence hooks used by settings.cpp.
void EspExportPersist(EspPersist& out);
void EspImportPersist(const EspPersist& in);
}

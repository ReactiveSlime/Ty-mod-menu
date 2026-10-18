#pragma once

namespace tymod {
    // Cheats ported from the Mul-Ty-Player offsets (same TY.exe build).
    extern bool  g_infiniteLives;
    extern bool  g_infiniteBreath;
    extern bool  g_invincible;
    // Super Speed: multiplies the hero's run/swim/air/glide speed fields
    // (MTP HeroHandler defaults: 10 / 20 / 10 / 7).
    extern bool  g_superSpeed;
    extern float g_superMult;

    void CheatsInit(unsigned moduleBase);
    void CheatsTick();

    bool CheatsWarp(int levelId);       // ChangeLevel: fade out, set id, trigger load
    bool CheatsInLevel();               // false on the main menu / loading screen

    int  CheatsLives();
    int  CheatsOpals();
    int  CheatsLevelId();               // current level id (0x280594)

    int         CheatsLevelCount();
    const char* CheatsLevelName(int i);
    int         CheatsLevelIdOf(int i);     // level id for menu index i
    int         CheatsLevelIndexById(int id); // menu row of a level id, or -1
}

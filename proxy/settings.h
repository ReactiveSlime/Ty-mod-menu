#pragma once
namespace tymod {
    // Configurable hotkeys (virtual-key codes; 0 = unbound).
    struct Hotkeys {
        int menu;       // toggles the mod menu
        int noclip;     // toggles No-Clip
        int esp;        // toggles the collectible ESP
        int superSpeed; // toggles Super Speed
    };
    extern Hotkeys g_keys;

    // Camera FOV override (vertical FOV in degrees; g_fovOn = active).
    extern bool  g_fovOn;
    extern float g_fovY;

    void SettingsInit();       // load tymod.ini (or apply defaults)
    void SettingsTick();       // saves automatically when the settings change
    const char* KeyName(int vk); // prints a VK as a readable name
}

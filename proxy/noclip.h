#pragma once
namespace tymod {
    extern bool  g_noclipEnabled;
    extern float g_speed;          // units per second

    void NoClipInit();
    void NoClipTick();
    void NoClipSetWindow(void* hwnd);
    void NoClipGetPos(float out[3]);
    long NoClipAddress();
    void NoClipGetFrozen(float out[3]);
}

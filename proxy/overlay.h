#pragma once
#include <windows.h>

namespace tymod {
    bool OverlayInit(void* hwnd);
    void OverlayNewFrame();
    void OverlayRender();
    void OverlayToggle();
    bool OverlayIsOpen();
    bool OverlayControlReleased();
}

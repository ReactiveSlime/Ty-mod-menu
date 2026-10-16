#pragma once
#include <windows.h>

namespace tymod {
    void LogSetModule(HMODULE m);
    void Log(const char* fmt, ...);
}

#include "log.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace tymod {
    static HMODULE g_self = nullptr;

    void LogSetModule(HMODULE m) { g_self = m; }

    void Log(const char* fmt, ...)
    {
        char msg[1024];
        va_list ap;
        va_start(ap, fmt);
        _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
        va_end(ap);

        char path[MAX_PATH] = {0};
        GetModuleFileNameA(g_self, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) *(slash + 1) = '\0';
        strncat_s(path, MAX_PATH, "tymod.log", _TRUNCATE);

        HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            SYSTEMTIME st; GetLocalTime(&st);
            char line[1200];
            int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                                "[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, msg);
            if (n > 0)
            {
                DWORD w = 0;
                WriteFile(h, line, (DWORD)n, &w, nullptr);
            }
            CloseHandle(h);
        }
    }
}

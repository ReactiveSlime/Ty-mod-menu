// TyMod - OpenAL32.dll proxy for TY the Tasmanian Tiger (32-bit)
//
// Loader mechanism: TY.exe imports OpenAL32.dll from its own folder. We replace
// it with this proxy. All 157 OpenAL exports are forwarded to soft_oal.dll via
// exports.def, so audio keeps working. DllMain then spins up the mod.
//
// Stage 3a: hook GDI SwapBuffers, log the game's GL version.

#include "log.h"
#include "mod.h"
#include <windows.h>

static DWORD WINAPI InitThread(LPVOID)
{
    tymod::Log("proxy loaded, starting mod init");
    tymod::ModInit();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        tymod::LogSetModule(hModule);
        DisableThreadLibraryCalls(hModule);
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}

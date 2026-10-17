// TyMod: OpenAL32.dll proxy for TY the Tasmanian Tiger (32-bit).
//
// TY.exe imports OpenAL32.dll from its own folder, so replacing that DLL is how
// the mod loads. All 157 OpenAL exports are forwarded to soft_oal.dll via
// exports.def, keeping audio working, and DllMain starts the mod.

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

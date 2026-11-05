#include <Windows.h>
BOOL WINAPI DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        SetEnvironmentVariableW(L"VC_LOCAL_EARLY_TEST", L"loaded before main");
    return TRUE;
}

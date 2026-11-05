// Early bootstrap for Miles-based ASI loaders that initialize after settings.
// Only VC Local User Files is loaded here; no other ASIs are enumerated.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cwchar>

static HMODULE systemDinput;
static HMODULE localUserFiles;

template <typename Fn> static Fn SystemFunction(const char* name) {
    return reinterpret_cast<Fn>(GetProcAddress(systemDinput, name));
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version,
    REFIID iid, void** result, void* outer) {
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, void**, void*);
    Fn fn = SystemFunction<Fn>("DirectInput8Create");
    return fn ? fn(instance, version, iid, result, outer) : E_FAIL;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    using Fn = HRESULT (WINAPI*)();
    Fn fn = SystemFunction<Fn>("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
    using Fn = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
    Fn fn = SystemFunction<Fn>("DllGetClassObject");
    return fn ? fn(clsid, iid, result) : E_FAIL;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    using Fn = HRESULT (WINAPI*)();
    Fn fn = SystemFunction<Fn>("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    using Fn = HRESULT (WINAPI*)();
    Fn fn = SystemFunction<Fn>("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

static bool LoadEarly() {
    wchar_t systemPath[MAX_PATH];
    const UINT length = GetSystemDirectoryW(systemPath, MAX_PATH);
    if (!length || length + 13 >= MAX_PATH)
        return false;
    wcscat_s(systemPath, L"\\dinput8.dll");
    systemDinput = LoadLibraryW(systemPath); // absolute Windows path, not this proxy
    if (!systemDinput)
        return false;
    wchar_t plugin[32768];
    const DWORD count = GetModuleFileNameW(nullptr, plugin, 32768);
    if (!count || count >= 32768)
        return false;
    wchar_t* slash = wcsrchr(plugin, L'\\');
    if (!slash)
        return false;
    if (wcscpy_s(slash + 1, 32768 - (slash + 1 - plugin), L"zz_VC-LocalUserFiles.asi") != 0)
        return false;
    localUserFiles = LoadLibraryW(plugin);
    return localUserFiles != nullptr;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (!LoadEarly()) {
            MessageBoxW(nullptr,
                L"VC Local User Files: early loading failed.\n"
                L"Keep dinput8.dll and zz_VC-LocalUserFiles.asi beside gta-vc.exe.\n"
                L"The game has not been started with a verified local-files redirect.",
                L"VC Local User Files", MB_OK | MB_ICONERROR);
            return FALSE;
        }
    }
    // Both modules intentionally stay loaded for the lifetime of the game.
    return TRUE;
}

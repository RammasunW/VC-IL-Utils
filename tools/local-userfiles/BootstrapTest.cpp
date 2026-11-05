#include <Windows.h>
#include <cassert>
#include <cstdio>
#include <cwchar>
#pragma comment(linker, "/alternatename:__imp__DirectInput8Create@20=__imp_DirectInput8Create")
extern "C" __declspec(dllimport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE, DWORD, REFIID, void**, void*);
int main() {
    wchar_t value[80];
    assert(GetEnvironmentVariableW(L"VC_LOCAL_EARLY_TEST", value, 80));
    assert(wcscmp(value, L"loaded before main") == 0);
    wchar_t systemPath[MAX_PATH];
    assert(GetSystemDirectoryW(systemPath, MAX_PATH));
    wcscat_s(systemPath, L"\\dinput8.dll");
    HMODULE system = LoadLibraryW(systemPath);
    assert(system);
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, void**, void*);
    Fn real = reinterpret_cast<Fn>(GetProcAddress(system, "DirectInput8Create"));
    assert(real);
    const GUID iid = {};
    void* first = nullptr;
    void* second = nullptr;
    // Invalid version deliberately avoids creating/capturing any input device.
    const HRESULT forwarded = DirectInput8Create(GetModuleHandleW(nullptr), 0, iid, &first, nullptr);
    const HRESULT original = real(GetModuleHandleW(nullptr), 0, iid, &second, nullptr);
    assert(forwarded == original && FAILED(original));
    FreeLibrary(system);
    puts("Early bootstrap loaded ASI before main; DirectInput forwarding verified");
}

// Native Win32 cursor regression check. Test windows remain offscreen.
#ifdef TEST_CN
#include "../src/Main_CN.cpp"
#else
#include "../src/Main_EN.cpp"
#endif
#include <cassert>

static int CursorCount() {
    const int count = ShowCursor(TRUE) - 1;
    ShowCursor(FALSE);
    return count;
}

static int warpCalls, clipCalls, captureCalls;
static const RECT* lastClip;
static BOOL WINAPI TestWarp(int, int) { ++warpCalls; return TRUE; }
static BOOL WINAPI TestClip(const RECT* rect) { ++clipCalls; lastClip = rect; return TRUE; }
static HWND WINAPI TestCapture(HWND window) { ++captureCalls; return window; }
static int unacquireCalls;
static HRESULT WINAPI TestUnacquire(void*) { ++unacquireCalls; return S_OK; }
static int cooperativeCalls;
static HRESULT WINAPI TestCooperative(void*, HWND, DWORD flags) {
    assert(flags == 6);
    ++cooperativeCalls;
    return S_OK;
}

static void TestDirectInputYield(HWND* windows) {
    void* deviceTable[32] = {};
    deviceTable[8] = reinterpret_cast<void*>(&TestUnacquire);
    deviceTable[13] = reinterpret_cast<void*>(&TestCooperative);
    void** device = deviceTable;
    BYTE platform[0x28] = {};
    *reinterpret_cast<void**>(platform + 0x1C) = &device;
    BYTE* platformPointer = platform;
    BYTE* reader = static_cast<BYTE*>(VirtualAlloc(nullptr, 32,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(reader);
    reader[0] = 0xA1;
    *reinterpret_cast<DWORD*>(reader + 1) = reinterpret_cast<DWORD>(&platformPointer);
    reader[5] = 0xB8; // normal game reader result, used to verify trampoline
    *reinterpret_cast<DWORD*>(reader + 6) = 77;
    reader[10] = 0xC3;
    const BYTE exclusiveCode[] = {0x8B, 0x0D, 0xEC, 0x3E, 0x78, 0x00};
    memcpy(reader + 16, exclusiveCode, sizeof(exclusiveCode));
    DWORD old;
    assert(VirtualProtect(reader, 32, PAGE_EXECUTE_READ, &old));
    assert(ToolDirectInput::Install(reinterpret_cast<DWORD>(reader),
        reinterpret_cast<DWORD>(&platformPointer), reinterpret_cast<DWORD>(reader + 16)));
    const BYTE nonexclusiveCode[] = {0x30, 0xC0, 0xC3};
    assert(memcmp(reader + 16, nonexclusiveCode, 3) == 0);
    auto read = reinterpret_cast<ToolDirectInput::Reader>(reader);
    BYTE state[20];
    memset(state, 0xFF, sizeof(state));
    SetCapture(windows[0]);
    for (int click = 0; click < 2; ++click) {
        assert(read(state) == S_OK);
        for (BYTE value : state) assert(value == 0);
        ReleaseCapture();
        SetCapture(windows[2]);
    }
    assert(unacquireCalls == 2);
    assert(cooperativeCalls == 1); // current device switches once, not every tick
    // Default activation/title-bar handling must yield before it starts.
    MapWndProc(windows[2], WM_ENTERSIZEMOVE, 0, 0);
    assert(unacquireCalls == 3);
    ReleaseCapture();
    for (int i = 0; i < 5; ++i) ShowWindow(windows[i], SW_HIDE);
    assert(read(state) == 77); // game input resumes through original code
    ToolDirectInput::Remove();
    assert(reader[0] == 0xA1);
    assert(memcmp(reader + 16, exclusiveCode, sizeof(exclusiveCode)) == 0);
    assert(read(state) == 77);
    VirtualFree(reader, 0, MEM_RELEASE);
    for (int i = 0; i < 5; ++i) ShowWindow(windows[i], SW_SHOWNOACTIVATE);
}

static void TestMouseImportGuards(HWND* windows) {
    BYTE* image = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(image);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 128;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + 128);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 512;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + 512);
    imports->Name = 600;
    strcpy_s(reinterpret_cast<char*>(image + 600), 20, "user32.dll");
    imports->OriginalFirstThunk = 640;
    imports->FirstThunk = 704;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(image + 640);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(image + 704);
    const char* functions[] = {"SetCursorPos", "ClipCursor", "SetCapture"};
    ULONG_PTR originals[] = {reinterpret_cast<ULONG_PTR>(&TestWarp),
        reinterpret_cast<ULONG_PTR>(&TestClip), reinterpret_cast<ULONG_PTR>(&TestCapture)};
    for (int i = 0; i < 3; ++i) {
        names[i].u1.AddressOfData = 800 + i * 64;
        auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + names[i].u1.AddressOfData);
        strcpy_s(name->Name, 40, functions[i]);
        slots[i].u1.Function = originals[i];
    }
    assert(ToolMouseHooks::Install(reinterpret_cast<HMODULE>(image)));
    assert(ToolMouseHooks::patchCount == 3);
    auto warp = reinterpret_cast<ToolMouseHooks::SetCursorPosFn>(slots[0].u1.Function);
    auto clip = reinterpret_cast<ToolMouseHooks::ClipCursorFn>(slots[1].u1.Function);
    auto capture = reinterpret_cast<ToolMouseHooks::SetCaptureFn>(slots[2].u1.Function);
    SetCapture(windows[0]);
    assert(ToolMouseHooks::ToolOwnsMouse());
    assert(warp(1000, 1000) && warpCalls == 0);
    RECT gameRect = {100, 100, 500, 500};
    assert(clip(&gameRect) && clipCalls == 1 && lastClip == nullptr);
    assert(capture(nullptr) == windows[0] && captureCalls == 0);
    assert(capture(windows[1]) == windows[1] && captureCalls == 1);
    // A second interaction is still protected after the first capture ends.
    ReleaseCapture();
    SetCapture(windows[2]);
    assert(warp(1000, 1000) && warpCalls == 0);
    ReleaseCapture();
    for (int i = 0; i < 5; ++i) ShowWindow(windows[i], SW_HIDE);
    assert(!ToolMouseHooks::ToolOwnsMouse());
    assert(warp(1000, 1000) && warpCalls == 1);
    assert(clip(&gameRect) && lastClip == &gameRect);
    capture(nullptr);
    assert(captureCalls == 2);
    ToolMouseHooks::Remove();
    for (int i = 0; i < 3; ++i) assert(slots[i].u1.Function == originals[i]);
    VirtualFree(image, 0, MEM_RELEASE);
    for (int i = 0; i < 5; ++i) ShowWindow(windows[i], SW_SHOWNOACTIVATE);
}

int main() {
    const int originalCount = CursorCount();
    const HCURSOR originalCursor = GetCursor();
    const char* classes[] = {"ILUtilsInfoWindow", "ILUtilsMissionWindow",
        "ILUtilsMapWindow", "ILUtilsSaveWindow", "ILUtilsVehicleSpawnerWindow"};
    HWND windows[5] = {};
    for (int i = 0; i < 5; ++i) {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = classes[i];
        assert(RegisterClassA(&wc));
        windows[i] = CreateWindowExA(WS_EX_NOACTIVATE, classes[i], "Cursor test",
            WS_POPUP, -20000, -20000, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        assert(windows[i]);
        ShowWindow(windows[i], SW_SHOWNOACTIVATE);
        assert(IsToolWindow(windows[i]));
    }
    for (int i = 0; i < 5; ++i) {
        SetCapture(windows[i]);
        assert(GetCapture() == windows[i]);
        while (CursorCount() > -2) ShowCursor(FALSE);
        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
        ForceCursorVisible();
        assert(CursorCount() == 0);
        assert(GetCursor() == LoadCursor(nullptr, IDC_IBEAM));
        // Simulate GTA clearing the thread cursor between window events.
        ForceCursorVisible(windows[i], LoadCursor(nullptr, IDC_IBEAM));
        SetCursor(nullptr);
        ForceCursorVisible();
        assert(GetCursor() == LoadCursor(nullptr, IDC_IBEAM));
        // WM_SETCURSOR knows its recipient even if cross-monitor hit testing
        // is temporarily unavailable during a drag or display transition.
        ReleaseCapture();
        SetCursor(nullptr);
        ForceCursorVisible(windows[i], LoadCursor(nullptr, IDC_ARROW));
        assert(GetCursor() == LoadCursor(nullptr, IDC_ARROW));
        SetCapture(windows[i]);
        for (int tick = 0; tick < 1000; ++tick) ForceCursorVisible();
        assert(CursorCount() == 0); // no counter inflation
        // Hiding another tool window must not hide this window's cursor.
        HWND other = windows[(i + 1) % 5];
        HideWindowAndCursor(other);
        assert(CursorCount() == 0);
        ShowWindow(other, SW_SHOWNOACTIVATE);
        ReleaseCapture();
    }
    TestMouseImportGuards(windows);
    TestDirectInputYield(windows);
    g_mapLDown = g_mapLMoved = g_mapDragging = true;
    MapWndProc(windows[2], WM_CAPTURECHANGED, 0, 0);
    assert(!g_mapLDown && !g_mapLMoved && !g_mapDragging);
    // A release without a preceding press must not run the teleport command.
    MapWndProc(windows[2], WM_LBUTTONUP, 0, MAKELPARAM(100, 100));
    for (int i = 0; i < 5; ++i) {
        DestroyWindow(windows[i]);
        UnregisterClassA(classes[i], GetModuleHandleA(nullptr));
    }
    while (CursorCount() < originalCount) ShowCursor(TRUE);
    while (CursorCount() > originalCount) ShowCursor(FALSE);
    SetCursor(originalCursor);
    puts("Tool cursor multi-window checks passed");
}

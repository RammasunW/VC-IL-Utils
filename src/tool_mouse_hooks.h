// Included after the tool-window helpers in each standalone implementation.
// Only GTA's own import slots are patched; tool windows keep normal Win32 APIs.
namespace ToolMouseHooks {
using SetCursorPosFn = BOOL (WINAPI*)(int, int);
using ClipCursorFn = BOOL (WINAPI*)(const RECT*);
using SetCaptureFn = HWND (WINAPI*)(HWND);
static SetCursorPosFn originalSetCursorPos = nullptr;
static ClipCursorFn originalClipCursor = nullptr;
static SetCaptureFn originalSetCapture = nullptr;

static bool ToolOwnsMouse() {
    return IsToolWindow(GetCapture()) || IsToolWindow(GetForegroundWindow()) ||
        IsToolWindow(GetFocus()) || IsToolWindow(ToolWindowUnderPointer());
}

static BOOL WINAPI GameSetCursorPos(int x, int y) {
    if (ToolOwnsMouse())
        return TRUE;
    return originalSetCursorPos(x, y);
}

static BOOL WINAPI GameClipCursor(const RECT* rect) {
    if (rect && ToolOwnsMouse())
        return originalClipCursor(nullptr);
    return originalClipCursor(rect);
}

static HWND WINAPI GameSetCapture(HWND window) {
    if (!IsToolWindow(window) && ToolOwnsMouse())
        return GetCapture();
    return originalSetCapture(window);
}

struct Patch {
    ULONG_PTR* slot;
    ULONG_PTR original;
    ULONG_PTR replacement;
};
static Patch patches[16] = {};
static size_t patchCount = 0;

static bool ReplaceSlot(ULONG_PTR* slot, ULONG_PTR value) {
    DWORD protection;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection))
        return false;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),
        reinterpret_cast<void*>(value));
    DWORD ignored;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    return true;
}

static void Remove() {
    while (patchCount) {
        const Patch& patch = patches[--patchCount];
        // Preserve a later mod's hook rather than removing its callback.
        if (*patch.slot == patch.replacement)
            ReplaceSlot(patch.slot, patch.original);
    }
}

static bool Install(HMODULE module) {
    if (patchCount)
        return true;
    originalSetCursorPos = nullptr;
    originalClipCursor = nullptr;
    originalSetCapture = nullptr;
    BYTE* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;
    const DWORD imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!imports)
        return false;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports);
    for (; descriptor->Name; ++descriptor) {
        if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), "user32.dll") != 0)
            continue;
        // The supplied JP and normal INTL executables retain import names.
        if (!descriptor->OriginalFirstThunk)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const char* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            ULONG_PTR replacement = 0;
            if (strcmp(name, "SetCursorPos") == 0) {
                originalSetCursorPos = reinterpret_cast<SetCursorPosFn>(slots->u1.Function);
                replacement = reinterpret_cast<ULONG_PTR>(&GameSetCursorPos);
            } else if (strcmp(name, "ClipCursor") == 0) {
                originalClipCursor = reinterpret_cast<ClipCursorFn>(slots->u1.Function);
                replacement = reinterpret_cast<ULONG_PTR>(&GameClipCursor);
            } else if (strcmp(name, "SetCapture") == 0) {
                originalSetCapture = reinterpret_cast<SetCaptureFn>(slots->u1.Function);
                replacement = reinterpret_cast<ULONG_PTR>(&GameSetCapture);
            }
            if (!replacement)
                continue;
            const ULONG_PTR previous = slots->u1.Function;
            if (patchCount == sizeof(patches) / sizeof(patches[0]) ||
                !ReplaceSlot(&slots->u1.Function, replacement)) {
                Remove();
                return false;
            }
            patches[patchCount++] = { &slots->u1.Function, previous, replacement };
        }
    }
    if (!originalSetCursorPos || !originalClipCursor || !originalSetCapture) {
        Remove();
        return false;
    }
    return true;
}
} // namespace ToolMouseHooks

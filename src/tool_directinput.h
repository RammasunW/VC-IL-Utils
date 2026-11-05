// INTL mouse-input arbitration, verified against the supported local executable.
// _InputGetMouseState: 0x5FF290; RsGlobal.ps pointer: 0x9B48F4.
// psGlobal.diMouse is at +0x1C; Unacquire is COM vtable slot 8.
namespace ToolDirectInput {
using Reader = HRESULT (__cdecl*)(void*);
using Unacquire = HRESULT (WINAPI*)(void*);
using SetCooperativeLevel = HRESULT (WINAPI*)(void*, HWND, DWORD);
static BYTE* entry = nullptr;
static BYTE original[5] = {};
static BYTE jump[5] = {};
static BYTE* trampoline = nullptr;
static Reader originalReader = nullptr;
static DWORD platformGlobal = 0;
static BYTE* exclusivePolicy = nullptr;
static BYTE exclusiveOriginal[3] = {};
static bool exclusivePatched = false;
static void* configuredDevice = nullptr;

static void ReleaseMouse() {
    if (!platformGlobal)
        return;
    BYTE* platform = *reinterpret_cast<BYTE**>(platformGlobal);
    if (!platform)
        return;
    void* device = *reinterpret_cast<void**>(platform + 0x1C);
    if (device) {
        void** vtable = *reinterpret_cast<void***>(device);
        reinterpret_cast<Unacquire>(vtable[8])(device);
        if (device != configuredDevice) {
            const HWND game = *reinterpret_cast<HWND*>(platform);
            // DISCL_NONEXCLUSIVE | DISCL_FOREGROUND. This is the policy
            // enabled by SSP's VC mouse fix, including its working JP path.
            if (SUCCEEDED(reinterpret_cast<SetCooperativeLevel>(vtable[13])(
                device, game, 0x02 | 0x04)))
                configuredDevice = device;
        }
    }
}

static HRESULT __cdecl ReadState(void* state) {
    if (ToolMouseHooks::ToolOwnsMouse()) {
        ReleaseMouse();
        // Return success with no movement/buttons: the game must not enter
        // its Acquire-and-retry loop while Windows is handling a tool click.
        if (state)
            memset(state, 0, 20); // verified DIMOUSESTATE2 size
        return S_OK;
    }
    return originalReader(state);
}

static bool WriteCode(BYTE* destination, const BYTE* source) {
    DWORD protection;
    if (!VirtualProtect(destination, 5, PAGE_EXECUTE_READWRITE, &protection))
        return false;
    memcpy(destination, source, 5);
    FlushInstructionCache(GetCurrentProcess(), destination, 5);
    DWORD ignored;
    VirtualProtect(destination, 5, protection, &ignored);
    return true;
}

static void Remove() {
    if (entry && memcmp(entry, jump, 5) == 0)
        WriteCode(entry, original);
    if (trampoline)
        VirtualFree(trampoline, 0, MEM_RELEASE);
    entry = trampoline = nullptr;
    originalReader = nullptr;
    platformGlobal = 0;
    if (exclusivePatched && exclusivePolicy) {
        DWORD protection;
        const BYTE disabled[] = {0x30, 0xC0, 0xC3}; // xor al,al; ret, as in SSP
        if (memcmp(exclusivePolicy, disabled, 3) == 0 &&
            VirtualProtect(exclusivePolicy, 3, PAGE_EXECUTE_READWRITE, &protection)) {
            memcpy(exclusivePolicy, exclusiveOriginal, 3);
            FlushInstructionCache(GetCurrentProcess(), exclusivePolicy, 3);
            DWORD ignored;
            VirtualProtect(exclusivePolicy, 3, protection, &ignored);
        }
    }
    exclusivePolicy = nullptr;
    exclusivePatched = false;
    configuredDevice = nullptr;
}

static bool Install(DWORD address, DWORD platformPointer, DWORD exclusiveAddress = 0) {
    if (entry)
        return true;
    BYTE* code = reinterpret_cast<BYTE*>(address);
    // The complete first instruction is MOV EAX,[absolute RsGlobal.ps].
    // Refuse unknown/patched prologues rather than copy unsafe instructions.
    if (!code || code[0] != 0xA1 ||
        *reinterpret_cast<const DWORD*>(code + 1) != platformPointer)
        return false;
    trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr, 10,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline)
        return false;
    memcpy(original, code, 5);
    memcpy(trampoline, code, 5);
    trampoline[5] = 0xE9;
    *reinterpret_cast<DWORD*>(trampoline + 6) =
        static_cast<DWORD>(reinterpret_cast<uintptr_t>(code + 5) -
            reinterpret_cast<uintptr_t>(trampoline + 10));
    DWORD old;
    if (!VirtualProtect(trampoline, 10, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        trampoline = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, 10);
    originalReader = reinterpret_cast<Reader>(trampoline);
    platformGlobal = platformPointer;
    jump[0] = 0xE9;
    *reinterpret_cast<DWORD*>(jump + 1) =
        static_cast<DWORD>(reinterpret_cast<uintptr_t>(&ReadState) -
            reinterpret_cast<uintptr_t>(code + 5));
    if (!WriteCode(code, jump)) {
        Remove();
        return false;
    }
    entry = code;
    if (exclusiveAddress) {
        BYTE* policy = reinterpret_cast<BYTE*>(exclusiveAddress);
        const BYTE expected[] = {0x8B, 0x0D, 0xEC, 0x3E, 0x78, 0x00};
        const BYTE disabled[] = {0x30, 0xC0, 0xC3};
        if (memcmp(policy, disabled, 3) != 0) {
            if (memcmp(policy, expected, sizeof(expected)) != 0) {
                Remove();
                return false;
            }
            DWORD protection;
            if (!VirtualProtect(policy, 3, PAGE_EXECUTE_READWRITE, &protection)) {
                Remove();
                return false;
            }
            memcpy(exclusiveOriginal, policy, 3);
            memcpy(policy, disabled, 3);
            FlushInstructionCache(GetCurrentProcess(), policy, 3);
            DWORD ignored;
            VirtualProtect(policy, 3, protection, &ignored);
            exclusivePolicy = policy;
            exclusivePatched = true;
        }
    }
    return true;
}
} // namespace ToolDirectInput

static void ReleaseGameMouseForTools() {
    ToolDirectInput::ReleaseMouse();
}

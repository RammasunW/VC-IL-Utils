// RC models have no usable door frames. Bypass entry/exit animations for Tommy.
namespace RcVehicleHooks {
using PedMethod = void (__thiscall*)(void*, void*, unsigned int);
struct Hook {
    BYTE* entry = nullptr;
    BYTE* trampoline = nullptr;
    BYTE original[7] = {};
    BYTE jump[7] = {};
};
static Hook enterHook, exitHook;
static thread_local bool handling = false;
using OpenDoorMethod = void (__thiscall*)(void*, int, int, float);
using ProcessDoorMethod = void (__thiscall*)(void*, unsigned int, unsigned int, float);
using MissingDoorMethod = bool (__thiscall*)(void*, unsigned int);
static ULONG_PTR* doorTable = nullptr;
static ULONG_PTR originalDoors[4] = {};
static const unsigned int doorSlots[] = {22, 23, 27, 29};

static bool RcModel(void* vehicle) {
    return vehicle && IsRcVehicleModel(*reinterpret_cast<const short*>(
        reinterpret_cast<const BYTE*>(vehicle) + 0x5C));
}

static void __fastcall OpenDoor(void* vehicle, void*, int component, int door, float ratio) {
    if (!RcModel(vehicle))
        reinterpret_cast<OpenDoorMethod>(originalDoors[0])(vehicle, component, door, ratio);
}
static void __fastcall ProcessDoor(void* vehicle, void*, unsigned int first, unsigned int second, float ratio) {
    if (!RcModel(vehicle))
        reinterpret_cast<ProcessDoorMethod>(originalDoors[1])(vehicle, first, second, ratio);
}
static bool __fastcall MissingDoor(void* vehicle, void*, unsigned int door) {
    return RcModel(vehicle) || reinterpret_cast<MissingDoorMethod>(originalDoors[2])(vehicle, door);
}
static bool __fastcall MissingDoorComponent(void* vehicle, void*, unsigned int component) {
    return RcModel(vehicle) || reinterpret_cast<MissingDoorMethod>(originalDoors[3])(vehicle, component);
}

static ULONG_PTR DoorReplacement(unsigned int index) {
    const ULONG_PTR functions[] = {reinterpret_cast<ULONG_PTR>(&OpenDoor),
        reinterpret_cast<ULONG_PTR>(&ProcessDoor), reinterpret_cast<ULONG_PTR>(&MissingDoor),
        reinterpret_cast<ULONG_PTR>(&MissingDoorComponent)};
    return functions[index];
}

static void RemoveDoors() {
    if (!doorTable)
        return;
    for (unsigned int i = 0; i < 4; ++i)
        if (doorTable[doorSlots[i]] == DoorReplacement(i))
            ToolMouseHooks::ReplaceSlot(&doorTable[doorSlots[i]], originalDoors[i]);
    doorTable = nullptr;
}

static bool InstallDoors(ULONG_PTR* table, const ULONG_PTR* expected) {
    for (unsigned int i = 0; i < 4; ++i)
        if (table[doorSlots[i]] != expected[i])
            return false;
    doorTable = table;
    for (unsigned int i = 0; i < 4; ++i) {
        originalDoors[i] = expected[i];
        if (!ToolMouseHooks::ReplaceSlot(&table[doorSlots[i]], DoorReplacement(i))) {
            RemoveDoors();
            return false;
        }
    }
    return true;
}

static bool PlayerRc(void* ped, void* vehicle) {
    return ped && ped == FindPlayerPedFn() && IsLiveVehiclePointer(vehicle) &&
        IsRcVehicleModel(*reinterpret_cast<const short*>(
            reinterpret_cast<const BYTE*>(vehicle) + 0x5C));
}

static bool SafeEnter(void* ped, void* vehicle) {
    if (!PlayerRc(ped, vehicle))
        return false;
    if (handling)
        return true;
    CPoolRaw* pool = *reinterpret_cast<CPoolRaw**>(g_addr->VehiclePool);
    const size_t index = (reinterpret_cast<BYTE*>(vehicle) - pool->entries) / 0x5DC;
    const int handle = (static_cast<int>(index) << 8) | pool->flags[index];
    handling = true;
    WarpPlayerIntoCarScript(handle);
    handling = false;
    return true;
}

static bool SafeExit(void* ped, void* vehicle) {
    if (!PlayerRc(ped, vehicle) || FindPlayerVehicleFn() != vehicle)
        return false;
    if (handling)
        return true;
    CVector position = TransformFromObjectSpace(vehicle, {2.5f, 0.0f, 0.5f});
    bool groundFound = false;
    using Ground = float (__cdecl*)(float, float, float, bool*);
    const float ground = reinterpret_cast<Ground>(g_addr->FindGroundZ)(
        position.x, position.y, position.z + 2.0f, &groundFound);
    if (groundFound && std::isfinite(ground))
        position.z = ground + 0.2f;
    ScriptCode code(0x012A); // WARP_PLAYER_FROM_CAR_TO_COORD: no door animations
    code.AddInt(0);
    code.AddFloat(position.x);
    code.AddFloat(position.y);
    code.AddFloat(position.z);
    handling = true;
    RunScriptCode(code);
    handling = false;
    return true;
}

static void __fastcall Enter(void* ped, void*, void* vehicle, unsigned int door) {
    if (!SafeEnter(ped, vehicle))
        reinterpret_cast<PedMethod>(enterHook.trampoline)(ped, vehicle, door);
}
static void __fastcall Exit(void* ped, void*, void* vehicle, unsigned int door) {
    if (!SafeExit(ped, vehicle))
        reinterpret_cast<PedMethod>(exitHook.trampoline)(ped, vehicle, door);
}

static bool Write(BYTE* destination, const BYTE* bytes, size_t length) {
    DWORD protection;
    if (!VirtualProtect(destination, length, PAGE_EXECUTE_READWRITE, &protection))
        return false;
    memcpy(destination, bytes, length);
    FlushInstructionCache(GetCurrentProcess(), destination, length);
    DWORD ignored;
    VirtualProtect(destination, length, protection, &ignored);
    return true;
}

static void RemoveOne(Hook& hook) {
    if (hook.entry && memcmp(hook.entry, hook.jump, 7) == 0)
        Write(hook.entry, hook.original, 7);
    if (hook.trampoline)
        VirtualFree(hook.trampoline, 0, MEM_RELEASE);
    hook = {};
}
static void Remove() { RemoveDoors(); RemoveOne(enterHook); RemoveOne(exitHook); }

static bool InstallOne(Hook& hook, DWORD address, BYTE stackSize, void* callback) {
    BYTE* entry = reinterpret_cast<BYTE*>(address);
    const BYTE expected[] = {0x53, 0x56, 0x57, 0x55, 0x83, 0xEC, stackSize};
    if (memcmp(entry, expected, 7) != 0)
        return false;
    BYTE* trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr, 12,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline)
        return false;
    memcpy(trampoline, entry, 7);
    trampoline[7] = 0xE9;
    *reinterpret_cast<DWORD*>(trampoline + 8) = static_cast<DWORD>(
        reinterpret_cast<uintptr_t>(entry + 7) - reinterpret_cast<uintptr_t>(trampoline + 12));
    memcpy(hook.original, entry, 7);
    memset(hook.jump, 0x90, 7);
    hook.jump[0] = 0xE9;
    *reinterpret_cast<DWORD*>(hook.jump + 1) = static_cast<DWORD>(
        reinterpret_cast<uintptr_t>(callback) - reinterpret_cast<uintptr_t>(entry + 5));
    hook.trampoline = trampoline;
    if (!Write(entry, hook.jump, 7)) {
        RemoveOne(hook);
        return false;
    }
    hook.entry = entry;
    FlushInstructionCache(GetCurrentProcess(), trampoline, 12);
    return true;
}

static bool Install(GameVersion version) {
    // Both JP entry points were independently matched in the local executable.
    const DWORD enter = version == VER_JP ? 0x517A20 : 0x518080;
    const DWORD exit = version == VER_JP ? 0x5165D0 : 0x516C60;
    const ULONG_PTR intlDoors[] = {0x59CF50, 0x59C950, 0x59C830, 0x59F270};
    const ULONG_PTR jpDoors[] = {0x59CD10, 0x59C710, 0x59C5F0, 0x59F030};
    // Verified from each executable's CAutomobile constructor and vtable.
    auto* table = reinterpret_cast<ULONG_PTR*>(version == VER_JP ? 0x6980C8 : 0x69AD90);
    if (!InstallDoors(table, version == VER_JP ? jpDoors : intlDoors) ||
        !InstallOne(enterHook, enter, 0x08, reinterpret_cast<void*>(&Enter)) ||
        !InstallOne(exitHook, exit, 0x40, reinterpret_cast<void*>(&Exit))) {
        Remove();
        return false;
    }
    return true;
}
}

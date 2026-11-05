#ifdef TEST_CN
#include "../src/Main_CN.cpp"
#else
#include "../src/Main_EN.cpp"
#endif
#include <cassert>

static BYTE player[0x400], cars[2][0x5DC], flags[2];
static CPoolRaw pool = {cars[0], flags, 2, 0};
static CPoolRaw* poolPointer = &pool;
static void* vehicle;
static int enterCalls, exitCalls, normalEnterCalls, normalExitCalls;
static float exitZ;
static int doorCalls, processDoorCalls, missingCalls;
static void __fastcall TestOpen(void*, void*, int, int, float) { ++doorCalls; }
static void __fastcall TestProcess(void*, void*, unsigned int, unsigned int, float) { ++processDoorCalls; }
static bool __fastcall TestMissing(void*, void*, unsigned int) { ++missingCalls; return false; }
static void* __cdecl Ped() { return player; }
static void* __cdecl Vehicle() { return vehicle; }
static float __cdecl Ground(float, float, float, bool* found) { *found = true; return 12.0f; }
static char __fastcall Script(void* raw, void*) {
    const CRunningScriptRaw* script = static_cast<CRunningScriptRaw*>(raw);
    const BYTE* code = reinterpret_cast<const BYTE*>(static_cast<uint32_t>(script->ip));
    const unsigned short opcode = *reinterpret_cast<const unsigned short*>(code);
    if (opcode == 0x0369) ++enterCalls;
    else if (opcode == 0x012A) { ++exitCalls; exitZ = *reinterpret_cast<const float*>(code + 18); }
    else assert(false);
    return 0;
}

static BYTE* FakeMethod(BYTE stack, int* calls) {
    BYTE* code = static_cast<BYTE*>(VirtualAlloc(nullptr, 32, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(code);
    const BYTE prologue[] = {0x53,0x56,0x57,0x55,0x83,0xEC,stack};
    memcpy(code, prologue, 7);
    code[7] = 0xFF; code[8] = 0x05; // increment fallback-call count
    *reinterpret_cast<DWORD*>(code + 9) = reinterpret_cast<DWORD>(calls);
    const BYTE epilogue[] = {0x83,0xC4,stack,0x5D,0x5F,0x5E,0x5B,0xC2,0x08,0x00};
    memcpy(code + 13, epilogue, sizeof(epilogue));
    return code;
}

static int imeCharacters, controlKeys;
static LRESULT CALLBACK TestWindow(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_IME_CHAR) ++imeCharacters;
    if (msg == WM_KEYDOWN && wp == VK_CONTROL) ++controlKeys;
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main() {
    AddrTable addresses = {};
    addresses.VehiclePool = reinterpret_cast<DWORD>(&poolPointer);
    addresses.FindGroundZ = reinterpret_cast<DWORD>(&Ground);
    BYTE modelInfo[0x40] = {};
    modelInfo[0x19] = MODEL_INFO_VEHICLE_RAW;
    void* modelTable[VEHICLE_SPAWNER_MODEL_LIMIT] = {};
    const int rcModels[] = {171,194,195,231};
    for (int model : rcModels) modelTable[model] = modelInfo;
    modelTable[130] = modelInfo;
    addresses.ModelInfoTable = reinterpret_cast<DWORD>(modelTable);
    g_addr = &addresses;
    FindPlayerPedFn = Ped;
    FindPlayerVehicleFn = Vehicle;
    ProcessOneCommandFn = reinterpret_cast<ProcessOneCommand_t>(&Script);
    flags[0] = 1; flags[1] = 2;
    *reinterpret_cast<CVector*>(cars[0] + 0x04) = {1,0,0};
    *reinterpret_cast<CVector*>(cars[0] + 0x34) = {45,50,10};
    BYTE* enterCode = FakeMethod(8, &normalEnterCalls);
    BYTE* exitCode = FakeMethod(0x40, &normalExitCalls);
    assert(RcVehicleHooks::InstallOne(RcVehicleHooks::enterHook,
        reinterpret_cast<DWORD>(enterCode), 8, reinterpret_cast<void*>(&RcVehicleHooks::Enter)));
    assert(RcVehicleHooks::InstallOne(RcVehicleHooks::exitHook,
        reinterpret_cast<DWORD>(exitCode), 0x40, reinterpret_cast<void*>(&RcVehicleHooks::Exit)));
    auto enter = reinterpret_cast<RcVehicleHooks::PedMethod>(enterCode);
    auto exit = reinterpret_cast<RcVehicleHooks::PedMethod>(exitCode);
    for (int model : rcModels) {
        assert(!IsForbiddenSpawnModel(model));
        *reinterpret_cast<short*>(cars[0] + 0x5C) = static_cast<short>(model);
        vehicle = nullptr;
        enter(player, cars[0], 0);
        vehicle = cars[0];
        exit(player, cars[0], 0);
        assert(fabsf(exitZ - 12.2f) < 0.001f);
    }
    assert(enterCalls == 4 && exitCalls == 4);
    assert(normalEnterCalls == 0 && normalExitCalls == 0);
    *reinterpret_cast<short*>(cars[0] + 0x5C) = 130;
    enter(player, cars[0], 0); exit(player, cars[0], 0);
    assert(normalEnterCalls == 1 && normalExitCalls == 1);
    RcVehicleHooks::Remove();
    assert(enterCode[0] == 0x53 && exitCode[0] == 0x53);
    VirtualFree(enterCode, 0, MEM_RELEASE); VirtualFree(exitCode, 0, MEM_RELEASE);
    // Spawn permission must not make RC models eligible for rig-all bombs.
    for (int i = 0; i < 2; ++i) {
        cars[i][0x50] = 2 | (3 << 3);
        *reinterpret_cast<float*>(cars[i] + 0x204) = 1000;
    }
    *reinterpret_cast<short*>(cars[0] + 0x5C) = 171;
    *reinterpret_cast<short*>(cars[1] + 0x5C) = 130;
    assert(RigAllCarBombs() == 1 && cars[0][0x1FE] == 0 && cars[1][0x1FE] == 11);

    ULONG_PTR doors[32] = {};
    const ULONG_PTR originals[] = {reinterpret_cast<ULONG_PTR>(&TestOpen),
        reinterpret_cast<ULONG_PTR>(&TestProcess), reinterpret_cast<ULONG_PTR>(&TestMissing),
        reinterpret_cast<ULONG_PTR>(&TestMissing)};
    for (int i = 0; i < 4; ++i) doors[RcVehicleHooks::doorSlots[i]] = originals[i];
    assert(RcVehicleHooks::InstallDoors(doors, originals));
    // Calls through the automobile vtable cover NPCs as well as Tommy.
    for (int model : rcModels) {
        *reinterpret_cast<short*>(cars[0] + 0x5C) = static_cast<short>(model);
        reinterpret_cast<RcVehicleHooks::OpenDoorMethod>(doors[22])(cars[0], 8, 2, 1.0f);
        reinterpret_cast<RcVehicleHooks::ProcessDoorMethod>(doors[23])(cars[0], 0, 0, 1.0f);
        assert(reinterpret_cast<RcVehicleHooks::MissingDoorMethod>(doors[27])(cars[0], 2));
        assert(reinterpret_cast<RcVehicleHooks::MissingDoorMethod>(doors[29])(cars[0], 8));
    }
    assert(doorCalls == 0 && processDoorCalls == 0 && missingCalls == 0);
    *reinterpret_cast<short*>(cars[0] + 0x5C) = 130;
    reinterpret_cast<RcVehicleHooks::OpenDoorMethod>(doors[22])(cars[0], 8, 2, 1.0f);
    reinterpret_cast<RcVehicleHooks::ProcessDoorMethod>(doors[23])(cars[0], 0, 0, 1.0f);
    assert(!reinterpret_cast<RcVehicleHooks::MissingDoorMethod>(doors[27])(cars[0], 2));
    assert(!reinterpret_cast<RcVehicleHooks::MissingDoorMethod>(doors[29])(cars[0], 8));
    assert(doorCalls == 1 && processDoorCalls == 1 && missingCalls == 2);
    RcVehicleHooks::RemoveDoors();
    for (int i = 0; i < 4; ++i) assert(doors[RcVehicleHooks::doorSlots[i]] == originals[i]);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = TestWindow;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "Grand theft auto 3";
    assert(RegisterClassA(&wc));
    HWND game = CreateWindowA(wc.lpszClassName, "IME test", WS_POPUP,
        -20000,-20000,100,100,nullptr,nullptr,wc.hInstance,nullptr);
    HWND tool = CreateWindowA("EDIT", "tool", WS_POPUP,
        -20000,-20000,100,100,nullptr,nullptr,wc.hInstance,nullptr);
    HIMC gameContext = ImmCreateContext(), toolContext = ImmCreateContext();
    assert(game && tool && gameContext && toolContext);
    ImmAssociateContext(game, gameContext); ImmAssociateContext(tool, toolContext);
    GameIme::Ensure();
    assert(GameIme::game == game && !ImmGetContext(game));
    HIMC currentToolContext = ImmGetContext(tool);
    assert(currentToolContext == toolContext);
    ImmReleaseContext(tool, currentToolContext);
    SendMessageA(game, WM_IME_CHAR, 0x4E2D, 0);
    SendMessageA(game, WM_KEYDOWN, VK_CONTROL, 0);
    assert(imeCharacters == 0 && controlKeys == 1);
    GameIme::Remove();
    HIMC restored = ImmGetContext(game);
    assert(restored == gameContext);
    ImmReleaseContext(game, restored);
    ImmAssociateContext(game, nullptr); ImmAssociateContext(tool, nullptr);
    ImmDestroyContext(gameContext); ImmDestroyContext(toolContext);
    DestroyWindow(game); DestroyWindow(tool);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    puts("Game IME and RC entry/exit checks passed");
}

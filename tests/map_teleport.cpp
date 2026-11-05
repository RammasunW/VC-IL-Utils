#ifdef TEST_CN
#include "../src/Main_CN.cpp"
#else
#include "../src/Main_EN.cpp"
#endif
#include <cassert>
#include <limits>

static BYTE fakePed[0x400], fakeVehicles[2][0x5DC], flags[2];
static CPoolRaw fakePool = {fakeVehicles[0], flags, 2, 0};
static CPoolRaw* poolPointer = &fakePool;
static void* currentPed = fakePed;
static void* currentVehicle;
static DWORD now;
static int queries, readyOn, modelRequests, collisionRequests, moves;
static float groundHeight, lastZ, lastX;
static void* __cdecl Ped() { return currentPed; }
static void* __cdecl Vehicle() { return currentVehicle; }
static DWORD WINAPI Now() { return now; }
static void Move(float x, float, float z) { ++moves; lastX = x; lastZ = z; }
static void __cdecl Models(const CVector* position, unsigned int) {
    assert(position->z == 0.0f);
    ++modelRequests;
}
static char __fastcall Script(void*, void*) { ++collisionRequests; return 0; }
static float __cdecl Ground(float, float, float z, bool* found) {
    assert(z == 1000.0f);
    *found = ++queries >= readyOn;
    return groundHeight;
}

static void Reset(bool inVehicle = false) {
    MapTeleportState::Cancel();
    memset(fakePed, 0, sizeof(fakePed));
    memset(fakeVehicles, 0, sizeof(fakeVehicles));
    flags[0] = 1;
    flags[1] = 0x80;
    currentPed = fakePed;
    currentVehicle = inVehicle ? fakeVehicles[0] : nullptr;
    now = 0;
    queries = modelRequests = collisionRequests = moves = 0;
    readyOn = 3;
    groundHeight = 12.0f;
}

int main() {
    AddrTable addresses = {};
    addresses.VehiclePool = reinterpret_cast<DWORD>(&poolPointer);
    addresses.RequestSceneModels = reinterpret_cast<DWORD>(&Models);
    addresses.FindGroundZ = reinterpret_cast<DWORD>(&Ground);
    g_addr = &addresses;
    FindPlayerPedFn = Ped;
    FindPlayerVehicleFn = Vehicle;
    ProcessOneCommandFn = reinterpret_cast<ProcessOneCommand_t>(&Script);
    MapTeleportState::move = Move;
    MapTeleportState::clock = Now;

    Reset();
    fakePed[0x11B] = 0xA0;
    MapTeleport(100, 200);
    assert(lastZ == 100 && (fakePed[0x11B] & 1));
    UpdatePendingTeleport();
    UpdatePendingTeleport();
    assert(MapTeleportState::pending && moves == 1);
    UpdatePendingTeleport();
    assert(!MapTeleportState::pending && lastZ == 12.5f);
    assert(fakePed[0x11B] == 0xA0);
    assert(modelRequests == 4 && collisionRequests == 4);

    Reset(true);
    fakeVehicles[0][0x11A] = 0xA0;
    fakeVehicles[0][0x11B] = 0xA1; // already frozen before teleport
    readyOn = 99;
    MapTeleport(100, 200);
    assert((fakeVehicles[0][0x11A] & 4) && fakePed[0x11B] == 0);
    for (int i = 0; i < 3; ++i) UpdatePendingTeleport();
    assert(MapTeleportState::pending);
    UpdatePendingTeleport();
    assert(!MapTeleportState::pending && lastZ == 6.5f);
    assert(fakeVehicles[0][0x11A] == 0xA0 && fakeVehicles[0][0x11B] == 0xA1);

    Reset();
    readyOn = 99;
    MapTeleport(100, 200);
    now = 150;
    UpdatePendingTeleport();
    assert(!MapTeleportState::pending && queries == 1 && lastZ == 6.5f);

    Reset();
    MapTeleport(100, 200);
    MapTeleport(300, 400);
    readyOn = 1;
    UpdatePendingTeleport();
    assert(lastX == 300 && !MapTeleportState::pending && !(fakePed[0x11B] & 1));

    Reset(true);
    MapTeleport(100, 200);
    currentVehicle = nullptr; // player exits while destination is pending
    UpdatePendingTeleport();
    assert(!MapTeleportState::pending && !(fakeVehicles[0][0x11B] & 1));

    Reset(true);
    MapTeleport(100, 200);
    ++flags[0]; // pool slot recycled: do not alter the replacement vehicle
    fakeVehicles[0][0x11B] = 0x55;
    UpdatePendingTeleport();
    assert(!MapTeleportState::pending && fakeVehicles[0][0x11B] == 0x55);

    Reset();
    readyOn = 1;
    groundHeight = std::numeric_limits<float>::quiet_NaN();
    MapTeleport(100, 200);
    for (int i = 0; i < 4; ++i) UpdatePendingTeleport();
    assert(lastZ == 6.5f);
    puts("Short map teleport checks passed");
}

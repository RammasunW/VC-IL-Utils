// Short asynchronous map teleport: stream, hold, query, then land.
namespace MapTeleportState {
using GroundQuery = float (__cdecl*)(float, float, float, bool*);
using RequestModels = void (__cdecl*)(const CVector*, unsigned int);
static void (*move)(float, float, float) = TeleportToWorld;
static DWORD (WINAPI* clock)() = GetTickCount;
static const unsigned int MAX_FRAMES = 4;
static const DWORD MAX_WAIT_MS = 150;
static const float CLEARANCE = 0.5f;
static const float SEA_LEVEL = 6.0f; // stock VC water.dat / WATERPRO.DAT
static const float HOLD_Z = 100.0f;
static bool pending = false;
static void* ped = nullptr;
static void* vehicle = nullptr;
static void* entity = nullptr;
static BYTE vehicleGeneration = 0;
static BYTE frozenBefore = 0, massBefore = 0;
static float x, y;
static unsigned int frames;
static DWORD started;

static bool VehicleStillExists() {
    if (!vehicle || !IsLiveVehiclePointer(vehicle))
        return false;
    CPoolRaw* pool = *reinterpret_cast<CPoolRaw**>(g_addr->VehiclePool);
    const size_t index = (reinterpret_cast<BYTE*>(vehicle) - pool->entries) / 0x5DC;
    return pool->flags[index] == vehicleGeneration;
}

static bool EntityStillExists() {
    return vehicle ? VehicleStillExists() : ped && FindPlayerPedFn() == ped;
}

static void StopMotion() {
    BYTE* bytes = reinterpret_cast<BYTE*>(entity);
    *reinterpret_cast<CVector*>(bytes + 0x70) = {}; // move speed
    *reinterpret_cast<CVector*>(bytes + 0x7C) = {}; // turn speed
    bytes[0x11B] |= 0x01; // CPhysical::bIsFrozen
    if (vehicle)
        bytes[0x11A] |= 0x04; // CPhysical::bInfiniteMass
}

static void Cancel() {
    if (pending && EntityStillExists()) {
        BYTE* bytes = reinterpret_cast<BYTE*>(entity);
        bytes[0x11B] = (bytes[0x11B] & ~0x01) | frozenBefore;
        if (vehicle)
            bytes[0x11A] = (bytes[0x11A] & ~0x04) | massBefore;
    }
    pending = false;
    ped = vehicle = entity = nullptr;
}

static void RequestDestination() {
    const CVector position = {x, y, 0.0f};
    reinterpret_cast<RequestModels>(g_addr->RequestSceneModels)(&position, 0);
    ScriptCode code(0x04E4); // REQUEST_COLLISION x y (non-blocking)
    code.AddFloat(x);
    code.AddFloat(y);
    RunScriptCode(code);
}

static void Start(float destinationX, float destinationY) {
    Cancel(); // a new click replaces any pending destination cleanly
    if (!std::isfinite(destinationX) || !std::isfinite(destinationY))
        return;
    ped = FindPlayerPedFn();
    if (!ped)
        return;
    vehicle = FindPlayerVehicleFn();
    if (vehicle) {
        if (!IsLiveVehiclePointer(vehicle)) {
            ped = vehicle = nullptr;
            return;
        }
        CPoolRaw* pool = *reinterpret_cast<CPoolRaw**>(g_addr->VehiclePool);
        const size_t index = (reinterpret_cast<BYTE*>(vehicle) - pool->entries) / 0x5DC;
        vehicleGeneration = pool->flags[index];
    }
    entity = vehicle ? vehicle : ped;
    BYTE* bytes = reinterpret_cast<BYTE*>(entity);
    frozenBefore = bytes[0x11B] & 0x01;
    massBefore = bytes[0x11A] & 0x04;
    x = destinationX;
    y = destinationY;
    frames = 0;
    started = clock();
    pending = true;
    StopMotion();
    RequestDestination();
    // Moving to the destination lets the normal camera/scene streamer catch
    // up. No synchronous LOAD_SCENE or busy waiting on the game thread.
    move(x, y, HOLD_Z);
    StopMotion();
}

static void Update() {
    if (!pending)
        return;
    if (!EntityStillExists() || FindPlayerPedFn() != ped ||
        FindPlayerVehicleFn() != vehicle) {
        Cancel();
        return;
    }
    StopMotion();
    RequestDestination();
    ++frames;
    bool found = false;
    const float ground = reinterpret_cast<GroundQuery>(g_addr->FindGroundZ)(
        x, y, 1000.0f, &found);
    found = found && std::isfinite(ground);
    if (found || frames >= MAX_FRAMES || clock() - started >= MAX_WAIT_MS) {
        // Sea-level fallback is deliberate. A miss can also mean that
        // collision has not finished loading within this very short window.
        const float surface = found && ground > SEA_LEVEL ? ground : SEA_LEVEL;
        // Existing coordinate opcodes add the entity's model-base offset.
        move(x, y, surface + CLEARANCE);
        StopMotion();
        Cancel();
    }
}
} // namespace MapTeleportState

static void MapTeleport(float x, float y) {
    MapTeleportState::Start(x, y);
}

static void UpdatePendingTeleport() {
    MapTeleportState::Update();
}

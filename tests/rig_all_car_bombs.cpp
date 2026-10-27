// Build as an x86 console executable; runs without loading Vice City.
#include "../src/Main.cpp"
#include <cassert>

static void* __cdecl TestPlayerPed() {
    return reinterpret_cast<void*>(0x12345678);
}

int main() {
    const int models[] = {130, 166, 136, 155, 165, 171, 180, 181, 194, 195,
        231, 130, 130, 130, 130, -1, 129, 140};
    const int slots = sizeof(models) / sizeof(models[0]);
    BYTE entries[slots][0x5DC] = {};
    BYTE flags[slots] = {};
    BYTE modelInfo[0x40] = {};
    void* modelTable[VEHICLE_SPAWNER_MODEL_LIMIT] = {};
    modelInfo[0x19] = MODEL_INFO_VEHICLE_RAW;
    // All synthetic models use the supported automobile hierarchy. The
    // explicit blacklist must still reject the special/RC model IDs.
    for (int model : models)
        if (model >= 130)
            modelTable[model] = modelInfo;

    for (int i = 0; i < slots; ++i) {
        entries[i][0x50] = 2 | (3 << 3); // vehicle, physics
        *reinterpret_cast<short*>(entries[i] + 0x5C) = static_cast<short>(models[i]);
        *reinterpret_cast<float*>(entries[i] + 0x204) = 1000.0f;
    }
    flags[11] = 0x80; // empty slot retaining stale vehicle bytes
    entries[12][0x50] = 2 | (5 << 3); // wrecked even with positive health
    *reinterpret_cast<float*>(entries[13] + 0x204) = 0.0f;
    entries[14][0x50] = 3 | (3 << 3); // wrong entity type
    modelTable[140] = nullptr;

    CPoolRaw pool = {entries[0], flags, slots, 0};
    CPoolRaw* poolPointer = &pool;
    AddrTable addresses = {};
    addresses.VehiclePool = reinterpret_cast<DWORD>(&poolPointer);
    addresses.ModelInfoTable = reinterpret_cast<DWORD>(modelTable);
    g_addr = &addresses;
    FindPlayerPedFn = TestPlayerPed;

    assert(RigAllCarBombs() == 4);
    for (int i = 0; i < slots; ++i) {
        assert(entries[i][0x1FE] == (i < 4 ? 11 : 0));
        assert(*reinterpret_cast<void**>(entries[i] + 0x210) ==
            (i < 4 ? TestPlayerPed() : nullptr));
    }
    assert(RigAllCarBombs() == 4); // repeated activation is safe
    poolPointer = nullptr;
    assert(RigAllCarBombs() == 0);
    poolPointer = &pool;
    pool.size = 0;
    assert(RigAllCarBombs() == 0);
    puts("RigAllCarBombs pool checks passed");
}

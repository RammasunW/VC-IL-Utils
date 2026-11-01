#ifdef TEST_CN
#include "../src/Main_CN.cpp"
#else
#include "../src/Main_EN.cpp"
#endif
#include <cassert>
#include <cwchar>

int main() {
    BYTE vehicles[2][0x5DC] = {};
    BYTE flags[2] = {};
    CPoolRaw pool = {vehicles[0], flags, 2, 0};
    CPoolRaw* poolPointer = &pool;
    void* list[2] = {vehicles[0], vehicles[1]};
    unsigned int times[2] = {123456, 987654};
    AddrTable addresses = {};
    addresses.VehiclePool = reinterpret_cast<DWORD>(&poolPointer);
    addresses.InterestingVehicles = reinterpret_cast<DWORD>(list);
    addresses.InterestingVehicleTimes = reinterpret_cast<DWORD>(times);
    g_addr = &addresses;
    *reinterpret_cast<short*>(vehicles[0] + 0x5C) = 166;
    *reinterpret_cast<short*>(vehicles[1] + 0x5C) = 141;
    char text[200];
    FormatInterestingVehicleLine(0, text, sizeof(text));
    assert(strstr(text, "Angel [123"));
    FormatInterestingVehicleLine(1, text, sizeof(text));
    assert(strstr(text, "Infernus [987"));
#ifdef TEST_CN
    wchar_t wide[200];
    MultiByteToWideChar(936, 0, text, -1, wide, 200);
    assert(wcscmp(wide, L"\u4e8c\u8f66\u5b9a\u5f8b\u69fd\u4f4d2: Infernus [987\u79d2]") == 0);
#else
    assert(strcmp(text, "Interesting vehicle 2: Infernus [987 s]") == 0);
#endif
    list[0] = nullptr;
    FormatInterestingVehicleLine(0, text, sizeof(text));
    assert(strstr(text, "[0"));
    list[0] = vehicles[0] + 1;
    assert(!IsLiveVehiclePointer(list[0]));
    list[0] = vehicles[0];
    flags[0] = 0x80;
    assert(!IsLiveVehiclePointer(list[0]));
    FormatInterestingVehicleLine(0, text, sizeof(text));
    assert(!strstr(text, "Angel"));
    FormatCreatedByLine(nullptr, text, sizeof(text));
    assert(text[0] == '\0');
    for (BYTE value = 1; value <= 4; ++value) {
        VehicleCreatedBy(vehicles[1]) = value;
        FormatCreatedByLine(vehicles[1], text, sizeof(text));
        char number[8];
        sprintf_s(number, "(%u)", static_cast<unsigned int>(value));
        assert(strstr(text, number));
#ifdef TEST_CN
        const wchar_t* names[] = {L"\u8857\u8f66", L"\u4efb\u52a1\u8f66",
            L"\u505c\u8f66\u573a\u7528\u8f66", L"\u6c38\u4e45\u8f66"};
        MultiByteToWideChar(936, 0, text, -1, wide, 200);
        assert(wcsstr(wide, names[value - 1]));
#else
        const char* names[] = {"Random", "Mission", "Parked", "Permanent"};
        assert(strstr(text, names[value - 1]));
#endif
    }
    poolPointer = nullptr;
    assert(!IsLiveVehiclePointer(list[1]));
    puts("Interesting vehicle panel checks passed");
}

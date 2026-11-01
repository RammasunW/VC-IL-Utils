// Build as x86. Define TEST_CN to exercise the GBK Chinese implementation.
#ifdef TEST_CN
#include "../src/Main_CN.cpp"
#else
#include "../src/Main_EN.cpp"
#endif
#include <cassert>
#include <cwchar>

static wchar_t lastMessage[256];
static void __cdecl CaptureMessage(const wchar_t* text, unsigned int, unsigned short) {
    wcscpy_s(lastMessage, text);
}

int main() {
    int limit = 120000;
    short status = 0;
    AddrTable addresses = {};
    addresses.RampageTimeLimit = reinterpret_cast<DWORD>(&limit);
    addresses.RampageStatus = reinterpret_cast<DWORD>(&status);
    g_addr = &addresses;
    AddMessageJumpQFn = CaptureMessage;

    const short inactiveStates[] = {0, 2, 3};
    for (short inactive : inactiveStates) {
        status = inactive;
        AdjustRampageTimer(5000);
        assert(limit == 120000);
        assert(wcscmp(lastMessage, L"Rampage is not active") == 0);
    }
    status = 1;
    AdjustRampageTimer(5000);
    assert(limit == 125000);
    assert(wcscmp(lastMessage, L"Rampage timer +5s") == 0);
    AdjustRampageTimer(-5000);
    assert(limit == 120000);
    assert(wcscmp(lastMessage, L"Rampage timer -5s") == 0);
    limit = 3000;
    AdjustRampageTimer(-5000);
    assert(limit == 0);
    limit = INT_MAX - 1;
    AdjustRampageTimer(5000);
    assert(limit == INT_MAX);
    assert(ParseKey("PGUP") == VK_PRIOR);
    assert(ParseKey("PGDN") == VK_NEXT);
    assert(kAddr_INTL.RampageTimeLimit == 0x974BE0);
    assert(kAddr_JP.RampageTimeLimit == 0x971BE8);
    puts("Rampage timer checks passed");
}

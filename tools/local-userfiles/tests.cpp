#define LOCAL_USERFILES_TEST
#include "Main.cpp"
#include <cassert>
static const char* receivedPath;
static char runtimePath[181];
static int fileInitCalls;
static char actualOpenedPath[512];
static void __cdecl FileInit() { ++fileInitCalls; }
static int __cdecl NativeOpen(const char* file, const char* mode) {
    strcpy_s(actualOpenedPath, file);
    FILE* opened = nullptr;
    fopen_s(&opened, file, mode);
    return reinterpret_cast<int>(opened);
}
static unsigned int __cdecl NativeRead(void* buffer, unsigned int size,
    unsigned int count, void* file) {
    return fread(buffer, size, count, static_cast<FILE*>(file));
}
static int __cdecl NativeClose(void* file) { return fclose(static_cast<FILE*>(file)); }
static char* __cdecl SilentPatchGetter() {
    return const_cast<char*>("C:\\Documents\\GTA Vice City User Files");
}

static void TestHooks(bool sspBefore, bool sspAfter) {
    using namespace LocalUserFiles;
    BYTE* image = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    assert(image);
    const Profile profile = {0, 0x100, 0x200, 0x500, 0x700, 0x300, 0x800,
        0x350, 0x400, 0x450, 0x600};
    const BYTE prefix[] = {0x56, 0x57, 0x83, 0xEC, 0x10};
    memcpy(image + 0x100, prefix, 5);
    image[0x200] = 0x8B; image[0x201] = 0x44;
    image[0x202] = 0x24; image[0x203] = 0x04; image[0x204] = 0x68;
    *reinterpret_cast<DWORD*>(image + 0x205) = reinterpret_cast<DWORD>(image + 0x700);
    image[0x209] = 0xA3; // original setter records its actual argument
    *reinterpret_cast<DWORD*>(image + 0x20A) = reinterpret_cast<DWORD>(&receivedPath);
    const BYTE epilogue[] = {0x83, 0xC4, 0x04, 0xC3};
    memcpy(image + 0x20E, epilogue, sizeof(epilogue));
    Jump(image + 0x300, 0xE8, image + 0x300, image + 0x100);
    Jump(image + 0x34B, 0xE8, image + 0x34B, image + 0x450);
    const BYTE inlinePrefix[] = {0xC7,0x84,0x24,0xC0,0,0,0,0,0,0};
    memcpy(image + 0x350, inlinePrefix, sizeof(inlinePrefix));
    Jump(image + 0x450, 0xE9, image + 0x450, &FileInit);
    image[0x400] = 0x50; // push returned directory and use original continuation
    Jump(image + 0x401, 0xE8, image + 0x401, image + 0x200);
    image[0x406] = 0x59; image[0x407] = 0xC3;
    const BYTE openPrefix[] = {0x8B,0x44,0x24,0x04,0x8B,0x4C,0x24,0x08,0x51,0x50};
    memcpy(image + 0x600, openPrefix, sizeof(openPrefix));
    Jump(image + 0x60A, 0xE8, image + 0x60A, &NativeOpen);
    BYTE sspJump[5];
    Jump(sspJump, 0xE9, image + 0x100, &SilentPatchGetter);
    if (sspBefore) memcpy(image + 0x100, sspJump, 5);
    strcpy_s(path, runtimePath);
    receivedPath = nullptr;
    assert(Install(image, profile));
    const BYTE readPrefix[] = {0x8B,0x44,0x24,0x0C,0x8B,0x4C,0x24,0x04,
        0x51,0x50,0x8B,0x44,0x24,0x10,0x6A,0x01,0x50};
    memcpy(image + 0x900, readPrefix, sizeof(readPrefix));
    Jump(image + 0x911, 0xE8, image + 0x911, &NativeRead);
    const BYTE closePrefix[] = {0x8B,0x44,0x24,0x04,0x50};
    memcpy(image + 0xA00, closePrefix, sizeof(closePrefix));
    Jump(image + 0xA05, 0xE8, image + 0xA05, &NativeClose);
    assert(InstallReadTraceEntries(image + 0x900, image + 0xA00));
    gameImage = image;
    gameImageSize = 4096;
    if (sspAfter) {
        // Simulate SSP overwriting the getter later in the ASI load sequence.
        assert(Write(image + 0x100, sspJump, 5));
        assert(strstr(reinterpret_cast<char*(*)()>(image + 0x100)(), "Documents"));
        BYTE sspInline[10];
        Jump(sspInline, 0xE8, image + 0x350, &SilentPatchGetter);
        Jump(sspInline + 5, 0xE9, image + 0x355, image + 0x400);
        assert(Write(image + 0x350, sspInline, 10));
    }
    const int callsBefore = fileInitCalls;
    reinterpret_cast<void(*)()>(image + 0x34B)();
    assert(fileInitCalls == callsBefore + 1);
    assert(receivedPath && strcmp(receivedPath, path) == 0);
    assert(strcmp(reinterpret_cast<char*>(image + 0x500), path) == 0);
    assert(strcmp(reinterpret_cast<char*(*)()>(image + 0x100)(), path) == 0);
    // Exercise real relative-file reads/writes after the directory switch.
    // Simulate the getter being overwritten again and the save prefix cached
    // with Documents after startup; the file-manager path must remain local.
    wchar_t previousDirectory[32768];
    assert(GetCurrentDirectoryW(_countof(previousDirectory), previousDirectory));
    strcpy_s(reinterpret_cast<char*>(image + 0x800), 200, "C:\\Documents\\GTAVCsf");
    assert(Write(image + 0x100, sspJump, 5));
    assert(reinterpret_cast<int(*)()>(image + 0x300)() == 0);
    char expectedPrefix[200];
    sprintf_s(expectedPrefix, "%s\\GTAVCsf", runtimePath);
    assert(strcmp(reinterpret_cast<char*>(image + 0x800), expectedPrefix) == 0);
    HANDLE probeFile = CreateFileA("local-settings-test.tmp", GENERIC_WRITE | GENERIC_READ,
        0, nullptr, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    assert(probeFile != INVALID_HANDLE_VALUE);
    const char marker[] = "local profile";
    DWORD written;
    assert(WriteFile(probeFile, marker, sizeof(marker), &written, nullptr));
    assert(SetFilePointer(probeFile, 0, nullptr, FILE_BEGIN) == 0);
    char readBack[sizeof(marker)];
    DWORD read;
    assert(ReadFile(probeFile, readBack, sizeof(readBack), &read, nullptr));
    assert(memcmp(marker, readBack, sizeof(marker)) == 0);
    CloseHandle(probeFile);
    assert(CreateDirectoryA("trace-test", nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
    FILE* fixture = nullptr;
    assert(fopen_s(&fixture, "trace-test\\gta_vc.set", "wb") == 0);
    unsigned char profileData[64] = {3,0,0,0};
    memcpy(profileData + 4, marker, sizeof(marker));
    assert(fwrite(profileData, 1, sizeof(profileData), fixture) == sizeof(profileData));
    fclose(fixture);
    sprintf_s(path, "%s\\trace-test", runtimePath);
    auto openSettings = reinterpret_cast<int (__cdecl*)(const char*, const char*)>(image + 0x600);
    // Deliberately supply a different path: the settings open must use the
    // configured absolute directory, regardless of the process directory.
    FILE* loaded = reinterpret_cast<FILE*>(openSettings("Z:\\wrong\\gta_vc.set", "rb"));
    assert(loaded);
    char expectedSettings[220];
    sprintf_s(expectedSettings, "%s\\gta_vc.set", path);
    assert(strcmp(actualOpenedPath, expectedSettings) == 0);
    auto readSettings = reinterpret_cast<int (__cdecl*)(int,char*,int)>(image + 0x900);
    auto closeSettings = reinterpret_cast<int (__cdecl*)(int)>(image + 0xA00);
    char header[29];
    assert(readSettings(reinterpret_cast<int>(loaded), header, sizeof(header)) == 29);
    assert(memcmp(header, profileData, 29) == 0);
    assert(fseek(loaded, 0, SEEK_SET) == 0);
    int version = 0;
    assert(readSettings(reinterpret_cast<int>(loaded), reinterpret_cast<char*>(&version), 4) == 4);
    assert(version == 3);
    char* remainder = reinterpret_cast<char*>(image + 0xB00);
    assert(readSettings(reinterpret_cast<int>(loaded), remainder, 80) == 60);
    assert(memcmp(remainder, profileData + 4, 60) == 0);
    assert(loadedCount == 1 && ChangedValues() == 0);
    remainder[0] ^= 1;
    assert(ChangedValues() == 1);
    remainder[0] ^= 1;
    assert(ChangedValues() == 0);
    assert(LocalUserFiles::settings[0].calls == 3);
    assert(LocalUserFiles::settings[0].bytes == 93);
    assert(LocalUserFiles::settings[0].shorts == 1);
    assert(closeSettings(reinterpret_cast<int>(loaded)) == 0);
    assert(LocalUserFiles::settings[0].handle == 0);
    strcpy_s(path, runtimePath);
    assert(DeleteFileA("trace-test\\gta_vc.set"));
    char fullSettings[MAX_PATH];
    assert(GetFullPathNameA("gta_vc.set", MAX_PATH, fullSettings, nullptr));
    assert(strncmp(fullSettings, runtimePath, strlen(runtimePath)) == 0);
    assert(SetCurrentDirectoryW(previousDirectory));
    // Reapply once so unload must restore the last observed SSP hook.
    assert(EnsureGetter());
    const BYTE firstGetterByte = 0xE9;
    Remove();
    assert(image[0x100] == firstGetterByte);
    assert(image[0x200] == 0x8B);
    assert(memcmp(image + 0x100, sspJump, 5) == 0);
    assert(image[0x300] == 0xE8);
    assert(image[0x34B] == 0xE8 && image[0x600] == 0x8B);
    assert(memcmp(image + 0x900, readPrefix, 8) == 0);
    assert(memcmp(image + 0xA00, closePrefix, 5) == 0);
    VirtualFree(image, 0, MEM_RELEASE);
}

int main() {
    wchar_t folder[512];
    assert(LocalUserFiles::DirectoryFromExe(L"D:\\Games\\Vice City\\gta-vc.exe", folder, 512));
    assert(wcscmp(folder, L"D:\\Games\\Vice City\\userfiles") == 0);
    assert(LocalUserFiles::DirectoryFromExe(L"D:\\gta-vc.exe", folder, 512));
    assert(wcscmp(folder, L"D:\\userfiles") == 0);
    assert(LocalUserFiles::DirectoryFromExe(L"\\\\server\\games\\vc\\gta-vc.exe", folder, 512));
    assert(wcscmp(folder, L"\\\\server\\games\\vc\\userfiles") == 0);
    assert(!LocalUserFiles::DirectoryFromExe(L"gta-vc.exe", folder, 512));
    assert(!LocalUserFiles::DirectoryFromExe(L"D:\\Games\\vc\\gta-vc.exe", folder, 4));
    char ansi[181];
    assert(LocalUserFiles::GameEncoding(L"D:\\Games\\vc\\userfiles", ansi, sizeof(ansi)));
    wchar_t longFolder[300];
    wmemset(longFolder, L'x', 299); longFolder[299] = 0;
    assert(!LocalUserFiles::GameEncoding(longFolder, ansi, sizeof(ansi)));
    assert(LocalUserFiles::PrepareDirectory());
    strcpy_s(runtimePath, LocalUserFiles::path);
    TestHooks(false, false);
    TestHooks(true, false);
    TestHooks(false, true);
    TestHooks(true, true);
    // The real standalone test program creates only its own sibling userfiles
    // directory; it never reads/writes the shared Documents game folder.
    assert(LocalUserFiles::PrepareDirectory());
    assert(strstr(LocalUserFiles::path, "userfiles"));
    puts("Local userfiles path and SilentPatch load-order checks passed");
}

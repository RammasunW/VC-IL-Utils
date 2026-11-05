#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdint>
#include <cstdlib>
#include <intrin.h>

static_assert(sizeof(void*) == 4, "Build as Win32/x86.");
namespace LocalUserFiles {
// Stock save code has 200-byte filename buffers. Leave room for slot names.
static const size_t MAX_GAME_DIRECTORY = 180;
static char path[MAX_GAME_DIRECTORY + 1] = {};
struct Profile { DWORD timestamp, getterRva, setterRva, bufferRva, formatRva, chdirRva, savePrefixRva,
    startupRva, continuationRva, fileInitRva, openRva; };
static const Profile profiles[] = {
    {0x504D6947, 0x202240, 0x21D930, 0x383EF8, 0x2D8B30, 0x08E020, 0x57509C,
        0x201A40, 0x201B2F, 0x08E100, 0x08DF90}, // INTL
    {0x40758BC6, 0x201FE0, 0x21D640, 0x380EF8, 0x2D5E1C, 0x08E410, 0x5720A4,
        0x201830, 0x20191F, 0x08E4F0, 0x08E380}  // JP
};
static BYTE* getter = nullptr;
static BYTE* setter = nullptr;
static char* gameBuffer = nullptr;
static char* savePrefix = nullptr;
static BYTE* changeDir = nullptr;
static BYTE oldChangeDir[5], changeDirJump[5];
static HANDLE logFile = INVALID_HANDLE_VALUE;
static LONG getterLogged, setterLogged, chdirLogged;
static BYTE oldGetter[5], getterJump[5], oldSetter[9], setterJump[9];
static BYTE* trampoline = nullptr;
using SaveDirectory = void (__cdecl*)(const char*);
static SaveDirectory originalSetter = nullptr;
static BYTE *startup, *startupGate, *fileOpen;
static BYTE oldStartup[10], startupPatch[10], oldGate[5], gatePatch[5], oldOpen[8], openPatch[8];
static void (__cdecl* originalFileInit)();
static int (__cdecl* originalFopen)(const char*, const char*);
static bool EnsureStartup();
static BYTE *readEntry, *closeEntry;
static BYTE oldRead[8], readJump[8], oldClose[5], closeJump[5];
static unsigned int (__cdecl* gameFread)(void*, unsigned int, unsigned int, void*);
static int (__cdecl* gameFclose)(void*);
struct SettingsRead { int handle; unsigned int calls, bytes, shorts; };
static SettingsRead settings[8] = {};
static BYTE* gameImage;
static size_t gameImageSize;
struct LoadedValue { char* address; unsigned int size; BYTE bytes[8]; };
static LoadedValue loadedValues[224];
static unsigned int loadedCount;
static bool snapshotReady;

static unsigned int ChangedValues() {
    unsigned int changed = 0;
    for (unsigned int i = 0; i < loadedCount; ++i)
        if (memcmp(loadedValues[i].address, loadedValues[i].bytes, loadedValues[i].size)) ++changed;
    return changed;
}

static void Log(const char* text) {
    if (logFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(logFile, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
        WriteFile(logFile, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(logFile);
    }
}

static bool DirectoryFromExe(const wchar_t* exe, wchar_t* output, size_t capacity) {
    const wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash)
        return false;
    const size_t prefix = static_cast<size_t>(slash - exe + 1);
    const wchar_t leaf[] = L"userfiles";
    if (prefix + _countof(leaf) > capacity)
        return false;
    wmemcpy(output, exe, prefix);
    wcscpy_s(output + prefix, capacity - prefix, leaf);
    return true;
}

static bool GameEncoding(const wchar_t* folder, char* result, size_t capacity) {
    BOOL substituted = FALSE;
    const UINT codePage = GetACP();
    const DWORD flags = codePage == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    const int size = WideCharToMultiByte(codePage, flags, folder, -1, result,
        static_cast<int>(capacity), nullptr, codePage == CP_UTF8 ? nullptr : &substituted);
    return size > 0 && !substituted;
}

static bool PrepareDirectory() {
    wchar_t exe[32768], folder[32768], shortFolder[32768];
    const DWORD length = GetModuleFileNameW(nullptr, exe, _countof(exe));
    if (!length || length >= _countof(exe) || !DirectoryFromExe(exe, folder, _countof(folder)))
        return false;
    wchar_t* last = wcsrchr(exe, L'\\');
    if (last) {
        wcscpy_s(last + 1, _countof(exe) - (last + 1 - exe), L"VC-LocalUserFiles.log");
        logFile = CreateFileW(exe, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    Log("VC Local User Files v6: absolute settings path and preference-value trace");
    if (!CreateDirectoryW(folder, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    const DWORD attributes = GetFileAttributesW(folder);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    if (!GameEncoding(folder, path, sizeof(path))) {
        const DWORD shortLength = GetShortPathNameW(folder, shortFolder, _countof(shortFolder));
        if (!shortLength || shortLength >= _countof(shortFolder) ||
            !GameEncoding(shortFolder, path, sizeof(path)))
            return false;
    }
    // Check access before altering game code; never silently use Documents.
    wchar_t probe[32768];
    if (swprintf_s(probe, L"%s\\.vc-local-userfiles-%lu.tmp", folder, GetCurrentProcessId()) < 0)
        return false;
    HANDLE file = CreateFileW(probe, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(file);
    Log(path);
    return true;
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

static void Jump(BYTE* bytes, BYTE opcode, BYTE* location, const void* target) {
    bytes[0] = opcode;
    *reinterpret_cast<DWORD*>(bytes + 1) = static_cast<DWORD>(
        reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(location + 5));
}

static char* __cdecl GetDirectory() {
    // Some game paths also use the original 256-byte directory buffer.
    if (gameBuffer)
        strcpy_s(gameBuffer, 256, path);
    if (savePrefix)
        sprintf_s(savePrefix, 200, "%s\\GTAVCsf", path);
    if (!InterlockedExchange(&getterLogged, 1)) Log("User-directory getter invoked");
    return path;
}

static int __cdecl ChangeDirectory() {
    const char* directory = GetDirectory();
    const BOOL success = SetCurrentDirectoryA(directory);
    if (!InterlockedExchange(&chdirLogged, 1)) {
        Log(success ? "File-manager user-directory switch succeeded" : "ERROR: directory switch failed");
        Log(directory);
    }
    return success ? 0 : -1;
}

static bool EnsureGetter() {
    if (memcmp(getter, getterJump, 5) == 0)
        return true;
    // Retain the most recent replacement for clean restoration. At startup
    // this may be SSP's hook installed after this ASI was loaded.
    memcpy(oldGetter, getter, 5);
    return Write(getter, getterJump, 5);
}

static bool EnsureStartup() {
    if (memcmp(startup, startupPatch, 10) == 0)
        return true;
    memcpy(oldStartup, startup, 10);
    return Write(startup, startupPatch, 10);
}

static void __cdecl BeforeFolderStartup() {
    originalFileInit();
    Log("Early startup gate invoked before inline Documents-folder block");
    if (!EnsureGetter() || !EnsureStartup())
        Log("ERROR: failed to reapply startup folder redirect");
    GetDirectory();
}

static int __cdecl TraceOpen(const char* filename, const char* mode) {
    const char* leaf = strrchr(filename, '\\');
    leaf = leaf ? leaf + 1 : filename;
    const bool isSettings = _stricmp(leaf, "gta_vc.set") == 0;
    char localSettings[220];
    if (isSettings) {
        sprintf_s(localSettings, "%s\\gta_vc.set", path);
        filename = localSettings;
    }
    const int result = originalFopen(filename, mode);
    if (isSettings) {
        const DWORD savedError = GetLastError();
        char full[1024] = {}, line[1200];
        GetFullPathNameA(filename, sizeof(full), full, nullptr);
        sprintf_s(line, "SETTINGS OPEN mode=%s result=%s path=%s", mode,
            result ? "success" : "failed", full[0] ? full : filename);
        Log(line);
        sprintf_s(line, "SETTINGS OPEN caller=%p", _ReturnAddress());
        Log(line);
        if (result && mode[0] == 'r')
            for (auto& read : settings)
                if (!read.handle) { read = {result, 0, 0, 0}; break; }
        SetLastError(savedError);
    }
    return result;
}

static int __cdecl TraceRead(int handle, char* buffer, int size) {
    const unsigned int count = gameFread(buffer, 1, size, reinterpret_cast<void*>(handle));
    const DWORD savedError = GetLastError();
    for (auto& read : settings) {
        if (!read.handle || read.handle != handle)
            continue;
        ++read.calls;
        read.bytes += count;
        if (count < static_cast<unsigned int>(size)) ++read.shorts;
        if (!snapshotReady && count && gameImage &&
            reinterpret_cast<uintptr_t>(buffer) >= reinterpret_cast<uintptr_t>(gameImage) &&
            reinterpret_cast<uintptr_t>(buffer) + count <= reinterpret_cast<uintptr_t>(gameImage) + gameImageSize) {
            unsigned int index = 0;
            while (index < loadedCount && loadedValues[index].address != buffer) ++index;
            if (index < _countof(loadedValues)) {
                auto& value = loadedValues[index];
                value.address = buffer;
                value.size = count < 8 ? count : 8;
                memcpy(value.bytes, buffer, value.size);
                if (index == loadedCount) ++loadedCount;
            }
        }
        if (read.calls <= 12 || read.calls >= 181 || count < static_cast<unsigned int>(size)) {
            char line[220], hex[40] = {};
            const unsigned int shown = count < 8 ? count : 8;
            for (unsigned int i = 0; i < shown; ++i)
                sprintf_s(hex + i * 3, sizeof(hex) - i * 3, "%02X ", static_cast<unsigned char>(buffer[i]));
            sprintf_s(line, "SETTINGS READ call=%u requested=%d returned=%u first-bytes=%s",
                read.calls, size, count, hex);
            Log(line);
            sprintf_s(line, "SETTINGS READ caller=%p destination=%p", _ReturnAddress(), buffer);
            Log(line);
        }
        break;
    }
    SetLastError(savedError);
    return count;
}

static int __cdecl TraceClose(int handle) {
    const DWORD savedError = GetLastError();
    for (auto& read : settings)
        if (read.handle && read.handle == handle) {
            char line[160];
            sprintf_s(line, "SETTINGS CLOSE read-calls=%u bytes-returned=%u short-reads=%u",
                read.calls, read.bytes, read.shorts);
            Log(line);
            if (!snapshotReady && read.calls >= 180) snapshotReady = true;
            if (snapshotReady) {
                sprintf_s(line, "SETTINGS VALUES tracked=%u changed-since-load=%u",
                    loadedCount, ChangedValues());
                Log(line);
                unsigned int shown = 0;
                for (unsigned int i = 0; i < loadedCount && shown < 12; ++i) {
                    const auto& value = loadedValues[i];
                    if (!memcmp(value.address, value.bytes, value.size)) continue;
                    char before[25] = {}, after[25] = {};
                    for (unsigned int j = 0; j < value.size; ++j) {
                        sprintf_s(before + j * 3, sizeof(before) - j * 3, "%02X ", value.bytes[j]);
                        sprintf_s(after + j * 3, sizeof(after) - j * 3, "%02X ", static_cast<BYTE>(value.address[j]));
                    }
                    sprintf_s(line, "SETTINGS CHANGED destination=%p loaded=%s current=%s", value.address, before, after);
                    Log(line);
                    ++shown;
                }
            }
            read = {};
            break;
        }
    SetLastError(savedError);
    return gameFclose(reinterpret_cast<void*>(handle));
}

static void __cdecl SetDirectory(const char*) {
    if (!InterlockedExchange(&setterLogged, 1)) Log("Startup save-directory setter invoked");
    // Game initialization occurs after the normal ASI load sequence. Reapply
    // the getter here so SSP's later path patch cannot split saves/settings.
    if (!EnsureGetter())
        MessageBoxA(nullptr, "VC Local User Files: could not retain the user-directory hook.",
            "VC Local User Files", MB_OK | MB_ICONERROR);
    originalSetter(GetDirectory());
}

static void Remove() {
    if (readEntry && memcmp(readEntry, readJump, 8) == 0) Write(readEntry, oldRead, 8);
    if (closeEntry && memcmp(closeEntry, closeJump, 5) == 0) Write(closeEntry, oldClose, 5);
    readEntry = closeEntry = nullptr;
    memset(settings, 0, sizeof(settings));
    loadedCount = 0;
    snapshotReady = false;
    gameImage = nullptr;
    gameImageSize = 0;
    if (getter && memcmp(getter, getterJump, 5) == 0)
        Write(getter, oldGetter, 5);
    if (setter && memcmp(setter, setterJump, 9) == 0)
        Write(setter, oldSetter, 9);
    if (changeDir && memcmp(changeDir, changeDirJump, 5) == 0)
        Write(changeDir, oldChangeDir, 5);
    if (startup && memcmp(startup, startupPatch, 10) == 0) Write(startup, oldStartup, 10);
    if (startupGate && memcmp(startupGate, gatePatch, 5) == 0) Write(startupGate, oldGate, 5);
    if (fileOpen && memcmp(fileOpen, openPatch, 8) == 0) Write(fileOpen, oldOpen, 8);
    if (trampoline)
        VirtualFree(trampoline, 0, MEM_RELEASE);
    getter = setter = trampoline = nullptr;
    originalSetter = nullptr;
    gameBuffer = nullptr;
    savePrefix = nullptr;
    changeDir = nullptr;
    startup = startupGate = fileOpen = nullptr;
    if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
    logFile = INVALID_HANDLE_VALUE;
}

static bool InstallReadTraceEntries(BYTE* read, BYTE* close) {
    const BYTE readPrefix[] = {0x8B,0x44,0x24,0x0C,0x8B,0x4C,0x24,0x04};
    const BYTE closePrefix[] = {0x8B,0x44,0x24,0x04,0x50,0xE8};
    if (memcmp(read, readPrefix, 8) != 0 || read[17] != 0xE8 ||
        memcmp(close, closePrefix, 6) != 0)
        return false;
    gameFread = reinterpret_cast<decltype(gameFread)>(read + 22 + *reinterpret_cast<int*>(read + 18));
    gameFclose = reinterpret_cast<decltype(gameFclose)>(close + 10 + *reinterpret_cast<int*>(close + 6));
    readEntry = read; closeEntry = close;
    memcpy(oldRead, read, 8); memcpy(oldClose, close, 5);
    memset(readJump, 0x90, 8); Jump(readJump, 0xE9, read, &TraceRead);
    Jump(closeJump, 0xE9, close, &TraceClose);
    return Write(read, readJump, 8) && Write(close, closeJump, 5);
}

static bool InstallReadTrace(BYTE* image, bool jp) {
    return InstallReadTraceEntries(image + (jp ? 0x08E340 : 0x08DF50),
        image + (jp ? 0x08E290 : 0x08DEA0));
}

static bool Install(BYTE* image, const Profile& profile) {
    BYTE* get = image + profile.getterRva;
    BYTE* set = image + profile.setterRva;
    BYTE* chdir = image + profile.chdirRva;
    BYTE* start = image + profile.startupRva;
    BYTE* open = image + profile.openRva;
    const BYTE getterPrefix[] = {0x56, 0x57, 0x83, 0xEC, 0x10};
    BYTE setterPrefix[] = {0x8B, 0x44, 0x24, 0x04, 0x68, 0, 0, 0, 0};
    *reinterpret_cast<DWORD*>(setterPrefix + 5) = reinterpret_cast<DWORD>(image + profile.formatRva);
    if ((memcmp(get, getterPrefix, 5) != 0 && get[0] != 0xE9) ||
        memcmp(set, setterPrefix, 9) != 0 || chdir[0] != 0xE8 ||
        chdir + 5 + *reinterpret_cast<int*>(chdir + 1) != get ||
        start[-5] != 0xE8 || start + *reinterpret_cast<int*>(start - 4) != image + profile.fileInitRva)
        return false;
    const BYTE inlinePrefix[] = {0xC7,0x84,0x24,0xC0,0,0,0,0,0,0};
    const BYTE openPrefix[] = {0x8B,0x44,0x24,0x04,0x8B,0x4C,0x24,0x08,0x51,0x50,0xE8};
    if ((memcmp(start, inlinePrefix, 10) != 0 && !(start[0] == 0xE8 && start[5] == 0xE9)) ||
        memcmp(open, openPrefix, sizeof(openPrefix)) != 0)
        return false;
    BYTE* code = static_cast<BYTE*>(VirtualAlloc(nullptr, 14,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code)
        return false;
    memcpy(code, set, 9); // complete instructions; no relative operands
    Jump(code + 9, 0xE9, code + 9, set + 9);
    DWORD previous;
    if (!VirtualProtect(code, 14, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(code, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, 14);
    getter = get;
    setter = set;
    gameBuffer = reinterpret_cast<char*>(image + profile.bufferRva);
    savePrefix = reinterpret_cast<char*>(image + profile.savePrefixRva);
    changeDir = chdir;
    memcpy(oldChangeDir, chdir, 5);
    trampoline = code;
    originalSetter = reinterpret_cast<SaveDirectory>(code);
    startup = start;
    startupGate = start - 5;
    originalFileInit = reinterpret_cast<void (__cdecl*)()>(image + profile.fileInitRva);
    fileOpen = open;
    originalFopen = reinterpret_cast<int (__cdecl*)(const char*, const char*)>(
        open + 15 + *reinterpret_cast<int*>(open + 11));
    memcpy(oldGate, startupGate, 5);
    memcpy(oldOpen, open, 8);
    Jump(gatePatch, 0xE8, startupGate, &BeforeFolderStartup);
    Jump(startupPatch, 0xE8, start, &GetDirectory);
    Jump(startupPatch + 5, 0xE9, start + 5, image + profile.continuationRva);
    memset(openPatch, 0x90, 8);
    Jump(openPatch, 0xE9, open, &TraceOpen);
    memcpy(oldSetter, set, 9);
    Jump(getterJump, 0xE9, get, &GetDirectory);
    memset(setterJump, 0x90, 9);
    Jump(setterJump, 0xE9, set, &SetDirectory);
    Jump(changeDirJump, 0xE9, chdir, &ChangeDirectory);
    if (!EnsureGetter() || !EnsureStartup() || !Write(startupGate, gatePatch, 5) ||
        !Write(open, openPatch, 8) || !Write(set, setterJump, 9) || !Write(chdir, changeDirJump, 5)) {
        Remove();
        return false;
    }
    GetDirectory();
    Log("Installed early gate, inline startup redirect, directory hooks and settings-open trace");
    return true;
}

static bool Initialize() {
    BYTE* image = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        return false;
    gameImage = image;
    gameImageSize = nt->OptionalHeader.SizeOfImage;
    for (const auto& profile : profiles)
        if (nt->FileHeader.TimeDateStamp == profile.timestamp) {
            if (!PrepareDirectory() || !Install(image, profile) ||
                !InstallReadTrace(image, profile.timestamp == 0x40758BC6)) {
                Remove();
                return false;
            }
            return true;
        }
    return false;
}
}

#ifndef LOCAL_USERFILES_TEST
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (!LocalUserFiles::Initialize()) {
            MessageBoxW(nullptr,
                L"VC Local User Files could not initialize.\n"
                L"Requires supported INTL 1.0 or JP and a writable game directory.\n"
                L"The userfiles path must fit the game's legacy filename limits; "
                L"non-ANSI paths require a usable short-path alias.\n"
                L"This ASI is disabled; no existing saves were moved or deleted.",
                L"VC Local User Files", MB_OK | MB_ICONERROR);
            return FALSE;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        LocalUserFiles::Remove();
    }
    return TRUE;
}
#endif

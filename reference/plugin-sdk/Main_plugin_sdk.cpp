#include <plugin.h> // Plugin-SDK version 1002 from 2025-12-09 23:18:09
#include <CMessages.h>
#include "CStreaming.h"
#include "CWorld.h"
#include "extensions/ScriptCommands.h"

#include <CModelInfo.h>
#include <CCarCtrl.h>
#include <RenderWare.h>


#include <Windows.h>
#include <CommCtrl.h>
#include <process.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cctype>

#pragma comment(lib, "Comctl32.lib")

using namespace plugin;

// -------------------------------------------------------
// configurable keys
// -------------------------------------------------------

static int g_keySpawnBarrel = VK_F4;
static int g_keyRigBomb = VK_F5;
static int g_keyUnrigBomb = VK_F6;
static int g_keyMarkVeh = VK_F7;
static int g_keyUnmarkVeh = VK_F8;
static int g_keyCarSpawner = VK_F9;

static char g_iniPath[MAX_PATH];

static const char* iniFileName = "ILUtilsKeys.ini";

static int ParseKey(const char* str) {
    if (!str || !str[0])
        return -1;

    // Common named keys
    struct KeyName {
        const char* name;
        int vk;
    };

    static const KeyName keys[] = {
        { "TAB",        VK_TAB },
        { "VK_TAB",     VK_TAB },

        { "SPACE",      VK_SPACE },
        { "VK_SPACE",   VK_SPACE },

        { "ENTER",      VK_RETURN },
        { "RETURN",     VK_RETURN },
        { "VK_RETURN",  VK_RETURN },

        { "SHIFT",      VK_SHIFT },
        { "VK_SHIFT",   VK_SHIFT },
        { "LSHIFT",     VK_LSHIFT },
        { "RSHIFT",     VK_RSHIFT },

        { "CTRL",       VK_CONTROL },
        { "CONTROL",    VK_CONTROL },
        { "VK_CONTROL", VK_CONTROL },
        { "LCTRL",      VK_LCONTROL },
        { "RCTRL",      VK_RCONTROL },

        { "ALT",        VK_MENU },
        { "VK_MENU",    VK_MENU },
        { "LALT",       VK_LMENU },
        { "RALT",       VK_RMENU },

        { "INSERT",     VK_INSERT },
        { "VK_INSERT",  VK_INSERT },

        { "DELETE",     VK_DELETE },
        { "DEL",        VK_DELETE },
        { "VK_DELETE",  VK_DELETE },

        { "HOME",       VK_HOME },
        { "END",        VK_END },

        { "PAGEUP",     VK_PRIOR },
        { "PGUP",       VK_PRIOR },
        { "PAGEDOWN",   VK_NEXT },
        { "PGDN",       VK_NEXT },

        { "UP",         VK_UP },
        { "DOWN",       VK_DOWN },
        { "LEFT",       VK_LEFT },
        { "RIGHT",      VK_RIGHT },

        { "BACKSPACE",  VK_BACK },
        { "ESCAPE",     VK_ESCAPE },
        { "ESC",        VK_ESCAPE },

        { "CAPSLOCK",   VK_CAPITAL },

        { "NUMPAD0",    VK_NUMPAD0 },
        { "NUMPAD1",    VK_NUMPAD1 },
        { "NUMPAD2",    VK_NUMPAD2 },
        { "NUMPAD3",    VK_NUMPAD3 },
        { "NUMPAD4",    VK_NUMPAD4 },
        { "NUMPAD5",    VK_NUMPAD5 },
        { "NUMPAD6",    VK_NUMPAD6 },
        { "NUMPAD7",    VK_NUMPAD7 },
        { "NUMPAD8",    VK_NUMPAD8 },
        { "NUMPAD9",    VK_NUMPAD9 },
    };

    for (const auto& key : keys) {
        if (_stricmp(str, key.name) == 0)
            return key.vk;
    }

    // F1 through F24, accepting both "F4" and "VK_F4"
    const char* fstr = str;

    if (_strnicmp(fstr, "VK_", 3) == 0)
        fstr += 3;

    if ((fstr[0] == 'F' || fstr[0] == 'f') && fstr[1]) {
        char* end = nullptr;
        long n = strtol(fstr + 1, &end, 10);

        if (*end == '\0' && n >= 1 && n <= 24)
            return VK_F1 + (int)n - 1;
    }

    // Single letters/numbers:
    // A-Z, 0-9
    if (str[0] && str[1] == '\0') {
        char c = str[0];

        if (c >= 'a' && c <= 'z')
            c -= ('a' - 'A');

        if ((c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
            return c;
    }

    // Also allow raw numeric VK codes:
    // e.g. 115 or 0x73 = F4
    char* end = nullptr;
    long value = strtol(str, &end, 0);

    if (*end == '\0' && value >= 0 && value <= 255)
        return (int)value;

    return -1;
}

static void GetIniPath() {
    HMODULE module = nullptr;

    // Get the module containing this function, i.e. this ASI/DLL,
    // rather than gta-vc.exe.
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetIniPath),
        &module
    );

    GetModuleFileNameA(module, g_iniPath, MAX_PATH);

    char* slash = strrchr(g_iniPath, '\\');

    if (slash)
        strcpy_s(
            slash + 1,
            MAX_PATH - (slash - g_iniPath) - 1,
            iniFileName
        );
    else
        strcpy_s(g_iniPath, iniFileName);
}

static int ReadKey(
    const char* section,
    const char* name,
    const char* defaultValue,
    int defaultKey
) {
    char buf[128];

    GetPrivateProfileStringA(
        section,
        name,
        defaultValue,
        buf,
        sizeof(buf),
        g_iniPath
    );

    int key = ParseKey(buf);

    return key != -1 ? key : defaultKey;
}

static void LoadConfig() {
    GetIniPath();

    // Create a default INI the first time if it doesn't exist.
    if (GetFileAttributesA(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA(
            "Keys",
            "SpawnBarrel",
            "F4",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "RigCarBomb",
            "F5",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "UnrigCarBomb",
            "F6",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "MarkVehicle",
            "F7",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "UnmarkVehicle",
            "F8",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "CarSpawner",
            "F9",
            g_iniPath
        );
    }

    g_keySpawnBarrel = ReadKey(
        "Keys",
        "SpawnBarrel",
        "F4",
        VK_F4
    );

    g_keyRigBomb = ReadKey(
        "Keys",
        "RigCarBomb",
        "F5",
        VK_F5
    );

    g_keyUnrigBomb = ReadKey(
        "Keys",
        "UnrigCarBomb",
        "F6",
        VK_F6
    );

    g_keyMarkVeh = ReadKey(
        "Keys",
        "MarkVehicle",
        "F7",
        VK_F7
    );

    g_keyUnmarkVeh = ReadKey(
        "Keys",
        "UnmarkVehicle",
        "F8",
        VK_F8
    );

    g_keyCarSpawner = ReadKey(
        "Keys",
        "CarSpawner",
        "F9",
        VK_F9
    );
}

// -------
// car spawner
// -------

static void ShowMessage(const char* text) {
    CMessages::AddMessageJumpQ(
        text,
        2000,
        0
    );
}

// -------------------------------------------------------
// Native Win32 vehicle-spawner window
// -------------------------------------------------------

#define ID_SPAWNER_EDIT   1001
#define ID_SPAWNER_SPAWN  1002
#define ID_SPAWNER_CANCEL 1003

static volatile LONG g_spawnerWindowOpen = 0;
static volatile LONG g_spawnRequestPending = 0;

static char g_spawnRequest[64] = {};

static HWND g_gameWindow = nullptr;
static HWND g_spawnerEdit = nullptr;
static HWND g_spawnerSpawnButton = nullptr;

static WNDPROC g_originalEditProc = nullptr;

static UINT g_spawnerClosedMessage = 0;
static volatile LONG g_spawnerFocusReturnPending = 0;
static volatile LONG g_suppressNextGameFocus = 0;
static volatile LONG g_spawnerShuttingDown = 0;
static volatile LONG g_spawnerThreadFinished = 0;
static volatile LONG g_spawnerWindowError = 0;

// These handles and subclass state belong to the game thread. Keep the
// worker handle until it has actually exited; do not detach a live UI thread.
static HANDLE g_spawnerThread = nullptr;
static HANDLE g_spawnerStopEvent = nullptr;
static bool g_gameSubclassInstalled = false;
static bool g_spawnerFocusHandled = false;
static UINT_PTR g_spawnerGeneration = 0;
static const UINT_PTR SPAWNER_SUBCLASS_ID = 1;

struct SpawnerThreadContext {
    HWND owner;
    HINSTANCE instance;
    HANDLE stopEvent;
    UINT closedMessage;
    UINT_PTR generation;
};
static SpawnerThreadContext g_spawnerContext = {};

static const char* SPAWNER_WINDOW_CLASS =
"ILUtilsVehicleSpawnerWindow";

static void ShutdownVehicleSpawner();
static LRESULT CALLBACK GameWindowProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

static void RemoveGameWindowHook() {
    if (g_gameSubclassInstalled) {
        if (IsWindow(g_gameWindow))
            RemoveWindowSubclass(g_gameWindow, GameWindowProc, SPAWNER_SUBCLASS_ID);
        g_gameSubclassInstalled = false;
    }
}

static void FinishSpawnerFocus() {
    if (g_spawnerFocusHandled ||
        !InterlockedCompareExchange(&g_spawnerThreadFinished, 0, 0))
        return;

    g_spawnerFocusHandled = true;
    if (!InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0) &&
        InterlockedCompareExchange(&g_spawnerFocusReturnPending, 0, 0) &&
        IsWindow(g_gameWindow)) {
        HWND foreground = GetForegroundWindow();
        if (!foreground || foreground == g_gameWindow) {
            if (IsIconic(g_gameWindow))
                ShowWindow(g_gameWindow, SW_RESTORE);
            SetForegroundWindow(g_gameWindow);
            SetFocus(g_gameWindow);
        }
    }

    InterlockedExchange(&g_suppressNextGameFocus, 0);
    InterlockedExchange(&g_spawnerFocusReturnPending, 0);
    // No callback into the ASI remains on an idle game window.
    RemoveGameWindowHook();
}

// Comctl32 maintains the callback chain, including removal during dispatch.
// Install/remove this subclass only on the game window's owning thread.
static LRESULT CALLBACK GameWindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR
) {
    if (g_spawnerClosedMessage && msg == g_spawnerClosedMessage) {
        // Ignore a queued notification from an earlier popup session.
        if (wParam == g_spawnerGeneration)
            FinishSpawnerFocus();
        return 0;
    }

    if (msg == WM_CLOSE)
        ShutdownVehicleSpawner();

    // During teardown the renderer/audio may already have been shut down.
    if (msg == WM_SETFOCUS &&
        InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0))
        return 0;

    if (msg == WM_SETFOCUS &&
        InterlockedExchange(&g_suppressNextGameFocus, 0)) {
        DWORD previousProcess = 0;
        if (wParam)
            GetWindowThreadProcessId(reinterpret_cast<HWND>(wParam), &previousProcess);

        // A known external source is a real focus return, not our popup.
        if (!previousProcess || previousProcess == GetCurrentProcessId())
            return 0;
    }

    if (msg == WM_NCDESTROY) {
        InterlockedExchange(&g_spawnerShuttingDown, 1);
        if (g_spawnerStopEvent)
            SetEvent(g_spawnerStopEvent);
        InterlockedExchange(&g_suppressNextGameFocus, 0);
        InterlockedExchange(&g_spawnerFocusReturnPending, 0);
        RemoveGameWindowHook();
        g_gameWindow = nullptr;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static bool InstallGameWindowHook(HWND hwnd) {
    if (g_gameSubclassInstalled)
        return g_gameWindow == hwnd;

    // Install from the game thread, never from the popup thread.
    if (GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId())
        return false;

    if (!g_spawnerClosedMessage) {
        g_spawnerClosedMessage = RegisterWindowMessageA(
            "ILUtils.VehicleSpawner.Closed"
        );
        if (!g_spawnerClosedMessage)
            return false;
    }

    if (!SetWindowSubclass(hwnd, GameWindowProc, SPAWNER_SUBCLASS_ID, 0))
        return false;

    g_gameSubclassInstalled = true;
    g_gameWindow = hwnd;
    return true;
}

static void CloseSpawnerWindow(HWND hwnd) {
    HWND foreground = GetForegroundWindow();
    const bool returnFocus = !InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0) &&
        IsWindow(g_spawnerContext.owner) &&
        (foreground == hwnd || (foreground && IsChild(hwnd, foreground)));

    if (returnFocus) {
        // Arm BEFORE destruction: Windows can reactivate the owner inside
        // DestroyWindow, before or during WM_DESTROY.
        InterlockedExchange(&g_spawnerFocusReturnPending, 1);
        InterlockedExchange(&g_suppressNextGameFocus, 1);
    }

    DestroyWindow(hwnd);
    // The worker posts one completion after destroying its windows and
    // unregistering its class, including cancel and startup-failure paths.
}

static LRESULT CALLBACK SpawnerEditProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            HWND parent = GetParent(hwnd);

            SendMessageA(
                parent,
                WM_COMMAND,
                MAKEWPARAM(ID_SPAWNER_SPAWN, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_spawnerSpawnButton)
            );

            return 0;
        }

        if (wParam == VK_ESCAPE) {
            PostMessageA(
                GetParent(hwnd),
                WM_CLOSE,
                0,
                0
            );

            return 0;
        }
    }

    return CallWindowProcA(
        g_originalEditProc,
        hwnd,
        msg,
        wParam,
        lParam
    );
}

static LRESULT CALLBACK SpawnerWindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (msg) {
    case WM_CREATE:
    {
        HFONT font = static_cast<HFONT>(
            GetStockObject(DEFAULT_GUI_FONT)
            );

        HWND label = CreateWindowExA(
            0,
            "STATIC",
            "Enter vehicle model name or ID:",
            WS_CHILD | WS_VISIBLE,
            12,
            12,
            260,
            20,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        SendMessageA(
            label,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        g_spawnerEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            ES_AUTOHSCROLL,
            12,
            35,
            260,
            24,
            hwnd,
            reinterpret_cast<HMENU>(ID_SPAWNER_EDIT),
            nullptr,
            nullptr
        );

        if (!g_spawnerEdit)
            return -1;

        // Friendly names may be longer than an internal 20-character IDE key.
        SendMessageA(
            g_spawnerEdit,
            EM_SETLIMITTEXT,
            sizeof(g_spawnRequest) - 1,
            0
        );

        SendMessageA(
            g_spawnerEdit,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        g_spawnerSpawnButton = CreateWindowExA(
            0,
            "BUTTON",
            "Spawn",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            BS_DEFPUSHBUTTON,
            76,
            72,
            90,
            28,
            hwnd,
            reinterpret_cast<HMENU>(ID_SPAWNER_SPAWN),
            nullptr,
            nullptr
        );

        SendMessageA(
            g_spawnerSpawnButton,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        HWND cancelButton = CreateWindowExA(
            0,
            "BUTTON",
            "Cancel",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP,
            178,
            72,
            90,
            28,
            hwnd,
            reinterpret_cast<HMENU>(ID_SPAWNER_CANCEL),
            nullptr,
            nullptr
        );

        SendMessageA(
            cancelButton,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        // Enter = Spawn, Escape = Cancel while focus is in the edit box.
        g_originalEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(
                g_spawnerEdit,
                GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&SpawnerEditProc)
            )
            );

        SetFocus(g_spawnerEdit);
        return 0;
    }

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);

        if (InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0)) {
            CloseSpawnerWindow(hwnd);
            return 0;
        }

        if (id == ID_SPAWNER_SPAWN) {
            char text[64] = {};

            GetWindowTextA(
                g_spawnerEdit,
                text,
                sizeof(text)
            );

            if (!text[0]) {
                MessageBeep(MB_ICONWARNING);
                SetFocus(g_spawnerEdit);
                return 0;
            }

            strcpy_s(
                g_spawnRequest,
                sizeof(g_spawnRequest),
                text
            );

            // Publish the completed string to the GTA game thread.
            InterlockedExchange(
                &g_spawnRequestPending,
                1
            );

            CloseSpawnerWindow(hwnd);
            return 0;
        }

        if (id == ID_SPAWNER_CANCEL) {
            CloseSpawnerWindow(hwnd);
            return 0;
        }

        break;
    }

    case WM_CLOSE:
        CloseSpawnerWindow(hwnd);
        return 0;

    case WM_DESTROY:
        // Child windows can still receive destruction messages here. Keep
        // their subclass procedure valid until the window thread exits.
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(
        hwnd,
        msg,
        wParam,
        lParam
    );
}

static void RunSpawnerWindow(const SpawnerThreadContext& context) {
    if (WaitForSingleObject(context.stopEvent, 0) == WAIT_OBJECT_0)
        return;

    HINSTANCE instance = context.instance;

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SpawnerWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = SPAWNER_WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        InterlockedExchange(&g_spawnerWindowError, 1);
        return;
    }

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        SPAWNER_WINDOW_CLASS,
        "ILUtils Vehicle Spawner",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        300,
        145,
        context.owner,
        nullptr,
        instance,
        nullptr
    );

    if (!hwnd) {
        UnregisterClassA(SPAWNER_WINDOW_CLASS, instance);
        InterlockedExchange(&g_spawnerWindowError, 1);
        return;
    }

    if (WaitForSingleObject(context.stopEvent, 0) != WAIT_OBJECT_0) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(g_spawnerEdit);
    }

    bool done = false;
    while (!done) {
        const DWORD result = MsgWaitForMultipleObjectsEx(
            1, &context.stopEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE
        );
        if (result != WAIT_OBJECT_0 + 1)
            break;

        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                done = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (WaitForSingleObject(context.stopEvent, 0) == WAIT_OBJECT_0) {
                done = true;
                break;
            }
        }
    }

    if (IsWindow(hwnd))
        CloseSpawnerWindow(hwnd);

    // A class registered by a DLL is not automatically unregistered when
    // that DLL unloads. Never leave its window procedure registered at exit.
    UnregisterClassA(SPAWNER_WINDOW_CLASS, instance);
}

static unsigned __stdcall SpawnerWindowThread(void* parameter) {
    const SpawnerThreadContext context = *static_cast<SpawnerThreadContext*>(parameter);
    RunSpawnerWindow(context);
    g_spawnerEdit = nullptr;
    g_spawnerSpawnButton = nullptr;
    g_originalEditProc = nullptr;
    InterlockedExchange(&g_spawnerThreadFinished, 1);
    PostMessageA(context.owner, context.closedMessage, context.generation, 0);
    return 0;
}

static void ReapSpawnerThread() {
    if (!g_spawnerThread || WaitForSingleObject(g_spawnerThread, 0) != WAIT_OBJECT_0)
        return;

    // Also handles a failed PostMessage without leaving suppression armed.
    FinishSpawnerFocus();
    CloseHandle(g_spawnerThread);
    CloseHandle(g_spawnerStopEvent);
    g_spawnerThread = nullptr;
    g_spawnerStopEvent = nullptr;
    InterlockedExchange(&g_spawnerWindowOpen, 0);
}

static void ShutdownVehicleSpawner() {
    // Called by the game thread before RenderWare teardown, never in DllMain.
    static bool inShutdown = false;
    if (inShutdown)
        return;
    inShutdown = true;
    InterlockedExchange(&g_spawnerShuttingDown, 1);
    InterlockedExchange(&g_spawnRequestPending, 0);
    if (g_spawnerStopEvent)
        SetEvent(g_spawnerStopEvent);

    bool hadQuitMessage = false;
    int quitCode = 0;
    while (g_spawnerThread && WaitForSingleObject(g_spawnerThread, 0) == WAIT_TIMEOUT) {
        // Owned windows can send messages to the game thread during their
        // destruction. Pump them while joining to avoid a cross-thread deadlock.
        const DWORD result = MsgWaitForMultipleObjectsEx(
            1, &g_spawnerThread, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE
        );
        if (result == WAIT_FAILED)
            break;
        if (result == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    hadQuitMessage = true;
                    quitCode = static_cast<int>(msg.wParam);
                }
                else {
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }
            }
        }
    }

    ReapSpawnerThread();
    RemoveGameWindowHook();
    InterlockedExchange(&g_spawnRequestPending, 0);
    InterlockedExchange(&g_suppressNextGameFocus, 0);
    InterlockedExchange(&g_spawnerFocusReturnPending, 0);
    if (hadQuitMessage)
        PostQuitMessage(quitCode);
    inShutdown = false;
}

static void OpenVehicleSpawnerWindow() {
    ReapSpawnerThread();
    if (InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0) || g_spawnerThread)
        return;

    if (InterlockedCompareExchange(
        &g_spawnerWindowOpen,
        1,
        0
    ) != 0) {
        return;
    }

    // Do not open another prompt until the previous request
    // has been consumed by the game thread.
    if (InterlockedCompareExchange(
        &g_spawnRequestPending,
        0,
        0
    ) != 0 || InterlockedCompareExchange(&g_spawnerFocusReturnPending, 0, 0) != 0) {
        InterlockedExchange(
            &g_spawnerWindowOpen,
            0
        );
        return;
    }

    if (!RsGlobal.ps || !RsGlobal.ps->window) {
        InterlockedExchange(
            &g_spawnerWindowOpen,
            0
        );

        ShowMessage(
            "Unable to open vehicle spawner"
        );
        return;
    }

    if (!InstallGameWindowHook(RsGlobal.ps->window)) {
        InterlockedExchange(&g_spawnerWindowOpen, 0);
        ShowMessage("Unable to install spawner focus hook");
        return;
    }

    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&SpawnerWindowThread), &module);
    g_spawnerStopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (module && g_spawnerStopEvent) {
        ++g_spawnerGeneration;
        g_spawnerFocusHandled = false;
        InterlockedExchange(&g_spawnerThreadFinished, 0);
        InterlockedExchange(&g_spawnerWindowError, 0);
        g_spawnerContext = { g_gameWindow, module, g_spawnerStopEvent,
            g_spawnerClosedMessage, g_spawnerGeneration };
        g_spawnerThread = reinterpret_cast<HANDLE>(_beginthreadex(
            nullptr, 0, SpawnerWindowThread, &g_spawnerContext, 0, nullptr
        ));
    }

    if (!g_spawnerThread) {
        if (g_spawnerStopEvent)
            CloseHandle(g_spawnerStopEvent);
        g_spawnerStopEvent = nullptr;
        RemoveGameWindowHook();
        InterlockedExchange(
            &g_spawnerWindowOpen,
            0
        );

        ShowMessage(
            "Unable to open vehicle spawner"
        );
        return;
    }

}

// Retail Vice City's model-info table has 6500 slots. Keep the bounds check
// local: the 2025 Plugin-SDK lacks the newer ms_modelInfoCount helper.
static const int VEHICLE_SPAWNER_MODEL_LIMIT = 6500;

static bool IsVehicleModelInRange(int modelId) {
    if (modelId < 0 || modelId >= VEHICLE_SPAWNER_MODEL_LIMIT)
        return false;

    CBaseModelInfo* info = CModelInfo::GetModelInfo(modelId);
    return info && info->m_nType == MODEL_INFO_VEHICLE;
}

// Friendly names are independent of IDE filenames (firetruck vs firetruk).
// IDs 130 through 236, in model order. Numbered racers stay distinguishable.
static const char* const VEHICLE_DISPLAY_NAMES[] = {
    "Landstalker", "Idaho", "Stinger", "Linerunner", "Perennial", "Sentinel",
    "Rio", "Firetruck", "Trashmaster", "Stretch", "Manana", "Infernus",
    "Voodoo", "Pony", "Mule", "Cheetah", "Ambulance", "FBI Washington",
    "Moonbeam", "Esperanto", "Taxi", "Washington", "Bobcat", "Mr. Whoopee",
    "BF Injection", "Hunter", "Police", "Enforcer", "Securicar", "Banshee",
    "Predator", "Bus", "Rhino", "Barracks OL", "Cuban Hermes", "Chopper",
    "Angel", "Coach", "Cabbie", "Stallion", "Rumpo", "RC Bandit",
    "Romero's Hearse", "Packer", "Sentinel XS", "Admiral", "Squalo", "Sea Sparrow",
    "Pizza Boy", "Gang Burrito", "Airtrain", "Dead Dodo", "Speeder", "Reefer",
    "Tropic", "Flatbed", "Yankee", "Caddy", "Zebra Cab", "Top Fun",
    "Skimmer", "PCJ-600", "Faggio", "Freeway", "RC Baron", "RC Raider",
    "Glendale", "Oceanic", "Sanchez", "Sparrow", "Patriot", "Love Fist",
    "Coast Guard", "Dinghy", "Hermes", "Sabre", "Sabre Turbo", "Phoenix",
    "Walton", "Regina", "Comet", "Deluxo", "Burrito", "Spand Express",
    "Marquis", "Baggage Handler", "Kaufman Cab", "Maverick", "VCN Maverick", "Rancher",
    "FBI Rancher", "Virgo", "Greenwood", "Cuban Jetmax", "Hotring Racer 1", "Sandking",
    "Blista Compact", "Police Maverick", "Boxville", "Benson", "Mesa Grande", "RC Goblin",
    "Hotring Racer 2", "Hotring Racer 3", "Bloodring Banger 1", "Bloodring Banger 2",
    "Vice Squad Cheetah"
};
static_assert(sizeof(VEHICLE_DISPLAY_NAMES) / sizeof(VEHICLE_DISPLAY_NAMES[0]) == 107,
    "Vehicle name table must cover models 130 through 236");

struct VehicleAlias { int modelId; const char* name; };
static const VehicleAlias VEHICLE_ALIASES[] = {
    {137, "fire engine"}, {153, "mr whoopee"}, {163, "barracks"},
    {172, "romero"}, {172, "romero hearse"}, {188, "zebra taxi"},
    {191, "pcj"}, {201, "love fist limo"}, {215, "baggage"},
    {216, "kaufman taxi"}, {223, "jetmax"},
    {224, "hotring"}, {224, "hotring1"}, {232, "hotring2"}, {233, "hotring3"},
    {234, "bloodring1"}, {235, "bloodring2"},
    {236, "vice cheetah"}, {236, "undercover cheetah"}, {236, "police cheetah"}
};
enum class VehicleMatch { Found, Unknown, Ambiguous };

static void NormalizeVehicleName(const char* source, size_t limit, char(&result)[64]) {
    size_t out = 0;
    for (size_t i = 0; i < limit && source[i] && out < sizeof(result) - 1; ++i) {
        unsigned char ch = static_cast<unsigned char>(source[i]);
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            result[out++] = static_cast<char>(ch);
    }
    result[out] = '\0';
}

static int VehicleNameDistance(const char* a, const char* b) {
    // Edit distance with adjacent transpositions: "infrenus" -> "infernus".
    // Inputs have already been bounded and normalized to at most 63 bytes.
    const size_t an = strlen(a), bn = strlen(b);
    int rows[3][64] = {};
    for (size_t j = 0; j <= bn; ++j)
        rows[0][j] = static_cast<int>(j);
    for (size_t i = 1; i <= an; ++i) {
        int* row = rows[i % 3];
        const int* previous = rows[(i - 1) % 3];
        row[0] = static_cast<int>(i);
        for (size_t j = 1; j <= bn; ++j) {
            int cost = previous[j - 1] + (a[i - 1] != b[j - 1]);
            if (previous[j] + 1 < cost) cost = previous[j] + 1;
            if (row[j - 1] + 1 < cost) cost = row[j - 1] + 1;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                rows[(i - 2) % 3][j - 2] + 1 < cost)
                cost = rows[(i - 2) % 3][j - 2] + 1;
            row[j] = cost;
        }
    }
    return rows[an % 3][bn];
}

struct VehicleNameMatches {
    int modelId = -1;
    int score = INT_MAX;
    bool ambiguous = false;

    void Consider(const char* query, const char* name, size_t nameLimit, int id) {
        char key[64];
        NormalizeVehicleName(name, nameLimit, key);
        if (!key[0]) return;
        const size_t length = strlen(query), candidateLength = strlen(key);
        int candidateScore = INT_MAX;
        if (strcmp(query, key) == 0)
            candidateScore = 0;
        else if (length >= 2 && strncmp(key, query, length) == 0)
            candidateScore = 1; // All matching prefixes have equal priority.
        else if (length >= 4 && candidateLength >= 4) {
            const int maxEdits = length >= 8 ? 2 : 1;
            if (abs(static_cast<int>(length) - static_cast<int>(candidateLength)) <= maxEdits) {
                const int distance = VehicleNameDistance(query, key);
                if (distance <= maxEdits)
                    candidateScore = 10 + distance;
            }
        }
        if (candidateScore == INT_MAX) return;
        if (candidateScore < score) {
            modelId = id;
            score = candidateScore;
            ambiguous = false;
        }
        else if (candidateScore == score && modelId != id)
            ambiguous = true;
    }
};

static VehicleMatch ResolveVehicleModel(
    const char* input,
    int& modelId
) {
    modelId = -1;
    if (!input)
        return VehicleMatch::Unknown;

    // Trim only the outside whitespace before recognizing numeric IDs.
    while (*input && std::isspace(static_cast<unsigned char>(*input))) ++input;
    char text[64];
    size_t length = 0;
    while (input[length] && length < sizeof(text) - 1) {
        text[length] = input[length];
        ++length;
    }
    if (input[length]) return VehicleMatch::Unknown; // Never silently truncate.
    while (length && std::isspace(static_cast<unsigned char>(text[length - 1]))) --length;
    text[length] = '\0';
    if (!length) return VehicleMatch::Unknown;

    // Numeric model ID first.
    char* end = nullptr;

    errno = 0;
    long value = strtol(
        text,
        &end,
        0
    );

    if (end != text && *end == '\0') {
        // Check before casting/indexing the SDK's model-info table.
        if (errno == ERANGE || value < 0 || value > INT_MAX ||
            value >= VEHICLE_SPAWNER_MODEL_LIMIT)
            return VehicleMatch::Unknown;

        if (!IsVehicleModelInRange(static_cast<int>(value)))
            return VehicleMatch::Unknown;
        modelId = static_cast<int>(value);
        return VehicleMatch::Found;
    }

    // Malformed/overflowing numeric text must not be fuzzy-matched to a name.
    if ((text[0] >= '0' && text[0] <= '9') || text[0] == '+' || text[0] == '-')
        return VehicleMatch::Unknown;

    // Otherwise treat the input as an IDE model name.
    // CModelInfo::GetModelInfo performs a case-insensitive lookup.
    if (length <= 20) {
        int exactId = -1;
        CBaseModelInfo* info = CModelInfo::GetModelInfo(text, &exactId);
        if (info && IsVehicleModelInRange(exactId)) {
            modelId = exactId;
            return VehicleMatch::Found;
        }
    }

    char query[64];
    NormalizeVehicleName(text, sizeof(text), query);
    if (!query[0]) return VehicleMatch::Unknown;
    VehicleNameMatches matches;

    // Include live IDE names so installed custom models keep working too.
    for (int id = 0; id < VEHICLE_SPAWNER_MODEL_LIMIT; ++id) {
        CBaseModelInfo* info = CModelInfo::GetModelInfo(id);
        if (info && info->m_nType == MODEL_INFO_VEHICLE)
            matches.Consider(query, info->m_szName, sizeof(info->m_szName), id);
    }
    for (size_t i = 0; i < sizeof(VEHICLE_DISPLAY_NAMES) / sizeof(VEHICLE_DISPLAY_NAMES[0]); ++i) {
        const int id = 130 + static_cast<int>(i);
        if (IsVehicleModelInRange(id))
            matches.Consider(query, VEHICLE_DISPLAY_NAMES[i], 64, id);
    }
    for (const auto& alias : VEHICLE_ALIASES) {
        if (IsVehicleModelInRange(alias.modelId))
            matches.Consider(query, alias.name, 64, alias.modelId);
    }
    if (matches.modelId < 0) return VehicleMatch::Unknown;
    if (matches.ambiguous) return VehicleMatch::Ambiguous;
    modelId = matches.modelId;
    return VehicleMatch::Found;
}

static bool IsForbiddenSpawnModel(int modelId) {
    // This spawner always warps Tommy into the driver seat. These models
    // are scenery/special aircraft or remote-control vehicles, not suitable
    // for that path. Resolve first so names and numeric IDs share this check.
    switch (modelId) {
    case 165:  // chopper: special police helicopter (not polmav)
    case 171:  // rcbandit
    case 180:  // airtrain
    case 181:  // deaddodo
    case 194:  // rcbaron
    case 195:  // rcraider
    case 231:  // rcgoblin
        return true;
    default:
        break;
    }

    // CREATE_CAR constructs a boat, bike or automobile. Other IDE types
    // use incompatible model hierarchies (including custom scenery planes).
    // Vice City's drivable helicopters and Skimmer use the automobile type.
    // SpawnVehicle checked the bounds, pointer and model-info type first.
    const unsigned int type = static_cast<CVehicleModelInfo*>(
        CModelInfo::GetModelInfo(modelId)
        )->m_nVehicleType;
    return type != VEHICLE_AUTOMOBILE && type != VEHICLE_BOAT &&
        type != VEHICLE_BIKE;
}

static void ChangeVehicleCreationType(CVehicle* vehicle, unsigned char type) {
    if (vehicle->m_nCreatedBy == type)
        return;

    // Keep the game's random/mission/parked/permanent vehicle totals balanced.
    CCarCtrl::UpdateCarCount(vehicle, true);
    vehicle->m_nCreatedBy = type;
    CCarCtrl::UpdateCarCount(vehicle, false);
}

static void ReleaseVehicleToTraffic(CVehicle* vehicle) {
    ChangeVehicleCreationType(vehicle, RANDOM_VEHICLE);
    // CREATE_CAR sets this script-retention flag independently of CreatedBy.
    // It is not the door lock. Clear it even for previously unmarked cars.
    vehicle->bIsLocked = false;
    CCarCtrl::RemoveFromInterestingVehicleList(vehicle);
}

static void KeepVehicleAsMission(CVehicle* vehicle) {
    ChangeVehicleCreationType(vehicle, MISSION_VEHICLE);
    vehicle->bIsLocked = true;
}

static void SpawnVehicle(int modelId) {
    // Validate at the creation boundary, before streaming or moving Tommy.
    if (!IsVehicleModelInRange(modelId)) {
        ShowMessage("Invalid vehicle model");
        return;
    }

    if (IsForbiddenSpawnModel(modelId)) {
        ShowMessage("Forbidden vehicle: cannot spawn and enter this model");
        return;
    }

    CPed* player = FindPlayerPed();

    if (!player)
        return;

    CEntity* playerEntity = FindPlayerEntity();

    if (!playerEntity)
        return;

    // Match the current player entity's heading. On foot this is
    // Tommy's heading; while driving this is the current vehicle's heading.
    const float playerHeading = playerEntity->GetHeading();

    CVector spawnPos = playerEntity->TransformFromObjectSpace(
        CVector(0.0f, 4.0f, 0.5f)
    );

    CStreaming::RequestModel(
        modelId,
        0
    );

    CStreaming::LoadAllRequestedModels(
        false
    );

    if (!CStreaming::HasModelLoaded(modelId)) {
        CStreaming::SetModelIsDeletable(modelId);

        ShowMessage(
            "Failed to load vehicle model"
        );
        return;
    }

    // If Tommy is already in a vehicle, move him out before
    // warping him into the newly-created vehicle.
    if (FindPlayerVehicle()) {
        CVector pos = FindPlayerCoors();

        Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(
            player,
            pos.x,
            pos.y,
            pos.z
        );
    }

    CVehicle* vehicle = nullptr;

    Command<Commands::CREATE_CAR>(
        modelId,
        spawnPos.x,
        spawnPos.y,
        spawnPos.z,
        &vehicle
    );

    if (!vehicle) {
        CStreaming::SetModelIsDeletable(
            modelId
        );

        ShowMessage(
            "Failed to create vehicle"
        );
        return;
    }

    vehicle->SetHeading(playerHeading);
    vehicle->UpdateRwFrame();

    Command<Commands::WARP_PLAYER_INTO_CAR>(
        0,
        vehicle
    );

    // Let the normal distance/visibility cleanup rules handle this vehicle
    // after Tommy leaves it. F7 can explicitly keep it as a mission vehicle.
    ReleaseVehicleToTraffic(vehicle);

    CStreaming::SetModelIsDeletable(
        modelId
    );

    char msg[128];

    sprintf_s(
        msg,
        "Spawned vehicle model %d",
        modelId
    );

    ShowMessage(msg);
}

static void ProcessVehicleSpawnRequest() {
    // Wait for the popup's destruction and focus handoff to finish first.
    if (InterlockedCompareExchange(&g_spawnerWindowOpen, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_spawnerFocusReturnPending, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_spawnRequestPending, 0, 0) == 0) {
        return;
    }

    char input[64] = {};

    strcpy_s(
        input,
        sizeof(input),
        g_spawnRequest
    );
    InterlockedExchange(&g_spawnRequestPending, 0);

    int modelId = -1;

    const VehicleMatch match = ResolveVehicleModel(input, modelId);
    if (match == VehicleMatch::Ambiguous) {
        ShowMessage("Ambiguous vehicle name: use a longer name or model ID");
        return;
    }
    if (match != VehicleMatch::Found) {
        char msg[128];

        sprintf_s(
            msg,
            "Unknown vehicle: %s",
            input
        );

        ShowMessage(msg);
        return;
    }

    SpawnVehicle(modelId);
}

static void ProcessVehicleSpawner() {
    ReapSpawnerThread();
    if (InterlockedExchange(&g_spawnerWindowError, 0))
        ShowMessage("Unable to open vehicle spawner");
    ProcessVehicleSpawnRequest();

    static bool spawnerKeyWasDown = false;

    const bool down =
        (GetAsyncKeyState(g_keyCarSpawner) & 0x8000) != 0;

    if (down && !spawnerKeyWasDown && FindPlayerPed() && !FindPlayerVehicle() &&
        RsGlobal.ps && GetForegroundWindow() == RsGlobal.ps->window) {
        OpenVehicleSpawnerWindow();
    }

    spawnerKeyWasDown = down;
}

// -------------------------------------------------------
// car bomb
// -------------------------------------------------------

static void Rig(CVehicle* veh) {
    veh->field_210 = (int)FindPlayerPed();
    // field_210 is CPed* bomb_rigger

    *(char*)((int)veh + 0x1FE) = 11;
    // byte 0x1FE & 7 is carbomb type
    // 11 & 7 = 3
}

static void Unrig(CVehicle* veh) {
    veh->field_210 = 0;

    *(char*)((int)veh + 0x1FE) = 8;
    // 8 & 7 = 0
}

// -------------------------------------------------------

class ILUtils {
public:
    ILUtils() {
        LoadConfig();

        Events::shutdownRwEvent += [] { ShutdownVehicleSpawner(); };
        Events::initRwEvent += [] { InterlockedExchange(&g_spawnerShuttingDown, 0); };

        static int keyPressTime = 0;

        Events::gameProcessEvent += [] {

            if (InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0))
                return;

            ProcessVehicleSpawner();

            // ------------------------------------------------
            // barrel spawner
            // ------------------------------------------------

            if (FindPlayerPed() &&
                KeyPressed(g_keySpawnBarrel) &&
                CTimer::m_snTimeInMilliseconds - keyPressTime > 500) {

                keyPressTime = CTimer::m_snTimeInMilliseconds;

                CStreaming::RequestModel(350, 0);
                CStreaming::LoadAllRequestedModels(false);

                CObject* barrel = new CObject(350, false);

                if (barrel) {
                    CVector playerPos = FindPlayerCoors();

                    barrel->SetPosition(playerPos);
                    CWorld::Add(barrel);

                    // Mark as mission object so it will be saved.
                    barrel->m_nObjectType = OBJECT_MISSION;

                    Command<Commands::MAKE_OBJECT_TARGETTABLE>(
                        barrel,
                        1
                    );

                    Command<Commands::ADD_ONE_OFF_SOUND>(
                        playerPos.x,
                        playerPos.y,
                        playerPos.z,
                        16
                    );

                    ShowMessage("Explosive barrel spawned");
                }
                else ShowMessage("Failed to spawn barrel");
            }

            // ------------------------------------------------
            // carbomb rigger
            // ------------------------------------------------

            if (FindPlayerPed() &&
                KeyPressed(g_keyRigBomb) &&
                CTimer::m_snTimeInMilliseconds - keyPressTime > 500) {

                keyPressTime = CTimer::m_snTimeInMilliseconds;

                if (CVehicle* pv = FindPlayerVehicle()) {
                    Rig(pv);

                    // Give detonator.
                    CStreaming::RequestModel(291, 0);
                    CStreaming::LoadAllRequestedModels(false);

                    FindPlayerPed()->GiveWeapon(
                        WEAPONTYPE_DETONATOR,
                        1,
                        false
                    );

                    // Play sound.
                    CVector playerPos = FindPlayerCoors();

                    Command<Commands::ADD_ONE_OFF_SOUND>(
                        playerPos.x,
                        playerPos.y,
                        playerPos.z,
                        16
                    );

                    ShowMessage("Car bomb armed");
                }
                else ShowMessage("You must be inside a vehicle to rig the car bomb");
            }

            // ------------------------------------------------
            // unrig carbomb
            // ------------------------------------------------

            if (FindPlayerPed() &&
                KeyPressed(g_keyUnrigBomb) &&
                CTimer::m_snTimeInMilliseconds - keyPressTime > 500) {

                keyPressTime = CTimer::m_snTimeInMilliseconds;

                if (CVehicle* pv = FindPlayerVehicle()) {
                    Unrig(pv);

                    // Play sound.
                    CVector playerPos = FindPlayerCoors();

                    Command<Commands::ADD_ONE_OFF_SOUND>(
                        playerPos.x,
                        playerPos.y,
                        playerPos.z,
                        10
                    );

                    ShowMessage("Car bomb removed");
                }
                else ShowMessage("You must be inside a vehicle to remove the car bomb");
            }

            // mark vehicle
            if (FindPlayerPed() &&
                KeyPressed(g_keyMarkVeh)) {

                if (CVehicle* pv = FindPlayerVehicle()) {
                    KeepVehicleAsMission(pv);

                    // Play sound.
                    CVector playerPos = FindPlayerCoors();
                    Command<Commands::ADD_ONE_OFF_SOUND>(
                        playerPos.x,
                        playerPos.y,
                        playerPos.z,
                        57
                    );

                    ShowMessage("Vehicle marked as mission vehicle");
                }
                else ShowMessage("You must be inside a vehicle to mark it");
            }

            // unmark vehicle
            if (FindPlayerPed() &&
                KeyPressed(g_keyUnmarkVeh)) {

                if (CVehicle* pv = FindPlayerVehicle()) {
                    ReleaseVehicleToTraffic(pv);

                    // Play sound.
                    CVector playerPos = FindPlayerCoors();
                    Command<Commands::ADD_ONE_OFF_SOUND>(
                        playerPos.x,
                        playerPos.y,
                        playerPos.z,
                        61
                    );

                    ShowMessage("Vehicle marked as random vehicle");
                }
                else ShowMessage("You must be inside a vehicle to unmark it");
            }
            };
    }
} ILUtils;
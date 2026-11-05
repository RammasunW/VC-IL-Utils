// Disable IME composition for the game's HWND, not for Windows or tool edits.
#include <imm.h>
#pragma comment(lib, "Imm32.lib")
namespace GameIme {
static HWND game = nullptr;
static HIMC originalContext = nullptr;
static const UINT_PTR SUBCLASS_ID = 0x494D45;

static void Disable(HWND window) {
    HIMC context = ImmGetContext(window);
    if (context) {
        ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(window, context);
    }
    ImmAssociateContext(window, nullptr);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR) {
    if (message == WM_IME_STARTCOMPOSITION || message == WM_IME_COMPOSITION ||
        message == WM_IME_ENDCOMPOSITION || message == WM_IME_CHAR)
        return 0;
    if (message == WM_IME_SETCONTEXT) {
        Disable(hwnd);
        return DefSubclassProc(hwnd, message, wp, 0);
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, WindowProc, SUBCLASS_ID);
        game = nullptr;
        originalContext = nullptr;
    }
    const LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
    if (message == WM_INPUTLANGCHANGE || message == WM_SETFOCUS)
        Disable(hwnd);
    return result;
}

static BOOL CALLBACK FindGame(HWND window, LPARAM pointer) {
    char name[80] = {};
    GetClassNameA(window, name, sizeof(name));
    if (_stricmp(name, "Grand theft auto 3") == 0) {
        *reinterpret_cast<HWND*>(pointer) = window;
        return FALSE;
    }
    return TRUE;
}

static void Ensure() {
    if (game && IsWindow(game))
        return;
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindGame, reinterpret_cast<LPARAM>(&found));
    if (!found || !SetWindowSubclass(found, WindowProc, SUBCLASS_ID, 0))
        return;
    game = found;
    HIMC context = ImmGetContext(found);
    if (context) {
        originalContext = context;
        ImmReleaseContext(found, context);
    }
    Disable(found);
}

static void Remove() {
    if (game && IsWindow(game)) {
        RemoveWindowSubclass(game, WindowProc, SUBCLASS_ID);
        ImmAssociateContext(game, originalContext);
    }
    game = nullptr;
    originalContext = nullptr;
}
}

static void EnsureGameImePolicy() { GameIme::Ensure(); }

#ifdef _WIN32

#define NOMINMAX
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#include "MacMenu.h"

namespace {

SDL_Window* g_window = nullptr;
WNDPROC g_originalWndProc = nullptr;

LRESULT CALLBACK WinMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND && HIWORD(wParam) == 0) {
        INT id = LOWORD(wParam);
        SDL_Event ev = {};
        ev.type = SDL_USEREVENT;
        ev.user.code = id;
        ev.user.data1 = nullptr;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
        return 0;
    }
    return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
}

} // namespace

namespace WinMenu {

void install(SDL_Window* window) {
    g_window = window;
    SDL_SysWMinfo info = {};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return;
    HWND hwnd = info.info.win.window;
    if (!hwnd) return;

    HMENU bar = CreateMenu();
    if (!bar) return;

    // kPen menu: About kPen, Check for Updates, separator, Exit
    HMENU app = CreatePopupMenu();
    if (app) {
        AppendMenuA(app, MF_STRING, (UINT_PTR)MacMenu::ABOUT, "About kPen");
        AppendMenuA(app, MF_STRING, (UINT_PTR)MacMenu::CHECK_FOR_UPDATES, "Check for Updates...");
        AppendMenuA(app, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(app, MF_STRING, (UINT_PTR)MacMenu::QUIT, "Exit");
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)app, "kPen");
    }

    // File menu: New, Open, separator, Save, Save As, separator, Close
    HMENU file = CreatePopupMenu();
    if (file) {
        AppendMenuA(file, MF_STRING, (UINT_PTR)MacMenu::FILE_NEW, "New");
        AppendMenuA(file, MF_STRING, (UINT_PTR)MacMenu::FILE_OPEN, "Open...");
        AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(file, MF_STRING, (UINT_PTR)MacMenu::FILE_SAVE, "Save");
        AppendMenuA(file, MF_STRING, (UINT_PTR)MacMenu::FILE_SAVE_AS, "Save As...");
        AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(file, MF_STRING, (UINT_PTR)MacMenu::FILE_CLOSE, "Close");
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "File");
    }

    // Edit menu: Undo, Redo, separator, Cut, Copy, Paste, separator, Select All
    HMENU edit = CreatePopupMenu();
    if (edit) {
        AppendMenuA(edit, MF_STRING, (UINT_PTR)MacMenu::EDIT_UNDO, "Undo");
        AppendMenuA(edit, MF_STRING, (UINT_PTR)MacMenu::EDIT_REDO, "Redo");
        AppendMenuA(edit, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(edit, MF_STRING, (UINT_PTR)MacMenu::EDIT_CUT, "Cut");
        AppendMenuA(edit, MF_STRING, (UINT_PTR)MacMenu::EDIT_COPY, "Copy");
        AppendMenuA(edit, MF_STRING, (UINT_PTR)MacMenu::EDIT_PASTE, "Paste");
        AppendMenuA(edit, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(edit, MF_STRING, (UINT_PTR)MacMenu::EDIT_SELECT_ALL, "Select All");
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)edit, "Edit");
    }

    SetMenu(hwnd, bar);
    g_originalWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)WinMenuWndProc);
}

} // namespace WinMenu

#endif // _WIN32

#pragma once

// MacMenu — native macOS menu bar for kPen.
//
// Call MacMenu::install() once after SDL_CreateWindow.
// Each menu item posts an SDL_USEREVENT with .code set to one of the constants
// below.  Handle them in kPen::run() alongside normal SDL_KEYDOWN events.

namespace MacMenu {

#ifdef __APPLE__
    void install();
    void useArrowCursor();  // immediately set macOS arrow cursor, bypassing SDL
#else
    inline void install() {}          // no-op on non-macOS
    inline void useArrowCursor() {}   // no-op on non-macOS
#endif

    // Event codes — placed in SDL_UserEvent.code
    enum Code {
        FILE_NEW      = 1000,
        FILE_OPEN     = 1001,
        FILE_SAVE     = 1002,
        FILE_SAVE_AS  = 1003,
        FILE_CLOSE    = 1004,
        EDIT_UNDO     = 1010,
        EDIT_REDO     = 1011,
        EDIT_CUT      = 1012,
        EDIT_COPY     = 1013,
        EDIT_PASTE    = 1014,
        EDIT_SELECT_ALL = 1015,
        QUIT          = 1099,
    };
}

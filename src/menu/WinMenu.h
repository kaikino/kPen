#pragma once

#include <SDL.h>

// WinMenu — native Windows menu bar for kPen (same options as MacMenu).
// Call WinMenu::install(window) once after SDL_CreateWindow on Windows.
// Menu items post SDL_USEREVENT with user.code = MacMenu::Code.

#ifdef _WIN32
namespace WinMenu {
    void install(SDL_Window* window);
}
#else
namespace WinMenu {
    inline void install(SDL_Window*) {}
}
#endif

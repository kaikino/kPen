#pragma once

// Windows update check: fetch latest release from GitHub, compare version,
// offer to download and run the NSIS installer. Runs async; results are
// delivered via SDL_USEREVENT so the main thread can show UI.
//
// Call startCheckAsync() when the user chooses "Check for Updates".
// When the check finishes, an SDL_USEREVENT is pushed:
//   - code WIN_UPDATE_RESULT (1102), data1 = WinUpdateResult* (caller must free)
// When download finishes and installer is ready:
//   - code WIN_INSTALL_LAUNCH (1103), data1 = char* temp installer path (caller must free, then run and exit)

#ifdef _WIN32

#include <SDL.h>

namespace WinUpdate {

enum : int {
    WIN_UPDATE_RESULT = 1102,  // data1 = WinUpdateResult*
    WIN_INSTALL_LAUNCH = 1103  // data1 = char* path to installer exe
};

struct WinUpdateResult {
    int has_update;   // 1 = update available, 0 = up to date, -1 = error
    char version[32];
    char url[512];
    char error_msg[128];
};

void startCheckAsync();
void startDownloadAndInstall(const char* url);

} // namespace WinUpdate

#else

namespace WinUpdate {
inline void startCheckAsync() {}
inline void startDownloadAndInstall(const char*) {}
}

#endif

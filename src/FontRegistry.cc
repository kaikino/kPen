#include "FontRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Returns true if the file name ends with a font extension (.ttf/.otf).
static bool endsWithFontExt(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".ttf" || ext == ".otf";
}

// Recursively scans a directory for font files and adds them to the output set.
// Parameters:
// - root:      the directory to scan
// - out:       the set of font paths
// - count:     the number of font files found
// - maxFiles:  the maximum number of font files to find
// - maxDepth:  the maximum depth to scan
// - depth:     the current depth of the scan
static void scanDir(const fs::path& root, std::set<std::string>& out, int& count, int maxFiles, int maxDepth, int depth) {
    if (count >= maxFiles || depth > maxDepth) return;
    std::error_code ec;
    if (!fs::exists(root, ec)) return;
    fs::directory_options opts = fs::directory_options::skip_permission_denied;
    // Robust directory iteration: skips permission-denied directories, tries
    // to resolve paths (i.e., symlinks) to canonical absolute paths, prevents
    // iterator from throwing an exception, iterates until maxDepth/maxFiles or
    // end of directory is reached.
    for (fs::directory_iterator it(root, opts, ec), end; it != end && count < maxFiles; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path& p = it->path();
        if (it->is_directory(ec)) {
            scanDir(p, out, count, maxFiles, maxDepth, depth + 1);
        } else if (it->is_regular_file(ec) && endsWithFontExt(p.filename().string())) {
            std::error_code ec2;
            std::string canon = fs::weakly_canonical(p, ec2).string();
            if (!ec2 && out.insert(canon).second) count++;
        }
    }
}

// Scans the system fonts and returns a list of font paths (.ttf/.otf files).
// Parameters:
// - extraDirs: a list of extra directories to scan
// Returns:
// - a list of font paths (.ttf/.otf files)
std::vector<std::string> FontRegistry_scanSystemFonts(const std::vector<std::string>& extraDirs) {
    std::set<std::string> uniq;
    int count = 0;
    const int kMaxFiles = 4000;
    const int kMaxDepth = 6;

    // First scan caller-provided extra directories.
    for (const std::string& d : extraDirs) {
        if (!d.empty()) scanDir(fs::path(d), uniq, count, kMaxFiles, kMaxDepth, 0);
    }

    // Then scan common system font directories, platform-dependent.
    // Currently supported: macOS, Windows, Linux.
#if defined(__APPLE__)
    scanDir("/System/Library/Fonts", uniq, count, kMaxFiles, kMaxDepth, 0);
    scanDir("/Library/Fonts", uniq, count, kMaxFiles, kMaxDepth, 0);
    if (const char* home = std::getenv("HOME"))
        scanDir(fs::path(home) / "Library/Fonts", uniq, count, kMaxFiles, kMaxDepth, 0);
#elif defined(_WIN32)
    if (const char* windir = std::getenv("WINDIR"))
        scanDir(fs::path(windir) / "Fonts", uniq, count, kMaxFiles, 2, 0);
#else
    scanDir("/usr/share/fonts", uniq, count, kMaxFiles, kMaxDepth, 0);
    scanDir("/usr/local/share/fonts", uniq, count, kMaxFiles, kMaxDepth, 0);
    if (const char* home = std::getenv("HOME"))
        scanDir(fs::path(home) / ".local/share/fonts", uniq, count, kMaxFiles, kMaxDepth, 0);
#endif

    std::vector<std::string> v(uniq.begin(), uniq.end());
    std::sort(v.begin(), v.end());
    return v;
}

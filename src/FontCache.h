#pragma once
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>

// FontCache is a helper around SDL2_ttf that keeps the underlying TTF_Font
// struct open by keeping a handle to it, giving this handle out when kPen asks
// for this font instead of calling TTF_OpenFont repeatedly. Manages the
// lifetime and use of the TTF_Font resource.

/** Open/cache TTF_Font by (path, pixel height, TTF style mask). */
class FontCache {
  public:
    FontCache();
    ~FontCache();
    // Entry point to a TTF_Font*, passes out handle to existing font, or
    // opens a path to a font and store it. Will evict an existing entry if cap
    // is reached (kMaxEntries).
    TTF_Font* get(const std::string& path, int pixelHeight, int ttfStyleMask);

  private:
    // Wrapper class enables flexibility to add metadata
    struct Entry {
        TTF_Font* font = nullptr;
    };
    // Stores handle -> opened font
    std::unordered_map<std::string, Entry> map_;
    // Build new key
    static std::string makeKey(const std::string& path, int px, int style);
    // Drop one entry before inserting into map at capacity
    void evictOneIfNeeded();
    // Max distinct fonts that can stay open at once
    static constexpr int kMaxEntries = 48;
};

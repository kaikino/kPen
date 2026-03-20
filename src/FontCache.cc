#include "FontCache.h"
#include <algorithm>
#include <cstdio>

// New keys are distinguished by path + pixel size + TTF style (no styling,
// bold, italic, etc.)
std::string FontCache::makeKey(const std::string& path, int px, int style) {
    char buf[32];
    snprintf(buf, sizeof(buf), "|%d|%d", px, style);
    return path + buf;
}

FontCache::FontCache() = default;

// SDL requires TTF_CloseFont when done
FontCache::~FontCache() {
    for (auto& kv : map_) {
        if (kv.second.font) TTF_CloseFont(kv.second.font);
    }
}

// Whatever unordered_map::begin() points to is evicted
void FontCache::evictOneIfNeeded() {
    if ((int)map_.size() < kMaxEntries) return;
    auto it = map_.begin();
    if (it->second.font) TTF_CloseFont(it->second.font);
    map_.erase(it);
}

TTF_Font* FontCache::get(const std::string& path, int pixelHeight, int ttfStyleMask) {
    if (path.empty()) return nullptr;
    pixelHeight = std::max(6, std::min(256, pixelHeight));
    std::string key = makeKey(path, pixelHeight, ttfStyleMask);
    auto it = map_.find(key);
    if (it != map_.end()) return it->second.font;

    evictOneIfNeeded();
    TTF_Font* f = TTF_OpenFont(path.c_str(), pixelHeight);
    if (!f) return nullptr;
    TTF_SetFontStyle(f, ttfStyleMask);
    map_[key].font = f;
    return f;
}

#include "UndoManager.h"
#include <algorithm>
#include <cstring>

int UndoManager::numTilesX(int w) {
    return (w + TILE - 1) / TILE;
}

int UndoManager::numTilesY(int h) {
    return (h + TILE - 1) / TILE;
}

int UndoManager::tileIndex(int tx, int ty, int numTX) {
    return ty * numTX + tx;
}

void UndoManager::invalidateCache() {
    cachedIndex_ = static_cast<size_t>(-1);
}

void UndoManager::reconstructState(size_t index, std::vector<uint32_t>& out, int& outW, int& outH) {
    outW = undoStack_[index].w;
    outH = undoStack_[index].h;
    size_t n = static_cast<size_t>(outW) * static_cast<size_t>(outH);
    out.resize(n);
    for (size_t i = 0; i <= index; i++) {
        const UndoEntry& e = undoStack_[i];
        if (e.is_full) {
            out = e.full_pixels;
            continue;
        }
        int ntx = numTilesX(e.w);
        for (const auto& p : e.tiles) {
            int ti = p.first;
            int ty = ti / ntx;
            int tx = ti % ntx;
            const std::vector<uint32_t>& tile = p.second;
            int baseX = tx * TILE;
            int baseY = ty * TILE;
            for (int dy = 0; dy < TILE; dy++) {
                int y = baseY + dy;
                if (y >= e.h) break;
                for (int dx = 0; dx < TILE; dx++) {
                    int x = baseX + dx;
                    if (x >= e.w) break;
                    out[static_cast<size_t>(y) * e.w + x] = tile[static_cast<size_t>(dy) * TILE + dx];
                }
            }
        }
    }
    outW = undoStack_[index].w;
    outH = undoStack_[index].h;
}

int UndoManager::pushUndo(int w, int h, const std::vector<uint32_t>& pixels) {
    redoStack_.clear();
    invalidateCache();
    UndoEntry e;
    e.w = w;
    e.h = h;
    e.serial = nextStateSerial_++;
    bool pushFull = undoStack_.empty() ||
        (undoStack_.back().w != w || undoStack_.back().h != h);
    if (pushFull) {
        e.is_full = true;
        e.full_pixels = pixels;
        undoStack_.push_back(std::move(e));
        return undoStack_.back().serial;
    }
    int ntx = numTilesX(w);
    int nty = numTilesY(h);
    workBuffer_.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    int prevW, prevH;
    reconstructState(undoStack_.size() - 1, workBuffer_, prevW, prevH);
    e.is_full = false;
    for (int ty = 0; ty < nty; ty++) {
        for (int tx = 0; tx < ntx; tx++) {
            int baseX = tx * TILE;
            int baseY = ty * TILE;
            bool changed = false;
            for (int dy = 0; dy < TILE && !changed; dy++) {
                int y = baseY + dy;
                if (y >= h) break;
                for (int dx = 0; dx < TILE; dx++) {
                    int x = baseX + dx;
                    if (x >= w) break;
                    size_t idx = static_cast<size_t>(y) * w + x;
                    if (pixels[idx] != workBuffer_[idx]) { changed = true; break; }
                }
            }
            if (!changed) continue;
            std::vector<uint32_t> tile(static_cast<size_t>(TILE) * TILE, 0u);
            for (int dy = 0; dy < TILE; dy++) {
                int y = baseY + dy;
                for (int dx = 0; dx < TILE; dx++) {
                    int x = baseX + dx;
                    uint32_t val = 0;
                    if (x < w && y < h)
                        val = pixels[static_cast<size_t>(y) * w + x];
                    tile[static_cast<size_t>(dy) * TILE + dx] = val;
                }
            }
            e.tiles.push_back({ tileIndex(tx, ty, ntx), std::move(tile) });
        }
    }
    undoStack_.push_back(std::move(e));
    return undoStack_.back().serial;
}

void UndoManager::replaceTopUndo(int w, int h, const std::vector<uint32_t>& pixels) {
    if (undoStack_.empty()) return;
    invalidateCache();
    UndoEntry& e = undoStack_.back();
    e.w = w;
    e.h = h;
    e.is_full = true;
    e.full_pixels = pixels;
    e.tiles.clear();
}

void UndoManager::setUndoTopPixels(const std::vector<uint32_t>& pixels) {
    if (undoStack_.empty()) return;
    invalidateCache();
    UndoEntry& e = undoStack_.back();
    e.is_full = true;
    e.full_pixels = pixels;
    e.tiles.clear();
}

CanvasState* UndoManager::getUndoTop() {
    if (undoStack_.empty()) return nullptr;
    size_t idx = undoStack_.size() - 1;
    if (cachedIndex_ == idx) return &reconstructed_;
    int w, h;
    reconstructState(idx, reconstructed_.pixels, w, h);
    reconstructed_.w = w;
    reconstructed_.h = h;
    reconstructed_.serial = undoStack_[idx].serial;
    cachedIndex_ = idx;
    return &reconstructed_;
}

size_t UndoManager::getUndoSize() const {
    return undoStack_.size();
}

void UndoManager::popUndo() {
    if (undoStack_.empty()) return;
    invalidateCache();
    undoStack_.pop_back();
}

void UndoManager::pushRedo(CanvasState s) {
    redoStack_.push_back(std::move(s));
}

void UndoManager::pushUndoKeepSerial(CanvasState s) {
    invalidateCache();
    UndoEntry e;
    e.w = s.w;
    e.h = s.h;
    e.serial = s.serial;
    e.is_full = true;
    e.full_pixels = std::move(s.pixels);
    undoStack_.push_back(std::move(e));
}

CanvasState* UndoManager::getRedoTop() {
    return redoStack_.empty() ? nullptr : &redoStack_.back();
}

void UndoManager::popRedo() {
    if (!redoStack_.empty()) redoStack_.pop_back();
}

void UndoManager::clearRedo() {
    redoStack_.clear();
}

void UndoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
    invalidateCache();
}

int UndoManager::currentSerial() const {
    return undoStack_.empty() ? 0 : undoStack_.back().serial;
}

bool UndoManager::redoEmpty() const {
    return redoStack_.empty();
}

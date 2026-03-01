#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <utility>

struct CanvasState {
    int w = 0, h = 0;
    std::vector<uint32_t> pixels;
    int serial = 0;
};

class UndoManager {
public:
    static constexpr int TILE = 32;

    // Push current canvas state to undo; clears redo. Returns serial.
    int pushUndo(int w, int h, const std::vector<uint32_t>& pixels);

    // Replace top-of-undo in place (e.g. resizeCanvas pre-resize refresh).
    void replaceTopUndo(int w, int h, const std::vector<uint32_t>& pixels);

    // Replace only the pixel buffer of the top undo state (e.g. doOpen after load).
    void setUndoTopPixels(const std::vector<uint32_t>& pixels);

    CanvasState* getUndoTop();
    size_t getUndoSize() const;
    void popUndo();

    // Push state to redo; undo() stashes current before applying previous.
    void pushRedo(CanvasState s);

    // Push state to undo keeping its serial; redo() moves redo top back to undo.
    void pushUndoKeepSerial(CanvasState s);

    CanvasState* getRedoTop();
    void popRedo();
    void clearRedo();
    void clear();

    int currentSerial() const;
    bool redoEmpty() const;

private:
    struct UndoEntry {
        int w = 0, h = 0, serial = 0;
        bool is_full = true;
        std::vector<uint32_t> full_pixels;
        std::vector<std::pair<int, std::vector<uint32_t>>> tiles;
    };
    static int numTilesX(int w);
    static int numTilesY(int h);
    static int tileIndex(int tx, int ty, int numTX);
    void reconstructState(size_t index, std::vector<uint32_t>& out, int& outW, int& outH);
    void invalidateCache();

    std::vector<UndoEntry> undoStack_;
    std::vector<CanvasState> redoStack_;
    int nextStateSerial_ = 1;
    CanvasState reconstructed_;
    size_t cachedIndex_ = static_cast<size_t>(-1);
    std::vector<uint32_t> workBuffer_;
};

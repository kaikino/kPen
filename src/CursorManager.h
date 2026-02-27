#pragma once
#include <SDL2/SDL.h>
#include "Tools.h"

// ── CursorManager ─────────────────────────────────────────────────────────────
//
// Owns every SDL_Cursor the app uses. Call update() once per frame.
//
// Brush cursor  – SDL_CreateColorCursor, filled with current brush color.
// Eraser cursor – SDL_CreateColorCursor, hollow with cornflower-blue outline.
// All other cursors are SDL system cursors (no images needed), except the
// bucket (fill) cursor which is a hand-drawn color bitmap.

class CursorManager {
public:
    CursorManager();
    ~CursorManager();

    // Call once per frame after event processing.
    void init();  // Call after SDL_Init and SDL_CreateWindow
    void update(ICoordinateMapper* mapper,
                ToolType currentType, ToolType originalType,
                AbstractTool* currentTool,
                int brushSize, bool squareBrush,
                SDL_Color brushColor,
                int mouseWinX, int mouseWinY,
                bool overToolbar);

private:
    // System cursors
    SDL_Cursor* curArrow    = nullptr;
    SDL_Cursor* curCross    = nullptr;
    SDL_Cursor* curSizeAll  = nullptr;
    SDL_Cursor* curSizeNS   = nullptr;
    SDL_Cursor* curSizeWE   = nullptr;
    SDL_Cursor* curSizeNWSE = nullptr;
    SDL_Cursor* curSizeNESW = nullptr;

    // Custom bitmap cursors
    SDL_Cursor* curBucket   = nullptr;  // fill tool — rebuilt on color change
    SDL_Cursor* curBrush    = nullptr;  // rebuilt on change
    SDL_Cursor* curEraser   = nullptr;  // rebuilt on change

    // Cache for brush cursor rebuild only
    int       lastWinSize     = -1;
    bool      lastSquareBrush = false;
    SDL_Color lastBrushColor  = {0, 0, 0, 255};

    // Cache for bucket cursor rebuild
    SDL_Color lastBucketColor = {255, 255, 255, 0}; // invalid sentinel

    void buildBucketCursor(SDL_Color color);
    void buildBrushCursors(ICoordinateMapper* mapper, int brushSize,
                           bool squareBrush, SDL_Color color);

    SDL_Cursor* makeColorCursor(const Uint32* argb, int w, int h, int hotX, int hotY);

    void fillCircle  (Uint32* buf, int w, int h, int cx, int cy, int r,    Uint32 color);
    void fillSquare  (Uint32* buf, int w, int h, int cx, int cy, int half, Uint32 color);
    void outlineCircle(Uint32* buf, int w, int h, int cx, int cy, int r,   Uint32 color);
    void outlineSquare(Uint32* buf, int w, int h, int cx, int cy, int half,Uint32 color);

    void buildBucketCursor();
    void setCursor(SDL_Cursor* c);      // always applies (no cache)
    void forceSetCursor(SDL_Cursor* c); // alias for setCursor
};

#pragma once
#include <SDL2/SDL.h>
#include "Tools.h"
#include "CanvasResizer.h"

// ── CursorManager ─────────────────────────────────────────────────────────────
//
// Owns every SDL_Cursor the app uses. Call update() once per frame.
//
// Brush cursor  – SDL_CreateColorCursor, filled with current brush color.
// Eraser cursor – SDL_CreateColorCursor, hollow with cornflower-blue outline.
// Resize cursors – 8 custom bitmap double-headed arrows, rebuilt whenever the
//                  active shape's rotation changes, so handles always show the
//                  correct drag direction regardless of shape rotation.
// All other cursors are SDL system cursors (no images needed), except the
// bucket (fill) cursor which is a hand-drawn color bitmap.

class CursorManager {
public:
    CursorManager();
    ~CursorManager();

    // Call once per frame after event processing.
    void init();  // Call after SDL_Init and SDL_CreateWindow
    SDL_Cursor* getHandCursor()  const { return curHand; }
    SDL_Cursor* getArrowCursor() const { return curArrow; }
    void forceSetCursor(SDL_Cursor* c); // apply cursor immediately
    void update(ICoordinateMapper* mapper,
                ToolType currentType, ToolType originalType,
                AbstractTool* currentTool,
                int brushSize, bool squareBrush,
                SDL_Color brushColor,
                int mouseWinX, int mouseWinY,
                bool overToolbar,
                bool overCanvas,
                bool nearHandle,
                const CanvasResizer* canvasResizer = nullptr,
                int canvasW = 0, int canvasH = 0,
                const SDL_Color* pickHoverColor = nullptr);

private:
    // System cursors
    SDL_Cursor* curArrow    = nullptr;
    SDL_Cursor* curCross    = nullptr;  // custom large crosshair (replaces system cursor)
    SDL_Cursor* curHand     = nullptr;
    SDL_Cursor* curSizeAll  = nullptr;
    SDL_Cursor* curSizeNS   = nullptr;
    SDL_Cursor* curSizeWE   = nullptr;
    SDL_Cursor* curSizeNWSE = nullptr;
    SDL_Cursor* curSizeNESW = nullptr;

    // Custom bitmap cursors
    SDL_Cursor* curBucket    = nullptr;  // fill tool — rebuilt on color change
    SDL_Cursor* curBrush     = nullptr;  // rebuilt on change
    SDL_Cursor* curEraser    = nullptr;  // rebuilt on change
    SDL_Cursor* curPick      = nullptr;  // eyedropper — tip color = pixel under cursor
    SDL_Color   lastPickTipColor = { 255, 255, 255, 0 };  // invalid sentinel
    SDL_Cursor* curCrossHairBrush  = nullptr;  // tiny-brush crosshair (brush color dot)
    SDL_Cursor* curCrossHairEraser = nullptr;  // tiny-brush crosshair (eraser color dot)
    int         lastCrossHairWinSz  = -1;
    bool        lastCrossHairSquare = false;
    SDL_Color   lastCrossHairColor  = {255, 255, 255, 0};  // invalid sentinel

    // Window-pixel threshold: if the brush would render smaller than this, show the
    // crosshair cursor instead of the brush/eraser shape so the hotspot stays precise.
    static constexpr int TINY_BRUSH_WIN_PX = 14;

    // 8 rotated resize-arrow cursors (slots 0–7 = 0°, 45°, 90°, … 315°).
    // Slot i points in direction i*45° clockwise from North (up).
    // Rebuilt whenever the shape rotation changes by more than 0.5°.
    static constexpr int NUM_RESIZE_SLOTS = 8;
    SDL_Cursor* curResize[NUM_RESIZE_SLOTS] = {};
    float       lastResizeRotationDeg = -9999.f;  // sentinel — forces first build
    bool        lastResizeFlipX       = false;
    bool        lastResizeFlipY       = false;

    // Rotate-handle cursor: a curved arc arrow, rebuilt on rotation change.
    SDL_Cursor* curRotate            = nullptr;
    float       lastRotateCursorDeg   = -9999.f;

    // Cursor lock during active drags — keeps the cursor stable even when the
    // mouse wanders off the shape (e.g. rotating with the mouse far from centre).
    bool                    dragHandleLocked = false;
    TransformTool::Handle   lockedHandle     = TransformTool::Handle::NONE;

    // Locked directional cursor for canvas resize drags.
    SDL_Cursor*             dragResizeCursor = nullptr;

    // Cache for brush cursor rebuild only
    int       lastWinSize     = -1;
    bool      lastSquareBrush = false;
    SDL_Color lastBrushColor  = {0, 0, 0, 255};

    // Cache for bucket cursor rebuild
    SDL_Color lastBucketColor = {255, 255, 255, 0}; // invalid sentinel

    void buildBucketCursor(SDL_Color color);
    void buildPickCursor(SDL_Color tipColor);
    void buildBrushCursors(ICoordinateMapper* mapper, int brushSize,
                           bool squareBrush, SDL_Color color);

    // Build/rebuild the 8 directional resize cursors (rotation + optional flip for during-drag).
    void buildResizeCursors(float rotationRad, bool flipX, bool flipY);

    void buildRotateCursor(float rotationRad);

    // Return the resize cursor for a given handle (pass current flip so cursor matches shape on hover and during drag).
    SDL_Cursor* getResizeCursor(TransformTool::Handle h, float rotationRad, bool flipX, bool flipY);

    // Draw a double-headed arrow into a Bitmap, pointing at angleDeg clockwise
    // from north (up), then rotate the bitmap. Returns a new SDL_Cursor.
    static SDL_Cursor* makeResizeArrowCursor(float angleDeg);

    // Draw a curved arc-arrow cursor rotated to angleDeg. Returns a new SDL_Cursor.
    static SDL_Cursor* makeRotateCursor(float angleDeg);

    // Crosshair: XOR center dot (sized to current brush window pixels) + thin arms.
    // Rebuilt whenever the effective dot size changes.
    static SDL_Cursor* makeCrossHairCursor(int dotRadius, bool squareDot = false, Uint32 dotColor = 0xFF000000);
    void buildCrossHairCursor(int winSz, bool squareBrush, SDL_Color brushColor);

    SDL_Cursor* makeColorCursor(const Uint32* argb, int w, int h, int hotX, int hotY);

    void fillCircle  (Uint32* buf, int w, int h, int cx, int cy, int r,    Uint32 color);
    void fillSquare  (Uint32* buf, int w, int h, int cx, int cy, int half, Uint32 color);
    void outlineCircle(Uint32* buf, int w, int h, int cx, int cy, int r,   Uint32 color);
    void outlineSquare(Uint32* buf, int w, int h, int cx, int cy, int half,Uint32 color);

    void setCursor(SDL_Cursor* c);      // always applies (no cache)
};

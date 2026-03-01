#pragma once
#include <SDL2/SDL.h>
#include "Tools.h"
#include "CanvasResizer.h"

// Owns all SDL cursors. Call init() after SDL_Init; update() once per frame.
class CursorManager {
public:
    CursorManager();
    ~CursorManager();

    void init();
    SDL_Cursor* getHandCursor()  const { return curHand; }
    SDL_Cursor* getArrowCursor() const { return curArrow; }
    void forceSetCursor(SDL_Cursor* c);
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
    SDL_Cursor* curArrow    = nullptr;
    SDL_Cursor* curCross    = nullptr;
    SDL_Cursor* curHand     = nullptr;
    SDL_Cursor* curSizeAll  = nullptr;
    SDL_Cursor* curSizeNS   = nullptr;
    SDL_Cursor* curSizeWE   = nullptr;
    SDL_Cursor* curSizeNWSE = nullptr;
    SDL_Cursor* curSizeNESW = nullptr;

    SDL_Cursor* curBucket    = nullptr;
    SDL_Cursor* curBrush     = nullptr;
    SDL_Cursor* curEraser    = nullptr;
    SDL_Cursor* curPick      = nullptr;
    SDL_Color   lastPickTipColor = { 255, 255, 255, 0 };
    SDL_Cursor* curCrossHairBrush  = nullptr;
    SDL_Cursor* curCrossHairEraser = nullptr;
    int         lastCrossHairWinSz  = -1;
    bool        lastCrossHairSquare = false;
    SDL_Color   lastCrossHairColor  = {255, 255, 255, 0};

    static constexpr int TINY_BRUSH_WIN_PX = 14;
    static constexpr int NUM_RESIZE_SLOTS = 8;
    SDL_Cursor* curResize[NUM_RESIZE_SLOTS] = {};
    float       lastResizeRotationDeg = -9999.f;
    bool        lastResizeFlipX       = false;
    bool        lastResizeFlipY       = false;

    SDL_Cursor* curRotate            = nullptr;
    float       lastRotateCursorDeg   = -9999.f;

    bool                    dragHandleLocked = false;
    TransformTool::Handle   lockedHandle     = TransformTool::Handle::NONE;
    SDL_Cursor*             dragResizeCursor = nullptr;

    int       lastWinSize     = -1;
    bool      lastSquareBrush = false;
    SDL_Color lastBrushColor  = {0, 0, 0, 255};
    SDL_Color lastBucketColor = {255, 255, 255, 0};

    void buildBucketCursor(SDL_Color color);
    void buildPickCursor(SDL_Color tipColor);
    void buildBrushCursors(ICoordinateMapper* mapper, int brushSize,
                           bool squareBrush, SDL_Color color);
    void buildResizeCursors(float rotationRad, bool flipX, bool flipY);
    void buildRotateCursor(float rotationRad);
    SDL_Cursor* getResizeCursor(TransformTool::Handle h, float rotationRad, bool flipX, bool flipY);

    static SDL_Cursor* makeResizeArrowCursor(float angleDeg);
    static SDL_Cursor* makeRotateCursor(float angleDeg);
    static SDL_Cursor* makeCrossHairCursor(int dotRadius, bool squareDot = false, Uint32 dotColor = 0xFF000000);
    void buildCrossHairCursor(int winSz, bool squareBrush, SDL_Color brushColor);

    SDL_Cursor* makeColorCursor(const Uint32* argb, int w, int h, int hotX, int hotY);

    void fillCircle  (Uint32* buf, int w, int h, int cx, int cy, int r,    Uint32 color);
    void fillSquare  (Uint32* buf, int w, int h, int cx, int cy, int half, Uint32 color);
    void outlineCircle(Uint32* buf, int w, int h, int cx, int cy, int r,   Uint32 color);
    void outlineSquare(Uint32* buf, int w, int h, int cx, int cy, int half,Uint32 color);

    void setCursor(SDL_Cursor* c);
};

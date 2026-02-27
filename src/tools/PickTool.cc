#include "Tools.h"
#include <algorithm>

// ── PickTool ──────────────────────────────────────────────────────────────────
//
// Reads the ARGB pixel at the clicked canvas coordinate and fires the callback.
// Clamped to canvas bounds before reading.

static void samplePixel(int cX, int cY, SDL_Renderer* r,
                         ICoordinateMapper* mapper,
                         const std::function<void(SDL_Color)>& cb) {
    int cw, ch;
    mapper->getCanvasSize(&cw, &ch);
    int x = std::max(0, std::min(cw - 1, cX));
    int y = std::max(0, std::min(ch - 1, cY));

    // Read 1×1 pixel from the canvas render target (currently active).
    SDL_Rect r1 = { x, y, 1, 1 };
    Uint32 pixel = 0;
    SDL_RenderReadPixels(r, &r1, SDL_PIXELFORMAT_ARGB8888, &pixel, 4);

    SDL_Color c;
    c.a = (pixel >> 24) & 0xFF;
    c.r = (pixel >> 16) & 0xFF;
    c.g = (pixel >>  8) & 0xFF;
    c.b =  pixel        & 0xFF;

    // Don't pick fully-transparent pixels — there's no meaningful color there.
    if (c.a == 0) return;

    cb(c);
}

void PickTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int /*brushSize*/, SDL_Color /*color*/) {
    isDrawing = true;
    startX = lastX = cX;
    startY = lastY = cY;
    samplePixel(cX, cY, r, mapper, onColorPicked);
}

void PickTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int /*brushSize*/, SDL_Color /*color*/) {
    if (!isDrawing) return;
    lastX = cX;
    lastY = cY;
    samplePixel(cX, cY, r, mapper, onColorPicked);
}

#include "Tools.h"
#include "DrawingUtils.h"
#include <algorithm>

static bool onCanvas(ICoordinateMapper* m, int cX, int cY) {
    int cw, ch; m->getCanvasSize(&cw, &ch);
    return cX >= 0 && cX < cw && cY >= 0 && cY < ch;
}

// Eraser draws fully transparent pixels by temporarily switching blend mode to NONE.
static void eraserDraw(SDL_Renderer* r, int x0, int y0, int x1, int y1, int brushSize, int cw, int ch) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    DrawingUtils::drawLine(r, x0, y0, x1, y1, brushSize, cw, ch);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

static void eraserSquareStamp(SDL_Renderer* r, int cx, int cy, int brushSize, int cw, int ch) {
    int half = brushSize / 2;
    int x0 = std::max(0, cx - half);
    int y0 = std::max(0, cy - half);
    int x1 = std::min(cw - 1, cx - half + brushSize - 1);
    int y1 = std::min(ch - 1, cy - half + brushSize - 1);
    if (x1 < x0 || y1 < y0) return;
    SDL_Rect sq = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderFillRect(r, &sq);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

static void eraserSquareLine(SDL_Renderer* r, int x0, int y0, int x1, int y1,
                              int brushSize, int cw, int ch) {
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        eraserSquareStamp(r, x0, y0, brushSize, cw, ch);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void EraserTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color /*color*/) {
    AbstractTool::onMouseDown(cX, cY, r, brushSize, {0,0,0,0});
    if (onCanvas(mapper, cX, cY)) {
        int cw, ch; mapper->getCanvasSize(&cw, &ch);
        if (squareBrush) eraserSquareStamp(r, cX, cY, brushSize, cw, ch);
        else             eraserDraw(r, cX, cY, cX, cY, brushSize, cw, ch);
    }
}

void EraserTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color /*color*/) {
    if (isDrawing) {
        if (onCanvas(mapper, cX, cY) || onCanvas(mapper, lastX, lastY)) {
            int cw, ch; mapper->getCanvasSize(&cw, &ch);
            if (squareBrush) eraserSquareLine(r, lastX, lastY, cX, cY, brushSize, cw, ch);
            else             eraserDraw(r, lastX, lastY, cX, cY, brushSize, cw, ch);
        }
        lastX = cX;
        lastY = cY;
    }
}

void EraserTool::onPreviewRender(SDL_Renderer* /*r*/, int /*brushSize*/, SDL_Color /*color*/) {
    // No preview rendering for EraserTool
}

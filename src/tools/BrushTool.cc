#include "Tools.h"
#include "DrawingUtils.h"
#include <algorithm>

// BrushTool uses its mapper to get the canvas size so it works at any resolution.
static bool onCanvas(ICoordinateMapper* m, int cX, int cY) {
    int cw, ch; m->getCanvasSize(&cw, &ch);
    return cX >= 0 && cX < cw && cY >= 0 && cY < ch;
}

static void brushSetColor(SDL_Renderer* r, SDL_Color color) {
    if (color.a == 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    } else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
    }
}
static void brushRestoreBlend(SDL_Renderer* r, SDL_Color color) {
    if (color.a == 0) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

// Draw a filled square stamp of size brushSize centered at (cx,cy), clipped to canvas.
static void drawSquareStamp(SDL_Renderer* r, int cx, int cy, int brushSize, int cw, int ch) {
    int half = brushSize / 2;
    int x0 = std::max(0, cx - half);
    int y0 = std::max(0, cy - half);
    int x1 = std::min(cw - 1, cx - half + brushSize - 1);
    int y1 = std::min(ch - 1, cy - half + brushSize - 1);
    if (x1 < x0 || y1 < y0) return;
    SDL_Rect sq = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
    SDL_RenderFillRect(r, &sq);
}

// Walk a line stamping square brushes at each step (Bresenham).
static void drawSquareLine(SDL_Renderer* r, int x0, int y0, int x1, int y1,
                            int brushSize, int cw, int ch) {
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        drawSquareStamp(r, x0, y0, brushSize, cw, ch);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void BrushTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
    if (onCanvas(mapper, cX, cY)) {
        int cw, ch; mapper->getCanvasSize(&cw, &ch);
        brushSetColor(canvasRenderer, color);
        if (squareBrush)
            drawSquareStamp(canvasRenderer, cX, cY, brushSize, cw, ch);
        else
            DrawingUtils::drawLine(canvasRenderer, cX, cY, cX, cY, brushSize, cw, ch);
        brushRestoreBlend(canvasRenderer, color);
    }
}

void BrushTool::onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (isDrawing) {
        if (onCanvas(mapper, cX, cY) || onCanvas(mapper, lastX, lastY)) {
            int cw, ch; mapper->getCanvasSize(&cw, &ch);
            brushSetColor(canvasRenderer, color);
            if (squareBrush)
                drawSquareLine(canvasRenderer, lastX, lastY, cX, cY, brushSize, cw, ch);
            else
                DrawingUtils::drawLine(canvasRenderer, lastX, lastY, cX, cY, brushSize, cw, ch);
            brushRestoreBlend(canvasRenderer, color);
        }
        lastX = cX;
        lastY = cY;
    }
}

void BrushTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
    // No preview rendering for BrushTool
}

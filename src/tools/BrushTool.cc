#include "Tools.h"
#include "DrawingUtils.h"

// BrushTool uses its mapper to get the canvas size so it works at any resolution.
static bool onCanvas(ICoordinateMapper* m, int cX, int cY) {
    int cw, ch; m->getCanvasSize(&cw, &ch);
    return cX >= 0 && cX < cw && cY >= 0 && cY < ch;
}

void BrushTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
    if (onCanvas(mapper, cX, cY)) {
        SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
        DrawingUtils::drawFillCircle(canvasRenderer, cX, cY, brushSize / 2);
    }
}

void BrushTool::onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (isDrawing) {
        // Only draw the segment if at least one endpoint is on the canvas.
        // drawLine itself clips to the canvas texture bounds, so we just need
        // to avoid calling it when both points are entirely off-canvas.
        if (onCanvas(mapper, cX, cY) || onCanvas(mapper, lastX, lastY)) {
            int cw, ch; mapper->getCanvasSize(&cw, &ch);
            SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
            DrawingUtils::drawLine(canvasRenderer, lastX, lastY, cX, cY, brushSize, cw, ch);
        }
        lastX = cX;
        lastY = cY;
    }
}

void BrushTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
    // No preview rendering for BrushTool
}
#include "Tools.h"
#include "DrawingUtils.h"

static bool onCanvas(int cX, int cY) {
    return cX >= 0 && cX < CANVAS_WIDTH && cY >= 0 && cY < CANVAS_HEIGHT;
}

void BrushTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
    if (onCanvas(cX, cY)) {
        SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
        DrawingUtils::drawFillCircle(canvasRenderer, cX, cY, brushSize / 2);
    }
}

void BrushTool::onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (isDrawing) {
        // Only draw the segment if at least one endpoint is on the canvas.
        // drawLine itself clips to the canvas texture bounds, so we just need
        // to avoid calling it when both points are entirely off-canvas.
        if (onCanvas(cX, cY) || onCanvas(lastX, lastY)) {
            SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
            DrawingUtils::drawLine(canvasRenderer, lastX, lastY, cX, cY, brushSize);
        }
        lastX = cX;
        lastY = cY;
    }
}

void BrushTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
    // No preview rendering for BrushTool
}

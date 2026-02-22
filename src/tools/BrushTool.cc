#include "Tools.h"
#include "DrawingUtils.h"

void BrushTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
    SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
    DrawingUtils::drawFillCircle(canvasRenderer, cX, cY, brushSize / 2);
}

void BrushTool::onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (isDrawing) {
        SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
        DrawingUtils::drawLine(canvasRenderer, lastX, lastY, cX, cY, brushSize);
        lastX = cX;
        lastY = cY;
    }
}

void BrushTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
    // No preview rendering for BrushTool
}

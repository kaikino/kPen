#include "Tools.h"
#include "DrawingUtils.h"

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

void BrushTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
    if (onCanvas(mapper, cX, cY)) {
        int cw, ch; mapper->getCanvasSize(&cw, &ch);
        brushSetColor(canvasRenderer, color);
        DrawingUtils::drawLine(canvasRenderer, cX, cY, cX, cY, brushSize, cw, ch);
        brushRestoreBlend(canvasRenderer, color);
    }
}

void BrushTool::onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (isDrawing) {
        if (onCanvas(mapper, cX, cY) || onCanvas(mapper, lastX, lastY)) {
            int cw, ch; mapper->getCanvasSize(&cw, &ch);
            brushSetColor(canvasRenderer, color);
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

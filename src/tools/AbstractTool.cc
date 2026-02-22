#include "Tools.h"

void AbstractTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    isDrawing = true;
    startX = lastX = cX;
    startY = lastY = cY;
}

void AbstractTool::onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (isDrawing) {
        lastX = cX;
        lastY = cY;
    }
}

bool AbstractTool::onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    bool changed = isDrawing;
    isDrawing = false;
    return changed;
}

void AbstractTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {}

void AbstractTool::onOverlayRender(SDL_Renderer* overlayRenderer) {}

bool AbstractTool::hasOverlayContent() {return false;}

void AbstractTool::deactivate(SDL_Renderer* canvasRenderer) {}

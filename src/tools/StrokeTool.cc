#include "Tools.h"

void StrokeTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    AbstractTool::onMouseDown(cX, cY, r, brushSize, color);
    if (isPointOnCanvas(mapper, cX, cY)) {
        int cw, ch;
        mapper->getCanvasSize(&cw, &ch);
        stampAt(r, cX, cY, brushSize, cw, ch, color);
    }
}

void StrokeTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (isDrawing) {
        if (isPointOnCanvas(mapper, cX, cY) || isPointOnCanvas(mapper, lastX, lastY)) {
            int cw, ch;
            mapper->getCanvasSize(&cw, &ch);
            drawSegment(r, lastX, lastY, cX, cY, brushSize, cw, ch, color);
        }
        lastX = cX;
        lastY = cY;
    }
}

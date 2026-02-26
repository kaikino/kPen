#include "Tools.h"
#include "DrawingUtils.h"

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

void EraserTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color /*color*/) {
    AbstractTool::onMouseDown(cX, cY, r, brushSize, {0,0,0,0});
    if (onCanvas(mapper, cX, cY)) {
        int cw, ch; mapper->getCanvasSize(&cw, &ch);
        eraserDraw(r, cX, cY, cX, cY, brushSize, cw, ch);
    }
}

void EraserTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color /*color*/) {
    if (isDrawing) {
        if (onCanvas(mapper, cX, cY) || onCanvas(mapper, lastX, lastY)) {
            int cw, ch; mapper->getCanvasSize(&cw, &ch);
            eraserDraw(r, lastX, lastY, cX, cY, brushSize, cw, ch);
        }
        lastX = cX;
        lastY = cY;
    }
}

void EraserTool::onPreviewRender(SDL_Renderer* /*r*/, int /*brushSize*/, SDL_Color /*color*/) {
    // No preview rendering for EraserTool
}

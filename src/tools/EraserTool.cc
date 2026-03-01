#include "Tools.h"
#include "DrawingUtils.h"

static const SDL_Color ERASER_COLOR = { 0, 0, 0, 0 };

void EraserTool::stampAt(SDL_Renderer* r, int cx, int cy, int brushSize, int cw, int ch, SDL_Color /*color*/) {
    if (squareBrush)
        DrawingUtils::drawSquareStamp(r, cx, cy, brushSize, cw, ch, ERASER_COLOR);
    else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        DrawingUtils::drawLine(r, cx, cy, cx, cy, brushSize, cw, ch);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
}

void EraserTool::drawSegment(SDL_Renderer* r, int x0, int y0, int x1, int y1, int brushSize, int cw, int ch, SDL_Color /*color*/) {
    if (squareBrush) {
        DrawingUtils::drawSquareLine(r, x0, y0, x1, y1, brushSize, cw, ch, ERASER_COLOR);
    } else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        DrawingUtils::drawLine(r, x0, y0, x1, y1, brushSize, cw, ch);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
}

void EraserTool::onPreviewRender(SDL_Renderer* /*r*/, int /*brushSize*/, SDL_Color /*color*/) {
}

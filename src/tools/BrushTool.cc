#include "Tools.h"
#include "DrawingUtils.h"

void BrushTool::stampAt(SDL_Renderer* r, int cx, int cy, int brushSize, int cw, int ch, SDL_Color color) {
    if (squareBrush)
        DrawingUtils::drawSquareStamp(r, cx, cy, brushSize, cw, ch, color);
    else {
        if (color.a == 0) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        } else {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
        }
        DrawingUtils::drawLine(r, cx, cy, cx, cy, brushSize, cw, ch);
        if (color.a == 0) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
}

void BrushTool::drawSegment(SDL_Renderer* r, int x0, int y0, int x1, int y1, int brushSize, int cw, int ch, SDL_Color color) {
    if (squareBrush) {
        DrawingUtils::drawSquareLine(r, x0, y0, x1, y1, brushSize, cw, ch, color);
    } else {
        if (color.a == 0) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        } else {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
        }
        DrawingUtils::drawLine(r, x0, y0, x1, y1, brushSize, cw, ch);
        if (color.a == 0) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
}

void BrushTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
}

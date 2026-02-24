#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>

ShapeTool::ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb)
    : AbstractTool(m), type(t), onShapeReady(std::move(cb)) {}

bool ShapeTool::onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (!isDrawing) return false;
    if (cX == startX && cY == startY) {
        isDrawing = false;
        return false;
    }

    int half = std::max(1, brushSize / 2);
    int shapeMinX = std::min(startX, cX),  shapeMaxX = std::max(startX, cX);
    int shapeMinY = std::min(startY, cY),  shapeMaxY = std::max(startY, cY);

    SDL_Rect bounds;
    if (type == ToolType::LINE) {
        bounds = {
            shapeMinX - half, shapeMinY - half,
            (shapeMaxX - shapeMinX + 1) + half * 2,
            (shapeMaxY - shapeMinY + 1) + half * 2
        };
    } else {
        bounds = {
            shapeMinX, shapeMinY,
            (shapeMaxX - shapeMinX + 1),
            (shapeMaxY - shapeMinY + 1)
        };
    }
    isDrawing = false;

    // Hand shape params to kPen which will create a ResizeTool
    if (onShapeReady)
        onShapeReady(type, bounds, startX, startY, cX, cY, brushSize, color);

    // Canvas unchanged here — ResizeTool commits on deactivate
    return false;
}

void ShapeTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
    if (!isDrawing) return;
    int winStartX, winStartY, winCurX, winCurY;
    mapper->getWindowCoords(startX, startY, &winStartX, &winStartY);
    
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    int curX, curY;
    // Use unclamped mapping so the preview follows the cursor outside the canvas edge
    {
        int wx1, wy1, wx2, wy2;
        mapper->getWindowCoords(0, 0, &wx1, &wy1);
        mapper->getWindowCoords(CANVAS_WIDTH, CANVAS_HEIGHT, &wx2, &wy2);
        int vw = wx2 - wx1, vh = wy2 - wy1;
        curX = vw > 0 ? (int)std::floor((mouseX - wx1) * ((float)CANVAS_WIDTH  / vw)) : 0;
        curY = vh > 0 ? (int)std::floor((mouseY - wy1) * ((float)CANVAS_HEIGHT / vh)) : 0;
    }
    if (curX == startX && curY == startY) return;

    mapper->getWindowCoords(curX, curY, &winCurX, &winCurY);
    int scaledBrush = mapper->getWindowSize(brushSize);
    SDL_SetRenderDrawColor(winRenderer, color.r, color.g, color.b, 255);

    int winW, winH;
    SDL_GetRendererOutputSize(winRenderer, &winW, &winH);
    int scaledHalf = scaledBrush / 2;
    if (type == ToolType::LINE) DrawingUtils::drawLine(winRenderer, winStartX, winStartY, winCurX, winCurY, scaledBrush, winW, winH);
    else if (type == ToolType::RECT) {
        SDL_Rect r = { std::min(winStartX, winCurX) + scaledHalf, std::min(winStartY, winCurY) + scaledHalf,
                        std::abs(winCurX - winStartX) - scaledHalf * 2, std::abs(winCurY - winStartY) - scaledHalf * 2 };
        if (r.w > 0 && r.h > 0) DrawingUtils::drawRect(winRenderer, &r, scaledBrush, winW, winH);
    }
    else if (type == ToolType::CIRCLE) DrawingUtils::drawOval(winRenderer, std::min(winStartX, winCurX) + scaledHalf, std::min(winStartY, winCurY) + scaledHalf,
                                                                std::max(winStartX, winCurX) - scaledHalf, std::max(winStartY, winCurY) - scaledHalf, scaledBrush, winW, winH);
}

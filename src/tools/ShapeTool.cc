#include "Tools.h"
#include "DrawingUtils.h"

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

    // For RECT and CIRCLE, geometry is inset by half so the stroke outer edge
    // lands exactly at the mouse coords — no extra padding needed.
    // For LINE, stroke is centered on the path so half padding is still required.
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
    // Clamp to canvas
    int right  = std::min(bounds.x + bounds.w, CANVAS_WIDTH);
    int bottom = std::min(bounds.y + bounds.h, CANVAS_HEIGHT);
    bounds.x = std::max(0, bounds.x);
    bounds.y = std::max(0, bounds.y);
    bounds.w = right  - bounds.x;
    bounds.h = bottom - bounds.y;
    if (bounds.w <= 0 || bounds.h <= 0) { isDrawing = false; return false; }

    // Create a transparent offscreen texture sized to the bounding rect
    SDL_Texture* buf = SDL_CreateTexture(canvasRenderer, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_TARGET, bounds.w, bounds.h);
    SDL_SetTextureBlendMode(buf, SDL_BLENDMODE_BLEND);

    // Render shape into buffer.
    // IMPORTANT: local offsets must be computed from the post-clamp bounds origin,
    // since that is what the texture is sized and positioned against.
    SDL_SetRenderTarget(canvasRenderer, buf);
    SDL_SetRenderDrawColor(canvasRenderer, 0, 0, 0, 0);
    SDL_RenderClear(canvasRenderer);

    int lsx = startX - bounds.x, lsy = startY - bounds.y;
    int lcx = cX     - bounds.x, lcy = cY     - bounds.y;

    SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
    if (type == ToolType::LINE)
        DrawingUtils::drawLine(canvasRenderer, lsx, lsy, lcx, lcy, brushSize);
    else if (type == ToolType::RECT) {
        SDL_Rect r = { std::min(lsx, lcx) + half, std::min(lsy, lcy) + half,
                        std::abs(lcx - lsx) - half * 2, std::abs(lcy - lsy) - half * 2 };
        if (r.w > 0 && r.h > 0) DrawingUtils::drawRect(canvasRenderer, &r, brushSize);
    }
    else if (type == ToolType::CIRCLE)
        DrawingUtils::drawOval(canvasRenderer, std::min(lsx, lcx) + half, std::min(lsy, lcy) + half,
                                std::max(lsx, lcx) - half, std::max(lsy, lcy) - half, brushSize);

    // Restore render target to canvas (kPen set it to canvas before calling us)
    SDL_SetRenderTarget(canvasRenderer, nullptr);

    isDrawing = false;

    // Hand texture off to kPen which will switch to SelectTool with it pre-loaded
    if (onShapeReady) onShapeReady(buf, bounds);

    // Canvas unchanged — return false so kPen doesn't push an undo state here
    return false;
}

void ShapeTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
    if (!isDrawing) return;
    int winStartX, winStartY, winCurX, winCurY;
    mapper->getWindowCoords(startX, startY, &winStartX, &winStartY);
    
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    int curX, curY;
    mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
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

#include "Tools.h"
#include "DrawingUtils.h"
#include <algorithm>

ResizeTool::ResizeTool(ICoordinateMapper* m, ToolType st, SDL_Rect bounds,
                       int sx, int sy, int ex, int ey, int bs, SDL_Color col)
    : TransformTool(m), shapeType(st), origBounds(bounds), shapeStartX(sx), shapeStartY(sy)
    , shapeEndX(ex), shapeEndY(ey), shapeBrushSize(bs), shapeColor(col)
{ currentBounds = bounds; }

ResizeTool::~ResizeTool() {}

void ResizeTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    handleMouseDown(cX, cY);
}

void ResizeTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    handleMouseMove(cX, cY);
}

bool ResizeTool::onMouseUp(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    handleMouseUp();
    return false; // committed via deactivate()
}

// ── Shape rendering ───────────────────────────────────────────────────────────

static void remapPoint(int ox, int oy, const SDL_Rect& orig, const SDL_Rect& curr, int& rx, int& ry) {
    float tx = orig.w > 0 ? (float)(ox - orig.x) / orig.w : 0.f;
    float ty = orig.h > 0 ? (float)(oy - orig.y) / orig.h : 0.f;
    rx = curr.x + (int)(tx * curr.w);
    ry = curr.y + (int)(ty * curr.h);
}

void ResizeTool::renderShape(SDL_Renderer* r, const SDL_Rect& b, int bs, SDL_Color col, int clipW, int clipH) const {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    int half = std::max(1, bs / 2);

    if (shapeType == ToolType::LINE) {
        int rx0, ry0, rx1, ry1;
        remapPoint(shapeStartX, shapeStartY, origBounds, b, rx0, ry0);
        remapPoint(shapeEndX,   shapeEndY,   origBounds, b, rx1, ry1);
        // Apply flips: mirror each point within the current bounds
        int bRight  = b.x + b.w;
        int bBottom = b.y + b.h;
        if (flipX) { rx0 = bRight - (rx0 - b.x); rx1 = bRight - (rx1 - b.x); }
        if (flipY) { ry0 = bBottom - (ry0 - b.y); ry1 = bBottom - (ry1 - b.y); }
        DrawingUtils::drawLine(r, rx0, ry0, rx1, ry1, bs, clipW, clipH);
    } else if (shapeType == ToolType::RECT) {
        SDL_Rect rect = { b.x + half, b.y + half, b.w - half * 2, b.h - half * 2 };
        if (rect.w > 0 && rect.h > 0) DrawingUtils::drawRect(r, &rect, bs, clipW, clipH);
    } else if (shapeType == ToolType::CIRCLE) {
        DrawingUtils::drawOval(r, b.x + half, b.y + half, b.x + b.w - half, b.y + b.h - half, bs, clipW, clipH);
    }
}

void ResizeTool::onOverlayRender(SDL_Renderer* r) {
    renderShape(r, currentBounds, shapeBrushSize, shapeColor, CANVAS_WIDTH, CANVAS_HEIGHT);
}

void ResizeTool::onPreviewRender(SDL_Renderer* r, int /*bs*/, SDL_Color /*col*/) {
    drawHandles(r);
}

void ResizeTool::deactivate(SDL_Renderer* r) {
    renderShape(r, currentBounds, shapeBrushSize, shapeColor, CANVAS_WIDTH, CANVAS_HEIGHT);
}

std::vector<uint32_t> ResizeTool::getFloatingPixels(SDL_Renderer* r) const {
    int w = currentBounds.w, h = currentBounds.h;
    if (w <= 0 || h <= 0) return {};

    // Render the shape into a temporary texture at its current bounds,
    // offset so the shape's top-left maps to (0,0) in the texture.
    SDL_Texture* tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tmp) return {};
    SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_BLEND);

    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tmp);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    // Shift the bounds so the shape renders at (0,0) within the texture
    SDL_SetRenderDrawColor(r, shapeColor.r, shapeColor.g, shapeColor.b, 255);
    int half = std::max(1, shapeBrushSize / 2);

    if (shapeType == ToolType::LINE) {
        // Remap endpoints from origBounds space into localBounds space
        auto remap = [&](int ox, int oy, int& rx, int& ry) {
            float tx = origBounds.w > 0 ? (float)(ox - origBounds.x) / origBounds.w : 0.f;
            float ty = origBounds.h > 0 ? (float)(oy - origBounds.y) / origBounds.h : 0.f;
            rx = (int)(tx * w);
            ry = (int)(ty * h);
        };
        int rx0, ry0, rx1, ry1;
        remap(shapeStartX, shapeStartY, rx0, ry0);
        remap(shapeEndX,   shapeEndY,   rx1, ry1);
        if (flipX) { rx0 = w - rx0; rx1 = w - rx1; }
        if (flipY) { ry0 = h - ry0; ry1 = h - ry1; }
        DrawingUtils::drawLine(r, rx0, ry0, rx1, ry1, shapeBrushSize, w, h);
    } else if (shapeType == ToolType::RECT) {
        SDL_Rect rect = { half, half, w - half * 2, h - half * 2 };
        if (rect.w > 0 && rect.h > 0)
            DrawingUtils::drawRect(r, &rect, shapeBrushSize, w, h);
    } else if (shapeType == ToolType::CIRCLE) {
        DrawingUtils::drawOval(r, half, half, w - half, h - half, shapeBrushSize, w, h);
    }

    std::vector<uint32_t> pixels(w * h);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);

    SDL_SetRenderTarget(r, prev);
    SDL_DestroyTexture(tmp);
    return pixels;
}

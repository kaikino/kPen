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

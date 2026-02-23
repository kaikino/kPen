#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>

ResizeTool::ResizeTool(ICoordinateMapper* m, ToolType shapeType, SDL_Rect bounds, 
                       int startX_, int startY_, int endX_, int endY_, int brushSize_, SDL_Color color_) :
    AbstractTool(m), shapeType(shapeType), origBounds(bounds), currentBounds(bounds),
    shapeStartX(startX_), shapeStartY(startY_), shapeEndX(endX_), shapeEndY(endY_),
    shapeBrushSize(brushSize_), shapeColor(color_), resizing(Handle::NONE), moved(false)
{}

ResizeTool::~ResizeTool() {}

ResizeTool::Handle ResizeTool::getHandle(int cX, int cY) const {
    const SDL_Rect& a = currentBounds;
    bool onL = std::abs(cX - a.x) <= GRAB;
    bool onR = std::abs(cX - (a.x + a.w)) <= GRAB;
    bool onT = std::abs(cY - a.y) <= GRAB;
    bool onB = std::abs(cY - (a.y + a.h)) <= GRAB;
    bool inX = cX >= a.x - GRAB && cX <= a.x + a.w + GRAB;
    bool inY = cY >= a.y - GRAB && cY <= a.y + a.h + GRAB;
    if (onL && onT) return Handle::NW;
    if (onR && onT) return Handle::NE;
    if (onL && onB) return Handle::SW;
    if (onR && onB) return Handle::SE;
    if (onT && inX) return Handle::N;
    if (onB && inX) return Handle::S;
    if (onL && inY) return Handle::W;
    if (onR && inY) return Handle::E;
    return Handle::NONE;
}

void ResizeTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    Handle h = getHandle(cX, cY);
    if (h != Handle::NONE) {
        resizing = h;
        anchorX = (h == Handle::W || h == Handle::NW || h == Handle::SW)
            ? currentBounds.x + currentBounds.w : currentBounds.x;
        anchorY = (h == Handle::N || h == Handle::NW || h == Handle::NE)
            ? currentBounds.y + currentBounds.h : currentBounds.y;
        return;
    }
    SDL_Point pt = {cX, cY};
    if (SDL_PointInRect(&pt, &currentBounds)) {
        isMoving = true;
        dragOffX = cX - currentBounds.x;
        dragOffY = cY - currentBounds.y;
    }
}

void ResizeTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (resizing != Handle::NONE) {
        moved = true;
        Handle h = resizing;
        int newX = currentBounds.x, newY = currentBounds.y;
        int newW = currentBounds.w, newH = currentBounds.h;
        if (h == Handle::W || h == Handle::NW || h == Handle::SW) {
            newX = std::min(cX, anchorX); newW = std::abs(anchorX - cX);
        }
        if (h == Handle::E || h == Handle::NE || h == Handle::SE) {
            newX = std::min(cX, anchorX); newW = std::abs(cX - anchorX);
        }
        if (h == Handle::N || h == Handle::NW || h == Handle::NE) {
            newY = std::min(cY, anchorY); newH = std::abs(anchorY - cY);
        }
        if (h == Handle::S || h == Handle::SW || h == Handle::SE) {
            newY = std::min(cY, anchorY); newH = std::abs(cY - anchorY);
        }
        if (newW > 0 && newH > 0)
            currentBounds = { newX, newY, newW, newH };
    } else if (isMoving) {
        moved = true;
        currentBounds.x = cX - dragOffX;
        currentBounds.y = cY - dragOffY;
    }
}

bool ResizeTool::onMouseUp(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    resizing  = Handle::NONE;
    isMoving  = false;
    return false; // commit happens in deactivate()
}

// ── Shape rendering helpers ───────────────────────────────────────────────────

// Map a point from the original bounds coordinate space into the current bounds.
static void remapPoint(int ox, int oy,
                       const SDL_Rect& orig, const SDL_Rect& curr,
                       int& rx, int& ry)
{
    float tx = (orig.w > 0) ? (float)(ox - orig.x) / orig.w : 0.f;
    float ty = (orig.h > 0) ? (float)(oy - orig.y) / orig.h : 0.f;
    rx = curr.x + (int)(tx * curr.w);
    ry = curr.y + (int)(ty * curr.h);
}

void ResizeTool::renderShape(SDL_Renderer* r, const SDL_Rect& bounds, int brushSz, SDL_Color col,
                              int clipW, int clipH) const
{
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);

    if (shapeType == ToolType::LINE) {
        int rx0, ry0, rx1, ry1;
        remapPoint(shapeStartX, shapeStartY, origBounds, bounds, rx0, ry0);
        remapPoint(shapeEndX,   shapeEndY,   origBounds, bounds, rx1, ry1);
        DrawingUtils::drawLine(r, rx0, ry0, rx1, ry1, brushSz, clipW, clipH);

    } else if (shapeType == ToolType::RECT) {
        int half = std::max(1, brushSz / 2);
        SDL_Rect rect = {
            bounds.x + half,
            bounds.y + half,
            bounds.w - half * 2,
            bounds.h - half * 2
        };
        if (rect.w > 0 && rect.h > 0)
            DrawingUtils::drawRect(r, &rect, brushSz, clipW, clipH);

    } else if (shapeType == ToolType::CIRCLE) {
        int half = std::max(1, brushSz / 2);
        DrawingUtils::drawOval(r,
            bounds.x + half, bounds.y + half,
            bounds.x + bounds.w - half,
            bounds.y + bounds.h - half,
            brushSz, clipW, clipH);
    }
}

// ── Overlay / preview rendering ───────────────────────────────────────────────

void ResizeTool::onOverlayRender(SDL_Renderer* overlayRenderer) {
    // Draw the live shape into the overlay (canvas coords, full canvas size)
    renderShape(overlayRenderer, currentBounds, shapeBrushSize, shapeColor,
                CANVAS_WIDTH, CANVAS_HEIGHT);
}

void ResizeTool::onPreviewRender(SDL_Renderer* winRenderer, int /*brushSize*/, SDL_Color /*color*/) {
    // Convert bounding box corners to window coords for the outline + handles
    int wx1, wy1, wx2, wy2;
    mapper->getWindowCoords(currentBounds.x,                  currentBounds.y,                  &wx1, &wy1);
    mapper->getWindowCoords(currentBounds.x + currentBounds.w, currentBounds.y + currentBounds.h, &wx2, &wy2);
    SDL_Rect outline = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
    DrawingUtils::drawMarchingRect(winRenderer, &outline);

    // 8 handle squares
    const int hs = 4;
    int wmx = (wx1 + wx2) / 2, wmy = (wy1 + wy2) / 2;
    SDL_Point handles[] = {
        {wx1, wy1}, {wmx, wy1}, {wx2, wy1},
        {wx1, wmy},              {wx2, wmy},
        {wx1, wy2}, {wmx, wy2}, {wx2, wy2}
    };
    for (auto& p : handles) {
        SDL_Rect sq = { p.x - hs, p.y - hs, hs * 2 + 1, hs * 2 + 1 };
        SDL_SetRenderDrawColor(winRenderer, 255, 255, 255, 255);
        SDL_RenderFillRect(winRenderer, &sq);
        SDL_SetRenderDrawColor(winRenderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(winRenderer, &sq);
    }
}

void ResizeTool::deactivate(SDL_Renderer* canvasRenderer) {
    // Stamp the final shape permanently onto the canvas
    renderShape(canvasRenderer, currentBounds, shapeBrushSize, shapeColor,
                CANVAS_WIDTH, CANVAS_HEIGHT);
}

bool ResizeTool::isHit(int cX, int cY) const {
    SDL_Point pt = {cX, cY};
    return SDL_PointInRect(&pt, &currentBounds) || getHandle(cX, cY) != Handle::NONE;
}

bool ResizeTool::hasOverlayContent() { return true; }

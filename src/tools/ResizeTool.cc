#include "Tools.h"
#include "DrawingUtils.h"
#include <algorithm>

ResizeTool::ResizeTool(ICoordinateMapper* m, ToolType st, SDL_Rect bounds, SDL_Rect ob,
                       int sx, int sy, int ex, int ey, int* liveBS, const SDL_Color* liveCol, bool filled)
    : TransformTool(m), shapeType(st), origBounds(ob), shapeStartX(sx), shapeStartY(sy)
    , shapeEndX(ex), shapeEndY(ey), liveBrushSize(liveBS), liveColor(liveCol), shapeFilled(filled)
{ currentBounds = bounds; }

ResizeTool::~ResizeTool() {}

void ResizeTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) { handleMouseDown(cX, cY); }
void ResizeTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) { handleMouseMove(cX, cY); }
bool ResizeTool::onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) { handleMouseUp(); return false; }

// Identical drawing logic to drawShapeCanvasSpace in ShapeTool.cc.
// origBounds = {min(sx,ex), min(sy,ey), abs(dx), abs(dy)} — no brush expansion.
// rx0/rx1 remap startX/endX proportionally into b; exact when b==origBounds.

void ResizeTool::renderShape(SDL_Renderer* r, const SDL_Rect& b, int bs, SDL_Color col, int clipW, int clipH) const {
    if (col.a == 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    } else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    int li = (bs - 1) / 2;  // left/top inset  (asymmetric for even brushes)
    int ri = bs / 2;         // right/bottom inset

    float tx0 = origBounds.w > 0 ? (float)(shapeStartX - origBounds.x) / origBounds.w : 0.f;
    float ty0 = origBounds.h > 0 ? (float)(shapeStartY - origBounds.y) / origBounds.h : 0.f;
    float tx1 = origBounds.w > 0 ? (float)(shapeEndX   - origBounds.x) / origBounds.w : 1.f;
    float ty1 = origBounds.h > 0 ? (float)(shapeEndY   - origBounds.y) / origBounds.h : 1.f;

    // Map start/end proportionally into b using b.w-1 so both rx0 and rx1 are
    // inclusive pixel coordinates within [b.x, b.x+b.w-1]. This keeps the flip
    // mirror axis clean and avoids off-by-one throughout.
    int rx0 = b.x + (b.w > 1 ? (int)std::round(tx0 * (b.w - 1)) : 0);
    int ry0 = b.y + (b.h > 1 ? (int)std::round(ty0 * (b.h - 1)) : 0);
    int rx1 = b.x + (b.w > 1 ? (int)std::round(tx1 * (b.w - 1)) : 0);
    int ry1 = b.y + (b.h > 1 ? (int)std::round(ty1 * (b.h - 1)) : 0);

    if (flipX) { int m = b.x + b.w - 1; rx0 = m - (rx0 - b.x); rx1 = m - (rx1 - b.x); }
    if (flipY) { int m = b.y + b.h - 1; ry0 = m - (ry0 - b.y); ry1 = m - (ry1 - b.y); }

    int minX = std::min(rx0, rx1), minY = std::min(ry0, ry1);
    int maxX = std::max(rx0, rx1), maxY = std::max(ry0, ry1);
    int cx0 = minX + li, cy0 = minY + li;
    int cx1 = maxX - ri, cy1 = maxY - ri;

    if (shapeType == ToolType::LINE) {
        // rx0/rx1 are brush outer edges (left when unflipped, right when flipped).
        // Recover center pixels then pass directly to drawLine.
        int lx0 = rx0 + (rx0 <= rx1 ? li : -ri);
        int ly0 = ry0 + (ry0 <= ry1 ? li : -ri);
        int lx1 = rx1 + (rx1 <= rx0 ? li : -ri);
        int ly1 = ry1 + (ry1 <= ry0 ? li : -ri);
        DrawingUtils::drawLine(r, lx0, ly0, lx1, ly1, bs, clipW, clipH);
    } else if (shapeType == ToolType::RECT) {
        if (shapeFilled) {
            SDL_Rect rect = { b.x, b.y, b.w, b.h };
            DrawingUtils::drawFilledRect(r, &rect, clipW, clipH);
        } else {
            SDL_Rect rect = { cx0, cy0, cx1 - cx0, cy1 - cy0 };
            if (rect.w >= 0 && rect.h >= 0) DrawingUtils::drawRect(r, &rect, bs, clipW, clipH);
        }
    } else if (shapeType == ToolType::CIRCLE) {
        if (shapeFilled) {
            DrawingUtils::drawFilledOval(r, b.x, b.y, b.x + b.w - 1, b.y + b.h - 1, clipW, clipH);
        } else {
            if (cx1 >= cx0 && cy1 >= cy0)
                DrawingUtils::drawOval(r, cx0, cy0, cx1, cy1, bs, clipW, clipH);
        }
    }
}

void ResizeTool::onOverlayRender(SDL_Renderer* r) {
    int cw, ch; mapper->getCanvasSize(&cw, &ch);
    SDL_Color drawColor = *liveColor;
    if (drawColor.a == 0) {
        drawColor = { 100, 149, 237, 128 }; // CornflowerBlue with 50% alpha
    }
    renderShape(r, currentBounds, *liveBrushSize, drawColor, cw, ch);
}

void ResizeTool::onPreviewRender(SDL_Renderer* r, int, SDL_Color) {
    drawHandles(r);
}

void ResizeTool::deactivate(SDL_Renderer* r) {
    int cw, ch; mapper->getCanvasSize(&cw, &ch);
    renderShape(r, currentBounds, *liveBrushSize, *liveColor, cw, ch);
}

std::vector<uint32_t> ResizeTool::getFloatingPixels(SDL_Renderer* r) const {
    int w = currentBounds.w, h = currentBounds.h;
    if (w <= 0 || h <= 0) return {};
    SDL_Texture* tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tmp) return {};
    SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tmp);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    renderShape(r, {0, 0, w, h}, *liveBrushSize, *liveColor, w, h);
    std::vector<uint32_t> pixels(w * h);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);
    SDL_SetRenderTarget(r, prev);
    SDL_DestroyTexture(tmp);
    return pixels;
}

bool ResizeTool::willRender() const {
    const SDL_Rect& b = currentBounds;
    if (b.w <= 0 || b.h <= 0) return false;
    if (shapeType == ToolType::LINE) return true;  // line always renders if bounds valid
    int bs = *liveBrushSize;
    if (shapeFilled) {
        // filled shapes render as long as bounds are non-empty
        return b.w >= 1 && b.h >= 1;
    } else {
        // stroked shapes need room for the brush insets on both sides
        int li = (bs - 1) / 2, ri = bs / 2;
        // Using b directly as the rect passed to renderShape: cx0=li, cx1=b.w-1-ri
        int cx1w = b.w - 1 - ri - li;  // cx1 - cx0 = (b.w-1-ri) - li
        int cy1h = b.h - 1 - ri - li;
        return cx1w >= 0 && cy1h >= 0;
    }
}

#include "Tools.h"
#include "DrawingUtils.h"
#include <algorithm>
#include <cmath>

ResizeTool::ResizeTool(ICoordinateMapper* m, ToolType st, SDL_Rect bounds, SDL_Rect ob,
                       int sx, int sy, int ex, int ey, int* liveBS, const SDL_Color* liveCol, bool filled)
    : TransformTool(m), shapeType(st), origBounds(ob), shapeStartX(sx), shapeStartY(sy)
    , shapeEndX(ex), shapeEndY(ey), liveBrushSize(liveBS), liveColor(liveCol), shapeFilled(filled)
{
    // ShapeTool now computes the tight pixel-footprint bounding box for all
    // shape types (including both filled and unfilled circles) and passes it
    // in as |bounds|.  No post-processing is needed here.
    currentBounds = bounds;
    syncDrawCenterFromBounds();
}

ResizeTool::~ResizeTool() {}

void ResizeTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) { handleMouseDown(cX, cY); }
void ResizeTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) { handleMouseMove(cX, cY); }
bool ResizeTool::onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) { handleMouseUp(); return false; }

// Render the shape into renderer r with bounds b (un-rotated, local space).
// clipW/clipH are the canvas dimensions for clipping.
void ResizeTool::renderShape(SDL_Renderer* r, const SDL_Rect& b, int bs, SDL_Color col, int clipW, int clipH) const {
    if (col.a == 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    } else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    int li = (bs - 1) / 2;
    int ri = bs / 2;

    float tx0 = origBounds.w > 0 ? (float)(shapeStartX - origBounds.x) / origBounds.w : 0.f;
    float ty0 = origBounds.h > 0 ? (float)(shapeStartY - origBounds.y) / origBounds.h : 0.f;
    float tx1 = origBounds.w > 0 ? (float)(shapeEndX   - origBounds.x) / origBounds.w : 1.f;
    float ty1 = origBounds.h > 0 ? (float)(shapeEndY   - origBounds.y) / origBounds.h : 1.f;

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
            // b is the tight pixel bounding box {x, y, w, h} of the oval.
            // drawFilledOval takes (x0,y0,x1,y1) inclusive endpoints of the draw extent.
            if (b.w >= 1 && b.h >= 1)
                DrawingUtils::drawFilledOval(r, b.x, b.y, b.x + b.w - 1, b.y + b.h - 1, clipW, clipH);
        } else {
            // b is the tight handle-space bounding box {x, y, w, h} of the oval stroke.
            // Inset by li/ri to recover the oval center-point draw coords cx0..cx1.
            int ocx0 = b.x + li, ocy0 = b.y + li;
            int ocx1 = b.x + b.w - 1 - ri, ocy1 = b.y + b.h - 1 - ri;
            if (ocx1 >= ocx0 && ocy1 >= ocy0)
                DrawingUtils::drawOval(r, ocx0, ocy0, ocx1, ocy1, bs, clipW, clipH);
        }
    }
}

// Draw the shape at (x,y) with size (w,h) and rotation. Position (x,y) is the same
// as the bounding box (drawCenter - halfSize) so shape and box always match.
void ResizeTool::renderShapeAt(SDL_Renderer* r, float x, float y, int w, int h, float rotationRad,
                              SDL_Color col, int clipW, int clipH) const {
    if (w <= 0 || h <= 0) return;

    if (rotationRad == 0.f) {
        renderShape(r, { (int)std::round(x), (int)std::round(y), w, h }, *liveBrushSize, col, clipW, clipH);
        return;
    }

    SDL_Texture* tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tmp) return;
    SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tmp);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderClear(r);
    renderShape(r, { 0, 0, w, h }, *liveBrushSize, col, w, h);
    SDL_SetRenderTarget(r, prev);

    double angleDeg = std::fmod(rotationRad * 180.0 / M_PI, 360.0);
    if (angleDeg < 0.0) angleDeg += 360.0;
    float hw = w * 0.5f, hh = h * 0.5f;
    bool parityDiff = (w & 1) != (h & 1);
    bool is90or270 = (std::fabs(angleDeg - 90.0) < 1.0 || std::fabs(angleDeg - 270.0) < 1.0);
    float pivotX = hw, pivotY = hh;
    if (is90or270 && parityDiff) {
        pivotX = (float)std::round(hw);
        pivotY = (float)std::round(hh);
    }
    SDL_FRect dstF = { x, y, (float)w, (float)h };
    SDL_FPoint centerF = { pivotX, pivotY };
    SDL_RenderCopyExF(r, tmp, nullptr, &dstF, angleDeg, &centerF, SDL_FLIP_NONE);
    SDL_DestroyTexture(tmp);
}

void ResizeTool::onOverlayRender(SDL_Renderer* r) {
    int cw, ch; mapper->getCanvasSize(&cw, &ch);
    SDL_Color drawColor = *liveColor;
    if (drawColor.a == 0) {
        drawColor = { 100, 149, 237, 128 };
    }
    const SDL_Rect b = currentBounds;
    float hw = b.w * 0.5f, hh = b.h * 0.5f;
    float drawX = getDrawCenterX() - hw, drawY = getDrawCenterY() - hh;
    float rot = getRotation();
    double rotDeg = std::fmod(rot * 180.0 / M_PI, 360.0);
    if (rotDeg < 0.0) rotDeg += 360.0;
    bool parityDiff = (b.w & 1) != (b.h & 1);
    bool is90or270 = (std::fabs(rotDeg - 90.0) < 1.0 || std::fabs(rotDeg - 270.0) < 1.0);
    if (is90or270 && parityDiff) {
        drawX = (float)std::round(drawX);
        drawY = (float)std::round(drawY);
    }
    renderShapeAt(r, drawX, drawY, b.w, b.h, rot, drawColor, cw, ch);
}

void ResizeTool::onPreviewRender(SDL_Renderer* r, int bs, SDL_Color) {
    // currentBounds semantics by shape type:
    //   LINE / RECT (filled or not): tight pixel-footprint == raw draw extent.
    //   CIRCLE filled:               currentBounds = raw draw extent {minX,minY,dw,dh};
    //                                pixel footprint equals the draw extent for fill.
    //   CIRCLE unfilled:             currentBounds = tight handle-space box derived
    //                                from getOvalCenterBounds + brush expansion, so
    //                                handles land exactly on the outermost stroke pixels.
    // In all cases drawHandles uses currentBounds directly — no special-casing needed.
    drawHandles(r);
}

void ResizeTool::deactivate(SDL_Renderer* r) {
    int cw, ch; mapper->getCanvasSize(&cw, &ch);
    const SDL_Rect b = currentBounds;
    float hw = b.w * 0.5f, hh = b.h * 0.5f;
    float drawX = getDrawCenterX() - hw, drawY = getDrawCenterY() - hh;
    renderShapeAt(r, drawX, drawY, b.w, b.h, getRotation(), *liveColor, cw, ch);
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
    // Render un-rotated into local space for pixel export
    renderShape(r, {0, 0, w, h}, *liveBrushSize, *liveColor, w, h);
    std::vector<uint32_t> pixels(w * h);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);
    SDL_SetRenderTarget(r, prev);
    SDL_DestroyTexture(tmp);
    return pixels;
}

void ResizeTool::snapBounds(int& newX, int& newY, int& newW, int& newH) {
    // Only unfilled circles need snapping — the Bresenham oval algorithm uses
    // integer-truncated center cx=(left+right)/2, so for odd center-point spans
    // (i.e. (newW - bs) is odd) the algorithm only reaches right-1 on the right
    // edge, leaving a 1-pixel gap between the stroke and the handle box.
    // Snap by expanding 1px toward the dragged handle (never shrink toward it).
    if (shapeType != ToolType::CIRCLE || shapeFilled) return;

    int bs = *liveBrushSize;

    // Snap width: center span = newW - bs; must be even.
    if ((newW - bs) % 2 != 0) {
        // Expand toward the dragged handle edge.
        bool dragRight = (resizing == Handle::E  || resizing == Handle::NE ||
                          resizing == Handle::SE);
        bool dragLeft  = (resizing == Handle::W  || resizing == Handle::NW ||
                          resizing == Handle::SW);
        if (dragRight) {
            newW++;               // right edge moves right by 1
        } else if (dragLeft) {
            newX--;  newW++;      // left edge moves left by 1 (anchor stays)
        } else {
            newW++;               // N/S drag: expand right arbitrarily
        }
    }

    // Snap height: center span = newH - bs; must be even.
    if ((newH - bs) % 2 != 0) {
        bool dragBottom = (resizing == Handle::S  || resizing == Handle::SE ||
                           resizing == Handle::SW);
        bool dragTop    = (resizing == Handle::N  || resizing == Handle::NE ||
                           resizing == Handle::NW);
        if (dragBottom) {
            newH++;
        } else if (dragTop) {
            newY--;  newH++;
        } else {
            newH++;               // E/W drag: expand downward arbitrarily
        }
    }
}

bool ResizeTool::willRender() const {
    const SDL_Rect& b = currentBounds;
    if (b.w <= 0 || b.h <= 0) return false;
    if (shapeType == ToolType::LINE) return true;
    int bs = *liveBrushSize;
    if (shapeFilled) {
        return b.w >= 1 && b.h >= 1;
    } else {
        // currentBounds is tight pixel-footprint; its size includes bs on each axis.
        // Need at least 1 center-point pixel, i.e. b.w >= bs and b.h >= bs.
        return b.w >= bs && b.h >= bs;
    }
}

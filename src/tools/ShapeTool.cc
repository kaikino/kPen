#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>

ShapeTool::ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb, bool fill)
    : AbstractTool(m), type(t), onShapeReady(std::move(cb)), filled(fill) {}

bool ShapeTool::onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (!isDrawing) return false;
    if (cX == startX && cY == startY) { isDrawing = false; return false; }

    int li = (brushSize - 1) / 2;
    int ri = brushSize / 2;

    SDL_Rect bounds, origBounds;
    int sx = startX, sy = startY, ex = cX, ey = cY;  // coords passed to ResizeTool
    if (type == ToolType::LINE) {
        // Convert to inclusive center endpoints (same as drawShapeCanvasSpace).
        int isx = sx < ex ? sx : sx > ex ? sx - 1 : sx;
        int isy = sy < ey ? sy : sy > ey ? sy - 1 : sy;
        int iex = ex > sx ? ex - 1 : ex < sx ? ex : ex;
        int iey = ey > sy ? ey - 1 : ey < sy ? ey : ey;
        // origBounds maps isx→tx=0, iex→tx=1 (w = iex-isx, clamped to 1 to
        // avoid division by zero for single-point lines where isx==iex).
        // bounds (currentBounds) = brush footprint: expands by li left and ri right,
        // so w = (iex-isx) + li + ri + 1 = (iex-isx) + brushSize.
        int spanX = std::abs(iex - isx), spanY = std::abs(iey - isy);
        origBounds = {
            std::min(isx, iex),
            std::min(isy, iey),
            std::max(1, spanX),
            std::max(1, spanY)
        };
        bounds = {
            origBounds.x - li,
            origBounds.y - li,
            spanX + brushSize,
            spanY + brushSize
        };
        sx = isx; sy = isy; ex = iex; ey = iey;
    } else {
        // Rect/circle: skip shapes too small to draw.
        int dw = std::abs(cX - startX);
        int dh = std::abs(cY - startY);
        if (!filled && (dw < brushSize || dh < brushSize)) { isDrawing = false; return false; }
        int minX = std::min(startX, cX), minY = std::min(startY, cY);
        if (type == ToolType::CIRCLE) {
            if (filled) {
                SDL_Rect cb = DrawingUtils::getOvalCenterBounds(minX, minY, minX + dw - 1, minY + dh - 1);
                if (cb.w == 0 && cb.h == 0) { isDrawing = false; return false; }
                bounds = origBounds = { cb.x, cb.y, cb.w + 1, cb.h + 1 };
            } else {
                int cx0 = minX + li, cy0 = minY + li;
                int cx1 = minX + dw - 1 - ri, cy1 = minY + dh - 1 - ri;
                if (cx1 < cx0 || cy1 < cy0) { isDrawing = false; return false; }
                SDL_Rect cb = DrawingUtils::getOvalCenterBounds(cx0, cy0, cx1, cy1);
                if (cb.w == 0 && cb.h == 0) { isDrawing = false; return false; }
                bounds = origBounds = {
                    cb.x - li, cb.y - li,
                    cb.w + brushSize, cb.h + brushSize
                };
            }
        } else {
            bounds = origBounds = { minX, minY, dw, dh };
        }
    }
    isDrawing = false;

    if (onShapeReady)
        onShapeReady(type, bounds, origBounds, sx, sy, ex, ey, brushSize, color, filled);

    return false;
}

// Draw shape in canvas space. Used by both onOverlayRender (live preview) and
// ResizeTool::renderShape (committed), guaranteeing pixel-identical output.
//
// addBrush is asymmetric for even sizes: extends (bs/2-1) left, bs/2 right of stamp.
// So the correct insets are:
//   left  inset = (bs-1)/2   [= bs/2-1 for even, bs/2 for odd]
//   right inset = bs/2       [= bs/2   for both]
// This makes the outer stroke edge touch minX and maxX exactly.

static void drawShapeCanvasSpace(SDL_Renderer* r, ToolType type,
                                  int startX, int startY, int endX, int endY,
                                  int bs, int clipW, int clipH, bool filled = false) {
    int li = (bs - 1) / 2;  // left/top inset
    int ri = bs / 2;         // right/bottom inset
    int minX = std::min(startX, endX), minY = std::min(startY, endY);
    // max(startX,endX) is the exclusive right boundary; -1 gives inclusive last pixel
    // used by rect/circle and also as the base for line (converted to inclusive below)
    int maxX = std::max(startX, endX) - 1, maxY = std::max(startY, endY) - 1;
    int cx0 = minX + li, cy0 = minY + li;
    int cx1 = maxX - ri, cy1 = maxY - ri;

    if (type == ToolType::LINE) {
        // Convert exclusive boundaries to inclusive center pixels, then pass
        // directly to drawLine (no inset — drawLine stamps the brush at each center).
        int iStartX = startX < endX ? startX : startX > endX ? startX - 1 : startX;
        int iStartY = startY < endY ? startY : startY > endY ? startY - 1 : startY;
        int iEndX   = endX > startX ? endX - 1 : endX < startX ? endX : endX;
        int iEndY   = endY > startY ? endY - 1 : endY < startY ? endY : endY;
        DrawingUtils::drawLine(r, iStartX, iStartY, iEndX, iEndY, bs, clipW, clipH);
    } else if (type == ToolType::RECT) {
        if (filled) {
            SDL_Rect rect = { minX, minY, maxX - minX + 1, maxY - minY + 1 };
            if (rect.w > 0 && rect.h > 0) DrawingUtils::drawFilledRect(r, &rect, clipW, clipH);
        } else {
            SDL_Rect rect = { cx0, cy0, cx1 - cx0, cy1 - cy0 };
            if (rect.w >= 0 && rect.h >= 0) DrawingUtils::drawRect(r, &rect, bs, clipW, clipH);
        }
    } else if (type == ToolType::CIRCLE) {
        if (filled) {
            if (maxX >= minX && maxY >= minY)
                DrawingUtils::drawFilledOval(r, minX, minY, maxX, maxY, clipW, clipH);
        } else {
            if (cx1 >= cx0 && cy1 >= cy0)
                DrawingUtils::drawOval(r, cx0, cy0, cx1, cy1, bs, clipW, clipH);
        }
    }
}

void ShapeTool::onOverlayRender(SDL_Renderer* r) {
    if (!isDrawing) return;
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    int curX, curY;
    mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
    if (curX == startX && curY == startY) return;

    int cw, ch; mapper->getCanvasSize(&cw, &ch);
    SDL_SetRenderDrawColor(r, cachedColor.r, cachedColor.g, cachedColor.b, 255);
    drawShapeCanvasSpace(r, type, startX, startY, curX, curY, cachedBrushSize, cw, ch, filled);
}

void ShapeTool::onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) {
    cachedBrushSize = brushSize;
    cachedColor     = color;

    if (!isDrawing) return;
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    int curX, curY;
    mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
    if (curX == startX && curY == startY) return;

    if (type == ToolType::LINE) return;  // line shows the stroke itself, no bounding box while drawing

    int li = (brushSize - 1) / 2;
    int ri = brushSize / 2;
    int minX = std::min(startX, curX), minY = std::min(startY, curY);
    int dw = std::abs(curX - startX),  dh = std::abs(curY - startY);

    int bx, by, bx2, by2;
    if (type == ToolType::CIRCLE && !filled) {
        int cx0 = minX + li, cy0 = minY + li;
        int cx1 = minX + dw - 1 - ri, cy1 = minY + dh - 1 - ri;
        if (cx1 < cx0 || cy1 < cy0) return;
        SDL_Rect cb = DrawingUtils::getOvalCenterBounds(cx0, cy0, cx1, cy1);
        bx = cb.x - li; by = cb.y - li;
        bx2 = cb.x + cb.w + ri + 1; by2 = cb.y + cb.h + ri + 1;
    } else if (type == ToolType::CIRCLE && filled) {
        SDL_Rect cb = DrawingUtils::getOvalCenterBounds(minX, minY, minX + dw - 1, minY + dh - 1);
        if (cb.w == 0 && cb.h == 0) return;
        bx = cb.x; by = cb.y;
        bx2 = cb.x + cb.w + 1; by2 = cb.y + cb.h + 1;
    } else {
        bx = minX; by = minY; bx2 = minX + dw; by2 = minY + dh;
    }

    int wx0, wy0, wx1, wy1;
    mapper->getWindowCoords(bx,  by,  &wx0, &wy0);
    mapper->getWindowCoords(bx2, by2, &wx1, &wy1);
    SDL_Rect outline = { wx0, wy0, wx1 - wx0, wy1 - wy0 };
    DrawingUtils::drawMarchingRect(r, &outline);
}

#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>

ShapeTool::ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb, bool fill,
                     std::function<void()> onLineCommitted)
    : AbstractTool(m), type(t), onShapeReady(std::move(cb)), onLineCommitted(std::move(onLineCommitted)), filled(fill) {}

bool ShapeTool::isOverLineHandle(int cX, int cY) const {
    if (!lineEditMode || type != ToolType::LINE) return false;
    int h = lineHitTest(cX, cY);
    return h == 0 || h == 1;
}

bool ShapeTool::isOverLineBody(int cX, int cY) const {
    if (!lineEditMode || type != ToolType::LINE) return false;
    return lineHitTest(cX, cY) == 2;
}

void ShapeTool::getLineEndpoints(int& x0, int& y0, int& x1, int& y1) const {
    x0 = lineStartX;
    y0 = lineStartY;
    x1 = lineEndX;
    y1 = lineEndY;
}

// Handle/segment hit uses window-space distance so grab area matches fixed-size drawn handles
static const int LINE_HANDLE_RADIUS_WIN = 3;

// Returns -1 none, 0 start handle, 1 end handle, 2 line segment (for commit = click outside)
int ShapeTool::lineHitTest(int cX, int cY) const {
    int wx, wy, w0x, w0y, w1x, w1y;
    mapper->getWindowCoords(cX, cY, &wx, &wy);
    mapper->getWindowCoords(lineStartX, lineStartY, &w0x, &w0y);
    mapper->getWindowCoords(lineEndX, lineEndY, &w1x, &w1y);
    const int R = LINE_HANDLE_RADIUS_WIN;
    int d0 = (wx - w0x) * (wx - w0x) + (wy - w0y) * (wy - w0y);
    if (d0 <= R * R) return 0;
    int d1 = (wx - w1x) * (wx - w1x) + (wy - w1y) * (wy - w1y);
    if (d1 <= R * R) return 1;
    int abx = w1x - w0x, aby = w1y - w0y;
    int apx = wx - w0x, apy = wy - w0y;
    int abSq = abx * abx + aby * aby;
    if (abSq == 0) return -1;
    int t = apx * abx + apy * aby;
    int distSq;
    if (t <= 0) distSq = d0;
    else if (t >= abSq) distSq = d1;
    else {
        int qx = w0x + (t * abx) / abSq;
        int qy = w0y + (t * aby) / abSq;
        distSq = (wx - qx) * (wx - qx) + (wy - qy) * (wy - qy);
    }
    if (distSq <= R * R) return 2;
    return -1;
}

void ShapeTool::commitLine(SDL_Renderer* r) {
    if (!lineEditMode || type != ToolType::LINE) return;
    int cw, ch;
    mapper->getCanvasSize(&cw, &ch);
    SDL_Color col = cachedColor;
    if (col.a == 0) col = { 0, 0, 0, 255 };
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    int iStartX = lineStartX, iStartY = lineStartY, iEndX = lineEndX, iEndY = lineEndY;
    if (iStartX > iEndX) std::swap(iStartX, iEndX);
    if (iStartY > iEndY) std::swap(iStartY, iEndY);
    if (iStartX == iEndX && iStartY == iEndY) {
        DrawingUtils::drawLine(r, lineStartX, lineStartY, lineStartX, lineStartY, cachedBrushSize, cw, ch);
    } else {
        DrawingUtils::drawLine(r, lineStartX, lineStartY, lineEndX, lineEndY, cachedBrushSize, cw, ch);
    }
    lineEditMode = false;
    draggingLineHandle = -1;
    if (onLineCommitted) onLineCommitted();
}

static void applyShiftConstraint(ToolType type, int startX, int startY,
                                  int& curX, int& curY) {
    int dx = curX - startX;
    int dy = curY - startY;
    if (dx == 0 && dy == 0) return;

    if (type == ToolType::LINE) {
        int adx = std::abs(dx), ady = std::abs(dy);
        if (adx > ady * 2) {
            curY = startY;
        } else if (ady > adx * 2) {
            curX = startX;
        } else {
            int d = std::min(adx, ady);
            curX = startX + (dx >= 0 ? d : -d);
            curY = startY + (dy >= 0 ? d : -d);
        }
    } else {
        int adx = std::abs(dx), ady = std::abs(dy);
        int d = std::min(adx, ady);
        curX = startX + (dx >= 0 ? d : -d);
        curY = startY + (dy >= 0 ? d : -d);
    }
}

void ShapeTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (type == ToolType::LINE && lineEditMode) {
        int hit = lineHitTest(cX, cY);
        if (hit == 0) { draggingLineHandle = 0; return; }
        if (hit == 1) { draggingLineHandle = 1; return; }
        if (hit == 2) {
            draggingLineHandle = 2;
            lineStartX0 = lineStartX;
            lineStartY0 = lineStartY;
            lineEndX0 = lineEndX;
            lineEndY0 = lineEndY;
            lineDragStartCX = cX;
            lineDragStartCY = cY;
            return;
        }
        commitLine(r);
        return;
    }
    AbstractTool::onMouseDown(cX, cY, r, brushSize, color);
}

void ShapeTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (type == ToolType::LINE && lineEditMode && draggingLineHandle >= 0) {
        if (draggingLineHandle == 0) {
            lineStartX = cX;
            lineStartY = cY;
        } else if (draggingLineHandle == 1) {
            lineEndX = cX;
            lineEndY = cY;
        } else if (draggingLineHandle == 2) {
            int dx = cX - lineDragStartCX;
            int dy = cY - lineDragStartCY;
            lineStartX = lineStartX0 + dx;
            lineStartY = lineStartY0 + dy;
            lineEndX = lineEndX0 + dx;
            lineEndY = lineEndY0 + dy;
        }
        return;
    }
    AbstractTool::onMouseMove(cX, cY, r, brushSize, color);
}

bool ShapeTool::onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (type == ToolType::LINE && lineEditMode) {
        draggingLineHandle = -1;
        return false;
    }
    if (!isDrawing) return false;
    if (cX == startX && cY == startY) { isDrawing = false; return false; }

    if (SDL_GetModState() & KMOD_SHIFT)
        applyShiftConstraint(type, startX, startY, cX, cY);

    if (cX == startX && cY == startY) { isDrawing = false; return false; }

    int li = (brushSize - 1) / 2;
    int ri = brushSize / 2;

    SDL_Rect bounds, origBounds;
    int sx = startX, sy = startY, ex = cX, ey = cY;
    if (type == ToolType::LINE) {
        int isx = sx < ex ? sx : sx > ex ? sx - 1 : sx;
        int isy = sy < ey ? sy : sy > ey ? sy - 1 : sy;
        int iex = ex > sx ? ex - 1 : ex < sx ? ex : ex;
        int iey = ey > sy ? ey - 1 : ey < sy ? ey : ey;
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
        int dw = std::abs(cX - startX);
        int dh = std::abs(cY - startY);
        if (!filled && (dw < brushSize || dh < brushSize)) { isDrawing = false; return false; }
        int minX = std::min(startX, cX), minY = std::min(startY, cY);
        if (type == ToolType::CIRCLE) {
            if (filled) {
                if (dw < 1 || dh < 1) { isDrawing = false; return false; }
                bounds = origBounds = { minX, minY, dw, dh };
            } else {
                int cx0 = minX + li, cy0 = minY + li;
                int cx1 = minX + dw - 1 - ri, cy1 = minY + dh - 1 - ri;
                if (cx1 < cx0 || cy1 < cy0) { isDrawing = false; return false; }
                SDL_Rect cb = DrawingUtils::getOvalCenterBounds(cx0, cy0, cx1, cy1);
                if (cb.w == 0 && cb.h == 0) { isDrawing = false; return false; }
                origBounds = { cx0, cy0, cx1 - cx0 + 1, cy1 - cy0 + 1 };
                bounds = { cb.x - li, cb.y - li,
                           cb.w + 1 + li + ri,
                           cb.h + 1 + li + ri };
            }
        } else {
            bounds = origBounds = { minX, minY, dw, dh };
        }
    }
    isDrawing = false;

    if (type == ToolType::LINE) {
        lineEditMode = true;
        lineStartX = sx;
        lineStartY = sy;
        lineEndX = ex;
        lineEndY = ey;
        cachedBrushSize = brushSize;
        cachedColor = color;
        return false;  // don't save state until line is committed
    }

    if (onShapeReady)
        onShapeReady(type, bounds, origBounds, sx, sy, ex, ey, brushSize, color, filled);

    return false;
}

static void drawShapeCanvasSpace(SDL_Renderer* r, ToolType type,
                                  int startX, int startY, int endX, int endY,
                                  int bs, int clipW, int clipH, bool filled = false) {
    int li = (bs - 1) / 2;
    int ri = bs / 2;
    int minX = std::min(startX, endX), minY = std::min(startY, endY);
    int maxX = std::max(startX, endX) - 1, maxY = std::max(startY, endY) - 1;
    int cx0 = minX + li, cy0 = minY + li;
    int cx1 = maxX - ri, cy1 = maxY - ri;

    if (type == ToolType::LINE) {
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
    if (type == ToolType::LINE && lineEditMode) {
        // Overlay is canvas-sized; draw in canvas coordinates so line stays fixed when panning/zooming
        int cw, ch;
        mapper->getCanvasSize(&cw, &ch);
        SDL_Color drawColor = cachedColor;
        if (drawColor.a == 0) drawColor = { 100, 149, 237, 200 };
        SDL_SetRenderDrawColor(r, drawColor.r, drawColor.g, drawColor.b, drawColor.a);
        DrawingUtils::drawLine(r, lineStartX, lineStartY, lineEndX, lineEndY, cachedBrushSize, cw, ch);
        // Handles are drawn in window space in kPen::renderFrame so they stay fixed size when zooming
        return;
    }
    if (!isDrawing) return;
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    int curX, curY;
    mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
    if (curX == startX && curY == startY) return;

    if (SDL_GetModState() & KMOD_SHIFT)
        applyShiftConstraint(type, startX, startY, curX, curY);

    int cw, ch; mapper->getCanvasSize(&cw, &ch);
    SDL_Color drawColor = cachedColor;
    if (drawColor.a == 0) {
        drawColor = { 100, 149, 237, 128 }; // CornflowerBlue with 50% alpha
    }

    SDL_SetRenderDrawColor(r, drawColor.r, drawColor.g, drawColor.b, drawColor.a);
    drawShapeCanvasSpace(r, type, startX, startY, curX, curY, cachedBrushSize, cw, ch, filled);
}

void ShapeTool::deactivate(SDL_Renderer* r) {
    if (type == ToolType::LINE && lineEditMode)
        commitLine(r);
}

void ShapeTool::onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) {
    cachedBrushSize = brushSize;
    cachedColor     = color;

    if (type == ToolType::LINE && lineEditMode) return;
    if (!isDrawing) return;
    int mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);
    int curX, curY;
    mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
    if (curX == startX && curY == startY) return;

    if (SDL_GetModState() & KMOD_SHIFT)
        applyShiftConstraint(type, startX, startY, curX, curY);

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

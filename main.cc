#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <memory>
#include <functional>

// Forward declarations and constants
const int CANVAS_WIDTH = 1000;
const int CANVAS_HEIGHT = 700;

enum class ToolType { BRUSH, LINE, RECT, CIRCLE, SELECT };

// Shared Drawing Helpers
namespace DrawingUtils {
    void drawFillCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius) {
        if (radius <= 0) {
            SDL_RenderDrawPoint(renderer, centerX, centerY);
            return;
        }
        for (int h = -radius; h <= radius; h++) {
            int half = (int)std::sqrt((float)(radius * radius - h * h));
            SDL_RenderDrawLine(renderer, centerX - half, centerY + h, centerX + half, centerY + h);
        }
    }

    // Accumulates horizontal spans across multiple circle stamps, then flushes in one pass.
    // Dramatically faster than calling drawFillCircle per outline point for thick brushes.
    struct SpanBuffer {
        int canvasW, canvasH;
        std::vector<std::vector<std::pair<int,int>>> spans;

        SpanBuffer(int w, int h) : canvasW(w), canvasH(h), spans(h) {}

        void addCircle(int cx, int cy, int radius) {
            int r = std::max(0, radius);
            for (int h = -r; h <= r; h++) {
                int row = cy + h;
                if (row < 0 || row >= canvasH) continue;
                int half = (int)std::sqrt((float)(r * r - h * h));
                int x0 = std::max(0, cx - half);
                int x1 = std::min(canvasW - 1, cx + half);
                spans[row].push_back({x0, x1});
            }
        }

        void flush(SDL_Renderer* renderer) {
            for (int row = 0; row < canvasH; row++) {
                for (auto& seg : spans[row])
                    SDL_RenderDrawLine(renderer, seg.first, row, seg.second, row);
            }
        }
    };

    void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int size, int w = CANVAS_WIDTH, int h = CANVAS_HEIGHT) {
        if (size <= 1) {
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
            return;
        }
        int radius = size / 2;
        SpanBuffer spans(w, h);
        int dx = std::abs(x2 - x1);
        int dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;
        while (true) {
            spans.addCircle(x1, y1, radius);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx)  { err += dx; y1 += sy; }
        }
        spans.flush(renderer);
    }

    void drawRect(SDL_Renderer* renderer, const SDL_Rect* rect, int size, int w = CANVAS_WIDTH, int h = CANVAS_HEIGHT) {
        drawLine(renderer, rect->x, rect->y, rect->x + rect->w, rect->y, size, w, h); 
        drawLine(renderer, rect->x + rect->w, rect->y, rect->x + rect->w, rect->y + rect->h, size, w, h); 
        drawLine(renderer, rect->x + rect->w, rect->y + rect->h, rect->x, rect->y + rect->h, size, w, h); 
        drawLine(renderer, rect->x, rect->y + rect->h, rect->x, rect->y, size, w, h); 
    }

    void drawOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int size, int w = CANVAS_WIDTH, int h = CANVAS_HEIGHT) {
        int left   = std::min(x0, x1);
        int top    = std::min(y0, y1);
        int right  = std::max(x0, x1);
        int bottom = std::max(y0, y1);

        if (left == right || top == bottom) return;

        int cx = (left + right) / 2;
        int cy = (top + bottom) / 2;
        int rx = cx - left;
        int ry = cy - top;
        int brushRadius = size / 2;

        long rx2 = (long)rx * rx, ry2 = (long)ry * ry;

        auto plot = [&](SpanBuffer& spans, int x, int y) {
            spans.addCircle(cx + x, cy + y, brushRadius);
            spans.addCircle(cx - x, cy + y, brushRadius);
            spans.addCircle(cx + x, cy - y, brushRadius);
            spans.addCircle(cx - x, cy - y, brushRadius);
        };

        SpanBuffer spans(w, h);

        // Midpoint ellipse algorithm — decides pixel placement based on
        // which candidate is closer to the true ellipse, giving rounder
        // results at small radii compared to the sqrt-based approach.

        // Region 1: slope magnitude < 1 (step in x)
        int x = 0, y = ry;
        long d1 = ry2 - rx2 * ry + rx2 / 4;
        long dx = 2 * ry2 * x, dy = 2 * rx2 * y;
        while (dx < dy) {
            plot(spans, x, y);
            x++;
            dx += 2 * ry2;
            if (d1 < 0) {
                d1 += dx + ry2;
            } else {
                y--;
                dy -= 2 * rx2;
                d1 += dx - dy + ry2;
            }
        }

        // Region 2: slope magnitude >= 1 (step in y)
        long d2 = ry2 * ((long)(x) * x + x) + rx2 * ((long)(y - 1) * (y - 1)) - rx2 * ry2;
        while (y >= 0) {
            plot(spans, x, y);
            y--;
            dy -= 2 * rx2;
            if (d2 > 0) {
                d2 += rx2 - dy;
            } else {
                x++;
                dx += 2 * ry2;
                d2 += dx - dy + rx2;
            }
        }
        spans.flush(renderer);
    }

    // Alternating black/white dashes around the full perimeter (marching ants style).
    // Always visible regardless of what color is underneath.
    void drawMarchingRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
        const int dashLen = 4;
        int x2 = rect->x + rect->w, y2 = rect->y + rect->h;
        int perim = 2 * (rect->w + rect->h);
        for (int p = 0; p < perim; p++) {
            int x, y;
            if      (p < rect->w)                 { x = rect->x + p;                       y = rect->y; }
            else if (p < rect->w + rect->h)       { x = x2;                                y = rect->y + (p - rect->w); }
            else if (p < 2*rect->w + rect->h)     { x = x2 - (p - rect->w - rect->h);     y = y2; }
            else                                  { x = rect->x;                           y = y2 - (p - 2*rect->w - rect->h); }
            bool black = (p / dashLen) % 2 == 0;
            SDL_SetRenderDrawColor(renderer, black ? 0 : 255, black ? 0 : 255, black ? 0 : 255, 255);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    }
}

class ICoordinateMapper {
public:
    virtual void getCanvasCoords(int winX, int winY, int* canX, int* canY) = 0;
    virtual void getWindowCoords(int canX, int canY, int* winX, int* winY) = 0;
    virtual int getWindowSize(int canSize) = 0;
};

class AbstractTool {
protected:
    ICoordinateMapper* mapper;
    bool isDrawing = false;
    int startX, startY, lastX, lastY;

public:
    AbstractTool(ICoordinateMapper* m) : mapper(m) {}
    virtual ~AbstractTool() {}

    virtual void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
        isDrawing = true;
        startX = lastX = cX;
        startY = lastY = cY;
    }

    virtual void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
        if (isDrawing) {
            lastX = cX;
            lastY = cY;
        }
    }

    virtual bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
        bool changed = isDrawing;
        isDrawing = false;
        return changed;
    }

    virtual void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) = 0;
    virtual void onOverlayRender(SDL_Renderer* overlayRenderer) {}
    virtual bool hasOverlayContent() { return false; }
    virtual void deactivate(SDL_Renderer* canvasRenderer) {}
};

class BrushTool : public AbstractTool {
public:
    using AbstractTool::AbstractTool;

    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override {
        AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
        SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
        DrawingUtils::drawFillCircle(canvasRenderer, cX, cY, brushSize / 2);
    }

    void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override {
        if (isDrawing) {
            SDL_SetRenderDrawColor(canvasRenderer, color.r, color.g, color.b, 255);
            DrawingUtils::drawLine(canvasRenderer, lastX, lastY, cX, cY, brushSize);
            lastX = cX;
            lastY = cY;
        }
    }

    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) override {}
};

// Callback: receives ownership of the shape texture and its canvas-space bounding rect
using ShapeReadyCallback = std::function<void(SDL_Texture*, SDL_Rect)>;

class ShapeTool : public AbstractTool {
    ToolType type;
    ShapeReadyCallback onShapeReady;
public:
    ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb)
        : AbstractTool(m), type(t), onShapeReady(std::move(cb)) {}

    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override {
        isDrawing = true;
        startX = lastX = cX;
        startY = lastY = cY;
    }

    bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override {
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
            // For CIRCLE, snap the inset coords the same way drawOval will
            if (type == ToolType::CIRCLE) {
                int iMinX = shapeMinX + half, iMaxX = shapeMaxX - half;
                int iMinY = shapeMinY + half, iMaxY = shapeMaxY - half;
                int cx = (iMinX + iMaxX) / 2, rx = cx - iMinX;
                int cy = (iMinY + iMaxY) / 2, ry = cy - iMinY;
                shapeMinX = cx - rx - half; shapeMaxX = cx + rx + half;
                shapeMinY = cy - ry - half; shapeMaxY = cy + ry + half;
            }
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

    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) override {
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
        SDL_SetRenderDrawColor(winRenderer, 150, 150, 150, 255);

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
};

class SelectTool : public AbstractTool {
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW };

    struct SelectionState {
        bool active = false;
        SDL_Rect area = {0, 0, 0, 0};
        SDL_Texture* selectionTexture = nullptr;
        bool isMoving = false;
        int dragOffsetX = 0, dragOffsetY = 0;
        Handle resizing = Handle::NONE;
        // Anchor edge positions kept fixed during resize
        int anchorX = 0, anchorY = 0;
    } state;

    static const int GRAB = 6; // canvas-pixel grab margin for handles

    Handle getHandle(int cX, int cY) const {
        if (!state.active) return Handle::NONE;
        const SDL_Rect& a = state.area;
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

public:
    using AbstractTool::AbstractTool;

    ~SelectTool() {
        if (state.selectionTexture) SDL_DestroyTexture(state.selectionTexture);
    }

    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override {
        if (state.active) {
            Handle h = getHandle(cX, cY);
            if (h != Handle::NONE) {
                state.resizing = h;
                // Store the opposite edge as the fixed anchor
                state.anchorX = (h == Handle::W || h == Handle::NW || h == Handle::SW)
                    ? state.area.x + state.area.w : state.area.x;
                state.anchorY = (h == Handle::N || h == Handle::NW || h == Handle::NE)
                    ? state.area.y + state.area.h : state.area.y;
                return;
            }
            SDL_Point pt = {cX, cY};
            if (SDL_PointInRect(&pt, &state.area)) {
                state.isMoving = true;
                state.dragOffsetX = cX - state.area.x;
                state.dragOffsetY = cY - state.area.y;
                return;
            }
        }
        AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
    }

    void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override {
        if (state.resizing != Handle::NONE) {
            Handle h = state.resizing;
            int newX = state.area.x, newY = state.area.y;
            int newW = state.area.w, newH = state.area.h;
            // Update the dragged edge(s), keep anchor fixed
            if (h == Handle::W || h == Handle::NW || h == Handle::SW) {
                newX = std::min(cX, state.anchorX);
                newW = std::abs(state.anchorX - cX);
            }
            if (h == Handle::E || h == Handle::NE || h == Handle::SE) {
                newX = std::min(cX, state.anchorX);
                newW = std::abs(cX - state.anchorX);
            }
            if (h == Handle::N || h == Handle::NW || h == Handle::NE) {
                newY = std::min(cY, state.anchorY);
                newH = std::abs(state.anchorY - cY);
            }
            if (h == Handle::S || h == Handle::SW || h == Handle::SE) {
                newY = std::min(cY, state.anchorY);
                newH = std::abs(cY - state.anchorY);
            }
            if (newW > 0 && newH > 0) {
                state.area = { newX, newY, newW, newH };
            }
        } else if (state.isMoving) {
            state.area.x = cX - state.dragOffsetX;
            state.area.y = cY - state.dragOffsetY;
        } else if (isDrawing) {
            lastX = cX;
            lastY = cY;
        }
    }

    bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override {
        if (state.resizing != Handle::NONE) {
            state.resizing = Handle::NONE;
            return false;
        }
        if (state.isMoving) {
            state.isMoving = false;
            return false; 
        }
        if (isDrawing) {
            if (cX == startX && cY == startY) {
                isDrawing = false;
                return false;
            }
            state.area = { std::min(startX, cX), std::min(startY, cY), 
                           std::max(1, std::abs(cX - startX)), std::max(1, std::abs(cY - startY)) };
            
            if (state.selectionTexture) SDL_DestroyTexture(state.selectionTexture);
            state.selectionTexture = SDL_CreateTexture(canvasRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, state.area.w, state.area.h);
            SDL_SetTextureBlendMode(state.selectionTexture, SDL_BLENDMODE_BLEND);

            std::vector<uint32_t> pixels(state.area.w * state.area.h);
            SDL_RenderReadPixels(canvasRenderer, &state.area, SDL_PIXELFORMAT_ARGB8888, pixels.data(), state.area.w * 4);
            SDL_UpdateTexture(state.selectionTexture, NULL, pixels.data(), state.area.w * 4);

            SDL_SetRenderDrawColor(canvasRenderer, 255, 255, 255, 255);
            SDL_RenderFillRect(canvasRenderer, &state.area);
            
            state.active = true;
            isDrawing = false;
            return true;
        }
        return false;
    }

    void onOverlayRender(SDL_Renderer* overlayRenderer) override {
        // Only render the texture content here (needs canvas coords).
        // The outline, handles, and drag rect are drawn in onPreviewRender
        // (window coords) so they are always exactly 1 window pixel wide.
        if (state.active && state.selectionTexture) {
            SDL_RenderCopy(overlayRenderer, state.selectionTexture, NULL, &state.area);
        }
    }

    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) override {
        // Draw selection drag preview
        if (isDrawing) {
            int mouseX, mouseY; SDL_GetMouseState(&mouseX, &mouseY);
            int curX, curY; mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
            int wx1, wy1, wx2, wy2;
            mapper->getWindowCoords(std::min(startX, curX), std::min(startY, curY), &wx1, &wy1);
            mapper->getWindowCoords(std::max(startX, curX), std::max(startY, curY), &wx2, &wy2);
            SDL_Rect r = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
            DrawingUtils::drawMarchingRect(winRenderer, &r);
        }

        if (state.active) {
            int wx1, wy1, wx2, wy2;
            mapper->getWindowCoords(state.area.x,                state.area.y,                &wx1, &wy1);
            mapper->getWindowCoords(state.area.x + state.area.w, state.area.y + state.area.h, &wx2, &wy2);
            SDL_Rect outline = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
            DrawingUtils::drawMarchingRect(winRenderer, &outline);

            // Draw 8 handle squares in window coords
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
    }

    void deactivate(SDL_Renderer* canvasRenderer) override {
        if (!state.active) return;
        if (state.selectionTexture) {
            SDL_RenderCopy(canvasRenderer, state.selectionTexture, NULL, &state.area);
            SDL_DestroyTexture(state.selectionTexture);
            state.selectionTexture = nullptr;
        }
        state.active = false;
    }

    bool isSelectionActive() const { return state.active; }
    bool isHit(int cX, int cY) const {
        if (!state.active) return false;
        // Consider a hit if clicking inside OR on a handle
        SDL_Point pt = {cX, cY};
        return SDL_PointInRect(&pt, &state.area) || getHandle(cX, cY) != Handle::NONE;
    }

    void activateWithTexture(SDL_Texture* tex, SDL_Rect area) {
        if (state.selectionTexture) SDL_DestroyTexture(state.selectionTexture);
        state.selectionTexture = tex;
        state.area = area;
        state.active = true;
        state.isMoving = false;
        state.resizing = Handle::NONE;
        isDrawing = false;
    }

    bool hasOverlayContent() override { return state.active; }
};

class kPen : public ICoordinateMapper {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvas;
    SDL_Texture* overlay; 
    
    std::unique_ptr<AbstractTool> currentTool;
    ToolType currentType = ToolType::BRUSH;
    ToolType originalType = ToolType::BRUSH;
    int brushSize = 2;
    SDL_Color brushColor = {0, 0, 0, 255};

    // ── Toolbar ──────────────────────────────────────────────────────────────
    static constexpr int TB_W      = 84;   // toolbar width in window pixels
    static constexpr int TB_PAD    = 6;    // inner padding
    static constexpr int ICON_SIZE = 24;   // tool icon button size (3 per row)
    static constexpr int ICON_GAP  = 3;

    // HSV color state (converted to brushColor on change)
    float hue = 0.f, sat = 0.f, val = 0.f;  // 0-1 each
    bool  draggingWheel = false;
    bool  draggingBrightness = false;
    bool  draggingSlider = false;
    int   colorWheelCX = 0, colorWheelCY = 0, colorWheelR = 0;
    SDL_Rect brightnessRect = {0,0,0,0};

    // Custom color slots (3x3 grid, configurable via color wheel)
    static constexpr int NUM_CUSTOM = 9;
    SDL_Color customColors[NUM_CUSTOM] = {
        {220,220,220,255},{180,180,180,255},{120,120,120,255},
        {255,100,100,255},{100,200,100,255},{100,150,255,255},
        {255,200,80,255}, {200,100,255,255},{80,220,200,255},
    };
    int selectedCustomSlot = -1; // -1 = none selected
    int selectedPresetSlot = -1; // -1 = none selected
    bool draggingSwatch = false;
    int  draggingSwatchIdx = -1;

    // Preset colors (27 total, 3 per row = 9 rows)
    static constexpr SDL_Color PRESETS[27] = {
        {255,255,255,255},{0,0,0,255},      {64,64,64,255},    // white, black, dark grey
        {128,128,128,255},{180,180,180,255},{220,220,220,255},  // grey, light grey, lighter grey
        {101,55,0,255},   {160,100,40,255}, {210,170,110,255}, // brown, light brown, lighter brown
        {180,0,0,255},    {255,80,80,255},  {255,180,190,255}, // red, light red, pink
        {230,100,0,255},  {255,165,60,255}, {255,230,0,255},   // orange, light orange, yellow
        {0,100,0,255},    {34,160,34,255},  {140,220,140,255}, // dark green, green, light green
        {0,0,160,255},    {30,100,220,255}, {140,190,255,255}, // dark blue, blue, light blue
        {55,0,130,255},   {128,0,200,255},  {210,150,255,255}, // indigo, purple, light purple
        {148,0,211,255},  {255,0,180,255},  {255,170,230,255}, // violet, magenta, light magenta
    };


    static SDL_Color hsvToRgb(float h, float s, float v) {
        h = fmod(h, 1.f) * 6.f;
        int   i = (int)h;
        float f = h - i, p = v*(1-s), q = v*(1-s*f), t = v*(1-s*(1-f));
        float r,g,b;
        switch(i%6){
            case 0: r=v;g=t;b=p; break; case 1: r=q;g=v;b=p; break;
            case 2: r=p;g=v;b=t; break; case 3: r=p;g=q;b=v; break;
            case 4: r=t;g=p;b=v; break; default:r=v;g=p;b=q; break;
        }
        return {(Uint8)(r*255),(Uint8)(g*255),(Uint8)(b*255),255};
    }

    static void rgbToHsv(SDL_Color c, float& h, float& s, float& v) {
        float r=c.r/255.f, g=c.g/255.f, b=c.b/255.f;
        float mx=std::max({r,g,b}), mn=std::min({r,g,b}), d=mx-mn;
        v = mx; s = mx<1e-6f ? 0 : d/mx;
        if(d<1e-6f){ h=0; return; }
        if(mx==r)      h=fmod((g-b)/d,6.f)/6.f;
        else if(mx==g) h=((b-r)/d+2)/6.f;
        else           h=((r-g)/d+4)/6.f;
        if(h<0) h+=1.f;
    }
    
    std::vector<std::vector<uint32_t>> undoStack;

    SDL_Rect getViewport() {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        // Reserve TB_W pixels on the left for the toolbar
        int availW = winW - TB_W;
        float canvasAspect = (float)CANVAS_WIDTH / CANVAS_HEIGHT;
        float windowAspect = (float)availW / winH;
        SDL_Rect v;
        if (windowAspect > canvasAspect) {
            v.h = winH; v.w = (int)(winH * canvasAspect);
            v.x = TB_W + (availW - v.w) / 2; v.y = 0;
        } else {
            v.w = availW; v.h = (int)(availW / canvasAspect);
            v.x = TB_W; v.y = (winH - v.h) / 2;
        }
        return v;
    }

public:
    void getCanvasCoords(int winX, int winY, int* cX, int* cY) override {
        SDL_Rect v = getViewport();
        // Clamping to ensure we don't pick outside canvas range
        *cX = std::max(0, std::min(CANVAS_WIDTH - 1, (int)std::floor((winX - v.x) * ((float)CANVAS_WIDTH / v.w))));
        *cY = std::max(0, std::min(CANVAS_HEIGHT - 1, (int)std::floor((winY - v.y) * ((float)CANVAS_HEIGHT / v.h))));
    }
    void getWindowCoords(int canX, int canY, int* wX, int* wY) override {
        SDL_Rect v = getViewport();
        *wX = v.x + (int)std::floor(canX * ((float)v.w / CANVAS_WIDTH));
        *wY = v.y + (int)std::floor(canY * ((float)v.h / CANVAS_HEIGHT));
    }
    int getWindowSize(int canSize) override {
        return (int)std::round(canSize * ((float)getViewport().w / CANVAS_WIDTH));
    }

    kPen() {
        SDL_Init(SDL_INIT_VIDEO);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        
        window = SDL_CreateWindow("kPen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 700, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
        
        canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, CANVAS_WIDTH, CANVAS_HEIGHT);
        overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, CANVAS_WIDTH, CANVAS_HEIGHT);
        SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
        
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderTarget(renderer, NULL);
        
        rgbToHsv(brushColor, hue, sat, val);
        setTool(ToolType::BRUSH);
        saveState(undoStack);
    }

    ~kPen() {
        SDL_DestroyTexture(canvas);
        SDL_DestroyTexture(overlay);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void setTool(ToolType t) {
        if (currentTool) {
            SDL_SetRenderTarget(renderer, canvas);
            currentTool->deactivate(renderer);
            SDL_SetRenderTarget(renderer, NULL);
        }
        originalType = t;
        currentType = t;
        switch(t) {
            case ToolType::BRUSH: currentTool = std::make_unique<BrushTool>(this); break;
            case ToolType::LINE:  currentTool = std::make_unique<ShapeTool>(this, ToolType::LINE,   [this](SDL_Texture* tex, SDL_Rect r){ activateShapeSelection(tex, r); }); break;
            case ToolType::RECT:  currentTool = std::make_unique<ShapeTool>(this, ToolType::RECT,   [this](SDL_Texture* tex, SDL_Rect r){ activateShapeSelection(tex, r); }); break;
            case ToolType::CIRCLE:currentTool = std::make_unique<ShapeTool>(this, ToolType::CIRCLE, [this](SDL_Texture* tex, SDL_Rect r){ activateShapeSelection(tex, r); }); break;
            case ToolType::SELECT:currentTool = std::make_unique<SelectTool>(this); break;
        }
    }

    // Switches to SELECT mode and injects the shape texture as the floating selection
    void activateShapeSelection(SDL_Texture* tex, SDL_Rect bounds) {
        // Switch tool to SELECT without deactivating (there's nothing active yet)
        currentType = ToolType::SELECT;
        currentTool = std::make_unique<SelectTool>(this);
        auto* st = static_cast<SelectTool*>(currentTool.get());
        st->activateWithTexture(tex, bounds);
        // Save undo state so the user can undo committing the shape
        saveState(undoStack);
    }

    void saveState(std::vector<std::vector<uint32_t>>& stack) {
        std::vector<uint32_t> pixels(CANVAS_WIDTH * CANVAS_HEIGHT);
        SDL_SetRenderTarget(renderer, canvas);
        SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels.data(), CANVAS_WIDTH * 4);
        stack.push_back(pixels);
        SDL_SetRenderTarget(renderer, NULL);
    }

    // ── Toolbar layout helpers ───────────────────────────────────────────────
    int toolStartY()   const { return TB_PAD; }
    int sliderSectionY() const { return toolStartY() + 3*(ICON_SIZE+ICON_GAP) + 2 + 20 + 2; }
    int sliderSectionH() const { return 14; }

    // Swatch grid geometry helpers — single source of truth for draw + hit-test
    int swatchCellSize()    const { return (TB_W - TB_PAD*2 - 4) / 3; }
    int swatchCellStride()  const { return swatchCellSize() + 2; }
    // Custom grid: origin is below brightness bar — computed at draw time and cached
    // We cache just the top-Y of each grid since it depends on window height via wheel.
    // Instead, hit-testing reads customGridY / presetGridY which are set during draw.
    mutable int customGridY = 0;
    mutable int presetGridY = 0;

    // Given a point, returns the swatch index (0-based) or -1 if not in the grid.
    int hitCustomSwatch(int x, int y) const {
        int sz = swatchCellSize(), stride = swatchCellStride();
        int lx = x - TB_PAD, ly = y - customGridY;
        if(lx < 0 || ly < 0) return -1;
        int col = lx / stride, row = ly / stride;
        if(col >= 3 || row >= 3) return -1;
        // Check we're inside the cell (not in the 2px gap)
        if(lx % stride >= sz || ly % stride >= sz) return -1;
        return row * 3 + col;
    }

    int hitPresetSwatch(int x, int y) const {
        int sz = swatchCellSize(), stride = swatchCellStride();
        int lx = x - TB_PAD, ly = y - presetGridY;
        if(lx < 0 || ly < 0) return -1;
        int col = lx / stride, row = ly / stride;
        if(col >= 3 || row >= 9) return -1;
        if(lx % stride >= sz || ly % stride >= sz) return -1;
        return row * 3 + col;
    }

    // ── Icon drawing ─────────────────────────────────────────────────────────
    void drawIcon(int cx, int cy, ToolType t, bool active) {
        SDL_Color fg = active ? SDL_Color{255,255,255,255} : SDL_Color{160,160,170,255};
        SDL_SetRenderDrawColor(renderer, fg.r, fg.g, fg.b, 255);
        int s = ICON_SIZE/2 - 3; // fits inside small button
        switch(t) {
            case ToolType::BRUSH: {
                for(int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i, cy+i);
                for(int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i+1, cy+i);
                SDL_Rect tip = {cx+s-1, cy+s-1, 3, 3};
                SDL_RenderFillRect(renderer, &tip);
                break;
            }
            case ToolType::LINE: {
                for(int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i, cy-i);
                for(int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i+1, cy-i);
                break;
            }
            case ToolType::RECT: {
                SDL_Rect r={cx-s,cy-s,s*2,s*2};
                SDL_RenderDrawRect(renderer, &r);
                break;
            }
            case ToolType::CIRCLE: {
                for(int deg=0; deg<360; deg+=5) {
                    float a=deg*M_PI/180.f;
                    SDL_RenderDrawPoint(renderer, cx+(int)(s*cos(a)), cy+(int)(s*sin(a)));
                }
                break;
            }
            case ToolType::SELECT: {
                int d=3;
                for(int i=0;i<s*2;i+=d*2){
                    SDL_RenderDrawLine(renderer,cx-s+i,cy-s,cx-s+std::min(i+d,s*2),cy-s);
                    SDL_RenderDrawLine(renderer,cx-s+i,cy+s,cx-s+std::min(i+d,s*2),cy+s);
                }
                for(int i=0;i<s*2;i+=d*2){
                    SDL_RenderDrawLine(renderer,cx-s,cy-s+i,cx-s,cy-s+std::min(i+d,s*2));
                    SDL_RenderDrawLine(renderer,cx+s,cy-s+i,cx+s,cy-s+std::min(i+d,s*2));
                }
                break;
            }
        }
    }

    // ── Full toolbar render ───────────────────────────────────────────────────
    void drawToolbar() {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);

        // Background panel
        SDL_Rect panel = {0, 0, TB_W, winH};
        SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
        SDL_RenderFillRect(renderer, &panel);
        // Right border
        SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
        SDL_RenderDrawLine(renderer, TB_W-1, 0, TB_W-1, winH);

        // ── Tool buttons (3 per row) ──
        // Layout: [brush, line, -] [rect, circle, -] [select, -, -]
        // -1 means empty slot
        const int toolGrid[3][3] = {
            {0, 1, -1},  // brush, line, empty
            {2, 3, -1},  // rect, circle, empty
            {4, -1, -1}  // select, empty, empty
        };
        const ToolType toolTypes[] = {ToolType::BRUSH, ToolType::LINE, ToolType::RECT, ToolType::CIRCLE, ToolType::SELECT};
        int cellW = (TB_W - TB_PAD) / 3;
        int ty = toolStartY();
        for(int row=0; row<3; row++) {
            for(int col=0; col<3; col++) {
                int idx = toolGrid[row][col];
                int bx = TB_PAD/2 + col*cellW;
                int by = ty + row*(ICON_SIZE+ICON_GAP);
                SDL_Rect btn = {bx, by, cellW-2, ICON_SIZE};
                if(idx < 0) {
                    // empty slot - just dim background
                    SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
                    SDL_RenderFillRect(renderer, &btn);
                    SDL_SetRenderDrawColor(renderer, 55, 55, 62, 255);
                    SDL_RenderDrawRect(renderer, &btn);
                    continue;
                }
                bool active = (currentType == toolTypes[idx]);
                if(active) {
                    SDL_SetRenderDrawColor(renderer, 70, 130, 220, 255);
                    SDL_RenderFillRect(renderer, &btn);
                } else {
                    SDL_SetRenderDrawColor(renderer, 45, 45, 52, 255);
                    SDL_RenderFillRect(renderer, &btn);
                }
                SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
                SDL_RenderDrawRect(renderer, &btn);
                drawIcon(bx + (cellW-2)/2, by + ICON_SIZE/2, toolTypes[idx], active);
            }
        }

        // ── Thickness slider (horizontal, compact) ──
        // ── Brush size preview circle ──
        // Fixed area of 20px height above the slider, circle scales with brushSize
        int previewAreaH = 20;
        int labelY = ty + 3*(ICON_SIZE+ICON_GAP) + 2;
        int previewCX = TB_W / 2;
        int previewCY = labelY + previewAreaH / 2;
        int maxR = previewAreaH / 2 - 1;
        int dotR = std::max(1, (int)((brushSize / 20.f) * maxR + 0.5f));
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        for(int py = -dotR; py <= dotR; py++)
            for(int px = -dotR; px <= dotR; px++)
                if(px*px + py*py <= dotR*dotR)
                    SDL_RenderDrawPoint(renderer, previewCX + px, previewCY + py);

        int sliderSectionY = labelY + previewAreaH + 2;
        int sX = TB_PAD, sW = TB_W - TB_PAD*2;
        int sH = 14; // total height of slider widget
        int trackY = sliderSectionY + sH/2;
        // Track
        SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
        SDL_RenderDrawLine(renderer, sX, trackY, sX+sW, trackY);
        SDL_RenderDrawLine(renderer, sX, trackY+1, sX+sW, trackY+1);
        // Thumb
        int thumbX = sX + (int)((brushSize-1)/19.f * sW);
        SDL_Rect thumb = {thumbX-5, sliderSectionY, 10, sH};
        SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
        SDL_RenderFillRect(renderer, &thumb);
        SDL_SetRenderDrawColor(renderer, 120, 120, 130, 255);
        SDL_RenderDrawRect(renderer, &thumb);

        // ── Color wheel ──
        int wTop = sliderSectionY + sH + 8;
        int availH = winH - wTop - TB_PAD;
        int wheelDiam = std::min(TB_W - TB_PAD*2, availH - 20);
        if(wheelDiam < 10) return;
        int wcx = TB_W/2, wcy = wTop + wheelDiam/2;
        int wr  = wheelDiam/2;
        colorWheelCX = wcx; colorWheelCY = wcy; colorWheelR = wr;

        // Draw wheel pixel by pixel
        for(int py=wcy-wr; py<=wcy+wr; py++) {
            for(int px=wcx-wr; px<=wcx+wr; px++) {
                float dx=px-wcx, dy=py-wcy;
                float dist=sqrt(dx*dx+dy*dy);
                if(dist>wr) continue;
                float h=fmod(atan2(dy,dx)/(2*M_PI)+1.f,1.f);
                float s=dist/wr;
                SDL_Color c=hsvToRgb(h,s,val);
                SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,255);
                SDL_RenderDrawPoint(renderer,px,py);
            }
        }
        // Wheel border
        for(int deg=0;deg<360;deg++){
            float a=deg*M_PI/180.f;
            SDL_SetRenderDrawColor(renderer,80,80,90,255);
            SDL_RenderDrawPoint(renderer,wcx+(int)(wr*cos(a)),wcy+(int)(wr*sin(a)));
        }
        // Hue/sat cursor on wheel
        float cursorAngle = hue * 2.f * M_PI;
        int cursorX = wcx + (int)(sat * wr * cos(cursorAngle));
        int cursorY = wcy + (int)(sat * wr * sin(cursorAngle));
        SDL_Rect cur = {cursorX-4, cursorY-4, 8, 8};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &cur);
        SDL_Rect cur2 = {cursorX-3, cursorY-3, 6, 6};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &cur2);

        // ── Brightness bar ──
        int bTop = wTop + wheelDiam + 6;
        int bH   = 12;
        int bX   = TB_PAD, bW = TB_W - TB_PAD*2;
        brightnessRect = {bX, bTop, bW, bH};
        for(int px=bX; px<bX+bW; px++) {
            float t = (float)(px-bX)/bW;
            SDL_Color c = hsvToRgb(hue, sat, t);
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
            SDL_RenderDrawLine(renderer, px, bTop, px, bTop+bH);
        }
        SDL_SetRenderDrawColor(renderer, 80,80,90,255);
        SDL_RenderDrawRect(renderer, &brightnessRect);
        // Brightness cursor
        int bCurX = bX + (int)(val * bW);
        SDL_SetRenderDrawColor(renderer, 255,255,255,255);
        SDL_RenderDrawLine(renderer, bCurX, bTop-2, bCurX, bTop+bH+2);
        SDL_SetRenderDrawColor(renderer, 0,0,0,255);
        SDL_RenderDrawLine(renderer, bCurX+1, bTop-2, bCurX+1, bTop+bH+2);

        // ── Custom color slots (3x3, configurable) ───────────────────────────
        int csy = bTop + bH + 7;
        SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
        SDL_RenderDrawLine(renderer, TB_PAD, csy, TB_W-TB_PAD, csy);
        csy += 4;
        customGridY = csy; // cache for O(1) hit-testing
        int sz = swatchCellSize(), stride = swatchCellStride();
        for(int i = 0; i < NUM_CUSTOM; i++) {
            int col = i % 3, row = i / 3;
            int sx = TB_PAD + col*stride, sy = csy + row*stride;
            SDL_Rect r = {sx, sy, sz, sz};
            SDL_SetRenderDrawColor(renderer, customColors[i].r, customColors[i].g, customColors[i].b, 255);
            SDL_RenderFillRect(renderer, &r);
            if(i == selectedCustomSlot) {
                SDL_Rect outer = {sx-2, sy-2, sz+4, sz+4};
                SDL_SetRenderDrawColor(renderer, 255,255,255,255);
                SDL_RenderDrawRect(renderer, &outer);
                SDL_Rect inner = {sx-1, sy-1, sz+2, sz+2};
                SDL_SetRenderDrawColor(renderer, 0,0,0,255);
                SDL_RenderDrawRect(renderer, &inner);
            } else {
                SDL_SetRenderDrawColor(renderer, 70,70,80,255);
                SDL_RenderDrawRect(renderer, &r);
            }
        }

        // ── Preset colors (27, 3 per row = 9 rows) ───────────────────────────
        int psy = csy + 3*stride + 7;
        SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
        SDL_RenderDrawLine(renderer, TB_PAD, psy, TB_W-TB_PAD, psy);
        psy += 4;
        presetGridY = psy; // cache for O(1) hit-testing
        for(int i = 0; i < 27; i++) {
            int col = i % 3, row = i / 3;
            int sx = TB_PAD + col*stride, sy = psy + row*stride;
            SDL_Rect r = {sx, sy, sz, sz};
            SDL_SetRenderDrawColor(renderer, PRESETS[i].r, PRESETS[i].g, PRESETS[i].b, 255);
            SDL_RenderFillRect(renderer, &r);
            if(i == selectedPresetSlot) {
                SDL_Rect outer = {sx-2, sy-2, sz+4, sz+4};
                SDL_SetRenderDrawColor(renderer, 255,255,255,255);
                SDL_RenderDrawRect(renderer, &outer);
                SDL_Rect inner = {sx-1, sy-1, sz+2, sz+2};
                SDL_SetRenderDrawColor(renderer, 0,0,0,255);
                SDL_RenderDrawRect(renderer, &inner);
            } else {
                SDL_SetRenderDrawColor(renderer, 70,70,80,255);
                SDL_RenderDrawRect(renderer, &r);
            }
        }
    }

    // ── Toolbar interaction ───────────────────────────────────────────────────
    // Returns true if the point is inside the toolbar
    bool inToolbar(int x, int y) const { return x < TB_W; }

    int sliderTopY() const { return sliderSectionY(); }
    int sliderH()    const { return sliderSectionH(); }

    bool handleToolbarDown(int x, int y) {
        if(!inToolbar(x,y)) return false;

        // Tool buttons (3-per-row grid)
        const int toolGrid[3][3] = {{0,1,-1},{2,3,-1},{4,-1,-1}};
        const ToolType toolTypes[] = {ToolType::BRUSH, ToolType::LINE, ToolType::RECT, ToolType::CIRCLE, ToolType::SELECT};
        int cellW = (TB_W - TB_PAD) / 3;
        for(int row=0;row<3;row++) {
            for(int col=0;col<3;col++) {
                int idx = toolGrid[row][col];
                if(idx < 0) continue;
                int bx = TB_PAD/2 + col*cellW;
                int by = toolStartY() + row*(ICON_SIZE+ICON_GAP);
                SDL_Rect btn = {bx, by, cellW-2, ICON_SIZE};
                SDL_Point pt = {x,y};
                if(SDL_PointInRect(&pt,&btn)) { setTool(toolTypes[idx]); return true; }
            }
        }

        // Thickness slider (horizontal)
        int sTop = sliderTopY(), sH = sliderH();
        SDL_Rect sliderArea = {TB_PAD/2, sTop-6, TB_W-TB_PAD, sH+12};
        SDL_Point pt={x,y};
        if(SDL_PointInRect(&pt,&sliderArea)) {
            draggingSlider = true;
            updateSliderFromMouse(x);
            return true;
        }

        // Color wheel
        if(colorWheelR > 0) {
            float dx=x-colorWheelCX, dy=y-colorWheelCY;
            float dist=sqrt(dx*dx+dy*dy);
            if(dist<=colorWheelR+4) {
                draggingWheel = true;
                updateWheelFromMouse(x,y);
                return true;
            }
        }

        // Brightness bar
        SDL_Point bpt={x,y};
        SDL_Rect bExpanded={brightnessRect.x-2,brightnessRect.y-4,brightnessRect.w+4,brightnessRect.h+8};
        if(SDL_PointInRect(&bpt,&bExpanded)) {
            draggingBrightness = true;
            updateBrightnessFromMouse(x);
            return true;
        }

        // Custom color slots — O(1) grid math hit-test
        {
            int i = hitCustomSwatch(x, y);
            if(i >= 0) {
                if(selectedCustomSlot == i) {
                    selectedCustomSlot = -1;
                } else {
                    selectedCustomSlot = i;
                    selectedPresetSlot = -1;
                    brushColor = customColors[i];
                    rgbToHsv(brushColor, hue, sat, val);
                }
                draggingSwatch = true;
                draggingSwatchIdx = i;
                return true;
            }
        }

        // Preset swatches — O(1) grid math hit-test
        {
            int i = hitPresetSwatch(x, y);
            if(i >= 0 && i < 27) {
                if(selectedPresetSlot == i) {
                    selectedPresetSlot = -1;
                } else {
                    selectedPresetSlot = i;
                    selectedCustomSlot = -1;
                    brushColor = PRESETS[i];
                    rgbToHsv(brushColor, hue, sat, val);
                }
                draggingSwatch = true;
                draggingSwatchIdx = i + NUM_CUSTOM; // index in combined preset+custom list
                return true;
            }
        }

        return true; // eat all toolbar clicks
    }

    bool handleToolbarMotion(int x, int y) {
        if(draggingSlider)      { updateSliderFromMouse(x);    return true; }
        if(draggingWheel)       { updateWheelFromMouse(x,y);  return true; }
        if(draggingBrightness)  { updateBrightnessFromMouse(x); return true; }
        if(draggingSwatch)      { return true; } // absorb motion, drop handled on up
        return inToolbar(x,y);
    }

    void handleToolbarUp(int x, int y) {
        // Preset drag → drop onto custom slot copies the color
        if(draggingSwatch && draggingSwatchIdx >= 0) {
            int i = hitCustomSwatch(x, y);
            if(i >= 0 && i != draggingSwatchIdx) {
                if (draggingSwatchIdx < NUM_CUSTOM) {
                    customColors[i] = customColors[draggingSwatchIdx];
                } else {
                    customColors[i] = PRESETS[draggingSwatchIdx - NUM_CUSTOM];
                }
                selectedCustomSlot = i;
                selectedPresetSlot = -1;
                brushColor = customColors[i];
                rgbToHsv(brushColor, hue, sat, val);
            }
        }
        draggingSwatch = false;
        draggingSwatchIdx = -1;
        draggingSlider = false;
        draggingWheel = false;
        draggingBrightness = false;
    }

    bool isDraggingToolbar() const { return draggingWheel || draggingBrightness || draggingSlider || draggingSwatch; }

    void updateSliderFromMouse(int x) {
        int sX = TB_PAD, sW = TB_W - TB_PAD*2;
        int clamped = std::max(sX, std::min(sX + sW, x));
        brushSize = 1 + (int)((float)(clamped - sX) / sW * 19.f + 0.5f);
        brushSize = std::max(1, std::min(20, brushSize));
    }

    void updateWheelFromMouse(int x, int y) {
        float dx=x-colorWheelCX, dy=y-colorWheelCY;
        float dist=sqrt(dx*dx+dy*dy);
        hue = fmod(atan2(dy,dx)/(2*M_PI)+1.f,1.f);
        sat = std::min(1.f, dist/colorWheelR);
        brushColor = hsvToRgb(hue, sat, val);
        selectedPresetSlot = -1; // deselect preset when using wheel
        if(selectedCustomSlot >= 0) customColors[selectedCustomSlot] = brushColor;
    }

    void updateBrightnessFromMouse(int x) {
        float t = (float)(x - brightnessRect.x) / brightnessRect.w;
        val = std::max(0.f, std::min(1.f, t));
        brushColor = hsvToRgb(hue, sat, val);
        selectedPresetSlot = -1; // deselect preset when using brightness
        if(selectedCustomSlot >= 0) customColors[selectedCustomSlot] = brushColor;
    }

    void run() {
        bool running = true;
        bool needsRedraw = true;
        bool overlayDirty = false;
        SDL_Event e;
        while (running) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_b: setTool(ToolType::BRUSH); needsRedraw = true; break;
                        case SDLK_l: setTool(ToolType::LINE);  needsRedraw = true; break;
                        case SDLK_r: setTool(ToolType::RECT);  needsRedraw = true; break;
                        case SDLK_o: setTool(ToolType::CIRCLE);needsRedraw = true; break;
                        case SDLK_s: setTool(ToolType::SELECT);needsRedraw = true; break;
                        case SDLK_UP:   brushSize = std::min(20, brushSize + 1); break;
                        case SDLK_DOWN: brushSize = std::max(1,  brushSize - 1); break;
                        case SDLK_z: if (e.key.keysym.mod & KMOD_CTRL) {
                            if (undoStack.size() > 1) { undoStack.pop_back(); applyState(undoStack.back()); needsRedraw = true; }
                        } break;
                    }
                }
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED)
                    needsRedraw = true;

                int cX, cY;
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    if (handleToolbarDown(e.button.x, e.button.y)) { needsRedraw = true; continue; }
                    getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
                    
                    if (currentType == ToolType::SELECT) {
                        auto st = static_cast<SelectTool*>(currentTool.get());
                        if (st->isSelectionActive() && !st->isHit(cX, cY)) {
                            SDL_SetRenderTarget(renderer, canvas);
                            st->deactivate(renderer);
                            SDL_SetRenderTarget(renderer, NULL);
                            saveState(undoStack);
                            setTool(originalType); // switch back to original tool after committing selection
                        }
                    }
                    
                    SDL_SetRenderTarget(renderer, canvas);
                    currentTool->onMouseDown(cX, cY, renderer, brushSize, brushColor);
                    SDL_SetRenderTarget(renderer, NULL);
                    needsRedraw = true; overlayDirty = true;
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    handleToolbarUp(e.button.x, e.button.y);
                    getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
                    SDL_SetRenderTarget(renderer, canvas);
                    if (currentTool->onMouseUp(cX, cY, renderer, brushSize, brushColor)) {
                        SDL_SetRenderTarget(renderer, NULL);
                        saveState(undoStack);
                    }
                    SDL_SetRenderTarget(renderer, NULL);
                    needsRedraw = true; overlayDirty = true;
                }
                if (e.type == SDL_MOUSEMOTION) {
                    if (handleToolbarMotion(e.motion.x, e.motion.y)) { needsRedraw = true; continue; }
                    getCanvasCoords(e.motion.x, e.motion.y, &cX, &cY);
                    SDL_SetRenderTarget(renderer, canvas);
                    currentTool->onMouseMove(cX, cY, renderer, brushSize, brushColor);
                    SDL_SetRenderTarget(renderer, NULL);
                    needsRedraw = true; overlayDirty = true;
                }
            }

            if (!needsRedraw) {
                SDL_Delay(4); // ~240fps cap when idle
                continue;
            }
            needsRedraw = false;

            // 1. Only redraw overlay texture when state has changed
            bool hasOverlay = currentTool->hasOverlayContent();
            if (overlayDirty && hasOverlay) {
                SDL_SetRenderTarget(renderer, overlay);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                currentTool->onOverlayRender(renderer);
                SDL_SetRenderTarget(renderer, NULL);
                overlayDirty = false;
            } else if (!hasOverlay && overlayDirty) {
                // Clear overlay if nothing to show
                SDL_SetRenderTarget(renderer, overlay);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                SDL_RenderClear(renderer);
                SDL_SetRenderTarget(renderer, NULL);
                overlayDirty = false;
            }

            // 2. Main Window Render
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
            SDL_RenderClear(renderer);
            
            SDL_Rect v = getViewport();
            SDL_RenderCopy(renderer, canvas, NULL, &v);
            if (hasOverlay)
                SDL_RenderCopy(renderer, overlay, NULL, &v);
            
            // 3. UI Helpers (shape previews)
            currentTool->onPreviewRender(renderer, brushSize, brushColor);

            // 4. Toolbar
            drawToolbar();

            SDL_RenderPresent(renderer);
        }
    }

    void applyState(std::vector<uint32_t>& p) {
        SDL_UpdateTexture(canvas, NULL, p.data(), CANVAS_WIDTH * 4);
    }
};

int main(int argc, char** argv) {
    kPen app;
    app.run();
    return 0;
}
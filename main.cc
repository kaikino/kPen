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

    void drawDottedRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
        int dashLen = 4;
        // Top
        for (int i = 0; i < rect->w; i += dashLen * 2) 
            SDL_RenderDrawLine(renderer, rect->x + i, rect->y, rect->x + std::min(i + dashLen, rect->w), rect->y);
        // Bottom
        for (int i = 0; i < rect->w; i += dashLen * 2) 
            SDL_RenderDrawLine(renderer, rect->x + i, rect->y + rect->h, rect->x + std::min(i + dashLen, rect->w), rect->y + rect->h);
        // Left
        for (int i = 0; i < rect->h; i += dashLen * 2) 
            SDL_RenderDrawLine(renderer, rect->x, rect->y + i, rect->x, rect->y + std::min(i + dashLen, rect->h));
        // Right
        for (int i = 0; i < rect->h; i += dashLen * 2) 
            SDL_RenderDrawLine(renderer, rect->x + rect->w, rect->y + i, rect->x + rect->w, rect->y + std::min(i + dashLen, rect->h));
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

    virtual void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) {
        isDrawing = true;
        startX = lastX = cX;
        startY = lastY = cY;
    }

    virtual void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) {
        if (isDrawing) {
            lastX = cX;
            lastY = cY;
        }
    }

    virtual bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) {
        bool changed = isDrawing;
        isDrawing = false;
        return changed;
    }

    virtual void onPreviewRender(SDL_Renderer* winRenderer, int brushSize) = 0;
    virtual void onOverlayRender(SDL_Renderer* overlayRenderer) {}
    virtual void deactivate(SDL_Renderer* canvasRenderer) {}
};

class BrushTool : public AbstractTool {
public:
    using AbstractTool::AbstractTool;

    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) override {
        AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize);
        SDL_SetRenderDrawColor(canvasRenderer, 0, 0, 0, 255);
        DrawingUtils::drawFillCircle(canvasRenderer, cX, cY, brushSize / 2);
    }

    void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) override {
        if (isDrawing) {
            SDL_SetRenderDrawColor(canvasRenderer, 0, 0, 0, 255);
            DrawingUtils::drawLine(canvasRenderer, lastX, lastY, cX, cY, brushSize);
            lastX = cX;
            lastY = cY;
        }
    }

    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize) override {}
};

// Callback: receives ownership of the shape texture and its canvas-space bounding rect
using ShapeReadyCallback = std::function<void(SDL_Texture*, SDL_Rect)>;

class ShapeTool : public AbstractTool {
    ToolType type;
    ShapeReadyCallback onShapeReady;
public:
    ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb)
        : AbstractTool(m), type(t), onShapeReady(std::move(cb)) {}

    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) override {
        isDrawing = true;
        startX = lastX = cX;
        startY = lastY = cY;
    }

    bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) override {
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

        SDL_SetRenderDrawColor(canvasRenderer, 0, 0, 0, 255);
        if (type == ToolType::LINE)
            DrawingUtils::drawLine(canvasRenderer, lsx, lsy, lcx, lcy, brushSize);
        else if (type == ToolType::RECT) {
            SDL_Rect r = { std::min(lsx, lcx) + half, std::min(lsy, lcy) + half,
                           std::abs(lcx - lsx) - half * 2, std::abs(lcy - lsy) - half * 2 };
            if (r.w > 0 && r.h > 0) DrawingUtils::drawRect(canvasRenderer, &r, brushSize);
        }
        else if (type == ToolType::CIRCLE)
            DrawingUtils::drawOval(canvasRenderer, lsx + half, lsy + half, lcx - half, lcy - half, brushSize);

        // Restore render target to canvas (kPen set it to canvas before calling us)
        SDL_SetRenderTarget(canvasRenderer, nullptr);

        isDrawing = false;

        // Hand texture off to kPen which will switch to SelectTool with it pre-loaded
        if (onShapeReady) onShapeReady(buf, bounds);

        // Canvas unchanged — return false so kPen doesn't push an undo state here
        return false;
    }

    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize) override {
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
        else if (type == ToolType::CIRCLE) DrawingUtils::drawOval(winRenderer, winStartX + scaledHalf, winStartY + scaledHalf, winCurX - scaledHalf, winCurY - scaledHalf, scaledBrush, winW, winH);
    }
};

class SelectTool : public AbstractTool {
    struct SelectionState {
        bool active = false;
        SDL_Rect area = {0, 0, 0, 0};
        SDL_Texture* selectionTexture = nullptr;
        bool isMoving = false;
        int dragOffsetX = 0, dragOffsetY = 0;
    } state;

public:
    using AbstractTool::AbstractTool;

    ~SelectTool() {
        if (state.selectionTexture) SDL_DestroyTexture(state.selectionTexture);
    }

    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) override {
        if (state.active) {
            SDL_Point pt = {cX, cY};
            if (SDL_PointInRect(&pt, &state.area)) {
                state.isMoving = true;
                state.dragOffsetX = cX - state.area.x;
                state.dragOffsetY = cY - state.area.y;
                return;
            }
        }
        AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize);
    }

    void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) override {
        if (state.isMoving) {
            state.area.x = cX - state.dragOffsetX;
            state.area.y = cY - state.dragOffsetY;
        } else if (isDrawing) {
            lastX = cX;
            lastY = cY;
        }
    }

    bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize) override {
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

            // Read pixels directly from the current canvas target
            std::vector<uint32_t> pixels(state.area.w * state.area.h);
            SDL_RenderReadPixels(canvasRenderer, &state.area, SDL_PIXELFORMAT_ARGB8888, pixels.data(), state.area.w * 4);
            SDL_UpdateTexture(state.selectionTexture, NULL, pixels.data(), state.area.w * 4);

            // Clear the area on the main canvas to WHITE
            SDL_SetRenderDrawColor(canvasRenderer, 255, 255, 255, 255);
            SDL_RenderFillRect(canvasRenderer, &state.area);
            
            state.active = true;
            isDrawing = false;
            return true; // Return true to save undo state after the selection "cut"
        }
        return false;
    }

    void onOverlayRender(SDL_Renderer* overlayRenderer) override {
        // Drag preview while the user is drawing a selection
        if (isDrawing) {
            int mouseX, mouseY; SDL_GetMouseState(&mouseX, &mouseY);
            int curX, curY; mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
            SDL_Rect r = {
                std::min(startX, curX) - 1, std::min(startY, curY) - 1,
                std::abs(curX - startX) + 1, std::abs(curY - startY) + 1
            };
            SDL_SetRenderDrawColor(overlayRenderer, 0, 0, 0, 255);
            DrawingUtils::drawDottedRect(overlayRenderer, &r);
        }

        if (state.active && state.selectionTexture) {
            SDL_RenderCopy(overlayRenderer, state.selectionTexture, NULL, &state.area);
            // Outline is drawn 1px outside so it never overlaps the content pixels
            SDL_Rect outline = { state.area.x - 1, state.area.y - 1, state.area.w + 1, state.area.h + 1 };
            SDL_SetRenderDrawColor(overlayRenderer, 0, 0, 0, 255);
            DrawingUtils::drawDottedRect(overlayRenderer, &outline);
        }
    }

    void deactivate(SDL_Renderer* canvasRenderer) override {
        if (!state.active) return;
        // Merge selection back to canvas
        if (state.selectionTexture) {
            SDL_RenderCopy(canvasRenderer, state.selectionTexture, NULL, &state.area);
            SDL_DestroyTexture(state.selectionTexture);
            state.selectionTexture = nullptr;
        }
        state.active = false;
    }

    bool isSelectionActive() const { return state.active; }
    bool isHit(int cX, int cY) const {
        SDL_Point pt = {cX, cY};
        return state.active && SDL_PointInRect(&pt, &state.area);
    }

    // Called by kPen after a ShapeTool finishes: injects a pre-rendered texture as the active selection
    void activateWithTexture(SDL_Texture* tex, SDL_Rect area) {
        if (state.selectionTexture) SDL_DestroyTexture(state.selectionTexture);
        state.selectionTexture = tex;
        state.area = area;
        state.active = true;
        state.isMoving = false;
        isDrawing = false;
    }

    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize) override {}
};

class kPen : public ICoordinateMapper {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvas;
    SDL_Texture* overlay; 
    
    std::unique_ptr<AbstractTool> currentTool;
    ToolType currentType = ToolType::BRUSH;
    int brushSize = 2;
    
    std::vector<std::vector<uint32_t>> undoStack;

    SDL_Rect getViewport() {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        float canvasAspect = (float)CANVAS_WIDTH / CANVAS_HEIGHT;
        float windowAspect = (float)winW / winH;
        SDL_Rect v;
        if (windowAspect > canvasAspect) {
            v.h = winH; v.w = (int)(winH * canvasAspect);
            v.x = (winW - v.w) / 2; v.y = 0;
        } else {
            v.w = winW; v.h = (int)(winW / canvasAspect);
            v.x = 0; v.y = (winH - v.h) / 2;
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

    void run() {
        bool running = true;
        SDL_Event e;
        while (running) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_b: setTool(ToolType::BRUSH); break;
                        case SDLK_l: setTool(ToolType::LINE); break;
                        case SDLK_r: setTool(ToolType::RECT); break;
                        case SDLK_o: setTool(ToolType::CIRCLE); break;
                        case SDLK_s: setTool(ToolType::SELECT); break;
                        case SDLK_UP: brushSize = std::min(20, brushSize + 1); break;
                        case SDLK_DOWN: brushSize = std::max(1, brushSize - 1); break;
                        case SDLK_z: if (e.key.keysym.mod & KMOD_CTRL) { if(undoStack.size() > 1) { undoStack.pop_back(); applyState(undoStack.back()); } } break;
                    }
                }

                int cX, cY;
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
                    
                    if (currentType == ToolType::SELECT) {
                        auto st = static_cast<SelectTool*>(currentTool.get());
                        if (st->isSelectionActive() && !st->isHit(cX, cY)) {
                            SDL_SetRenderTarget(renderer, canvas);
                            st->deactivate(renderer);
                            SDL_SetRenderTarget(renderer, NULL);
                            saveState(undoStack);
                        }
                    }
                    
                    SDL_SetRenderTarget(renderer, canvas);
                    currentTool->onMouseDown(cX, cY, renderer, brushSize);
                    SDL_SetRenderTarget(renderer, NULL);
                }
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
                    SDL_SetRenderTarget(renderer, canvas);
                    if (currentTool->onMouseUp(cX, cY, renderer, brushSize)) {
                        SDL_SetRenderTarget(renderer, NULL);
                        saveState(undoStack);
                    }
                    SDL_SetRenderTarget(renderer, NULL);
                }
                if (e.type == SDL_MOUSEMOTION) {
                    getCanvasCoords(e.motion.x, e.motion.y, &cX, &cY);
                    SDL_SetRenderTarget(renderer, canvas);
                    currentTool->onMouseMove(cX, cY, renderer, brushSize);
                    SDL_SetRenderTarget(renderer, NULL);
                }
            }

            // Render cycle
            // 1. Draw selection content to internal overlay texture (coordinate-aligned)
            SDL_SetRenderTarget(renderer, overlay);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
            currentTool->onOverlayRender(renderer);
            SDL_SetRenderTarget(renderer, NULL);

            // 2. Main Window Render
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
            SDL_RenderClear(renderer);
            
            SDL_Rect v = getViewport();
            SDL_RenderCopy(renderer, canvas, NULL, &v);
            SDL_RenderCopy(renderer, overlay, NULL, &v);
            
            // 3. UI Helpers (Dotted selection box, shape previews)
            currentTool->onPreviewRender(renderer, brushSize);
            
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
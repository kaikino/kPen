#include <SDL2/SDL.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <memory>

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
        for (int w = -radius; w <= radius; w++) {
            for (int h = -radius; h <= radius; h++) {
                if ((w * w + h * h) <= (radius * radius)) {
                    SDL_RenderDrawPoint(renderer, centerX + w, centerY + h);
                }
            }
        }
    }

    void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int size) {
        if (size <= 1) {
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
            return;
        }
        int radius = size / 2;
        int dx = std::abs(x2 - x1);
        int dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            drawFillCircle(renderer, x1, y1, radius);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx) { err += dx; y1 += sy; }
        }
    }

    void drawRect(SDL_Renderer* renderer, const SDL_Rect* rect, int size) {
        drawLine(renderer, rect->x, rect->y, rect->x + rect->w, rect->y, size); 
        drawLine(renderer, rect->x + rect->w, rect->y, rect->x + rect->w, rect->y + rect->h, size); 
        drawLine(renderer, rect->x + rect->w, rect->y + rect->h, rect->x, rect->y + rect->h, size); 
        drawLine(renderer, rect->x, rect->y + rect->h, rect->x, rect->y, size); 
    }

    void drawOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int size) {
        int left = std::min(x0, x1);
        int top = std::min(y0, y1);
        int width = std::abs(x1 - x0);
        int height = std::abs(y1 - y0);
        
        if (width == 0 || height == 0) return;

        float rx = width / 2.0f;
        float ry = height / 2.0f;
        float centerX = left + rx;
        float centerY = top + ry;

        int radius = size / 2;
        float h_val = pow(rx - ry, 2) / pow(rx + ry, 2);
        float circumference = M_PI * (rx + ry) * (1 + (3 * h_val) / (10 + sqrt(4 - 3 * h_val)));
        int steps = (int)std::max(circumference * 1.5f, 10.0f);

        for (int i = 0; i < steps; i++) {
            float angle = 2.0f * M_PI * i / steps;
            int x = (int)(centerX + rx * cos(angle));
            int y = (int)(centerY + ry * sin(angle));
            drawFillCircle(renderer, x, y, radius);
        }
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

class ShapeTool : public AbstractTool {
    ToolType type;
public:
    ShapeTool(ICoordinateMapper* m, ToolType t) : AbstractTool(m), type(t) {}

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
        SDL_SetRenderDrawColor(canvasRenderer, 0, 0, 0, 255);
        if (type == ToolType::LINE) DrawingUtils::drawLine(canvasRenderer, startX, startY, cX, cY, brushSize);
        else if (type == ToolType::RECT) {
            SDL_Rect r = { std::min(startX, cX), std::min(startY, cY), std::abs(cX - startX), std::abs(cY - startY) };
            DrawingUtils::drawRect(canvasRenderer, &r, brushSize);
        }
        else if (type == ToolType::CIRCLE) DrawingUtils::drawOval(canvasRenderer, startX, startY, cX, cY, brushSize);
        isDrawing = false;
        return true;
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

        if (type == ToolType::LINE) DrawingUtils::drawLine(winRenderer, winStartX, winStartY, winCurX, winCurY, scaledBrush);
        else if (type == ToolType::RECT) {
            SDL_Rect r = { std::min(winStartX, winCurX), std::min(winStartY, winCurY), std::abs(winCurX - winStartX), std::abs(winCurY - winStartY) };
            DrawingUtils::drawRect(winRenderer, &r, scaledBrush);
        }
        else if (type == ToolType::CIRCLE) DrawingUtils::drawOval(winRenderer, winStartX, winStartY, winCurX, winCurY, scaledBrush);
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
        if (state.active && state.selectionTexture) {
            // Render the "knot" selection content only
            SDL_RenderCopy(overlayRenderer, state.selectionTexture, NULL, &state.area);
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

    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize) override {
        if (isDrawing) {
            int winStartX, winStartY, winCurX, winCurY;
            mapper->getWindowCoords(startX, startY, &winStartX, &winStartY);
            
            int mouseX, mouseY; SDL_GetMouseState(&mouseX, &mouseY);
            int curX, curY; mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
            mapper->getWindowCoords(curX, curY, &winCurX, &winCurY);
            
            SDL_Rect r = { std::min(winStartX, winCurX), std::min(winStartY, winCurY), std::abs(winCurX - winStartX), std::abs(winCurY - winStartY) };
            SDL_SetRenderDrawColor(winRenderer, 0, 0, 0, 255);
            DrawingUtils::drawDottedRect(winRenderer, &r);
        }

        if (state.active) {
            int wX, wY, wX2, wY2;
            mapper->getWindowCoords(state.area.x, state.area.y, &wX, &wY);
            mapper->getWindowCoords(state.area.x + state.area.w, state.area.y + state.area.h, &wX2, &wY2);
            SDL_Rect scaledRect = { wX, wY, wX2 - wX, wY2 - wY };
            SDL_SetRenderDrawColor(winRenderer, 0, 0, 0, 255);
            DrawingUtils::drawDottedRect(winRenderer, &scaledRect);
        }
    }
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
            case ToolType::LINE:  currentTool = std::make_unique<ShapeTool>(this, ToolType::LINE); break;
            case ToolType::RECT:  currentTool = std::make_unique<ShapeTool>(this, ToolType::RECT); break;
            case ToolType::CIRCLE:currentTool = std::make_unique<ShapeTool>(this, ToolType::CIRCLE); break;
            case ToolType::SELECT:currentTool = std::make_unique<SelectTool>(this); break;
        }
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
#define _USE_MATH_DEFINES
#include "kPen.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────

kPen::kPen() : toolbar(nullptr, this) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window   = SDL_CreateWindow("kPen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1000, 700, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

    // Patch the renderer into the toolbar now that it exists
    toolbar = Toolbar(renderer, this);

    canvas  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, CANVAS_WIDTH, CANVAS_HEIGHT);
    overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, CANVAS_WIDTH, CANVAS_HEIGHT);
    SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);

    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    setTool(ToolType::BRUSH);
    saveState(undoStack);
}

kPen::~kPen() {
    SDL_DestroyTexture(canvas);
    SDL_DestroyTexture(overlay);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

// ── Viewport ──────────────────────────────────────────────────────────────────

SDL_Rect kPen::getViewport() {
    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    int availW = winW - Toolbar::TB_W;
    float canvasAspect  = (float)CANVAS_WIDTH / CANVAS_HEIGHT;
    float windowAspect  = (float)availW / winH;
    SDL_Rect v;
    if (windowAspect > canvasAspect) {
        v.h = winH; v.w = (int)(winH * canvasAspect);
        v.x = Toolbar::TB_W + (availW - v.w) / 2; v.y = 0;
    } else {
        v.w = availW; v.h = (int)(availW / canvasAspect);
        v.x = Toolbar::TB_W; v.y = (winH - v.h) / 2;
    }
    return v;
}

// ── ICoordinateMapper ─────────────────────────────────────────────────────────

void kPen::getCanvasCoords(int winX, int winY, int* cX, int* cY) {
    SDL_Rect v = getViewport();
    *cX = std::max(0, std::min(CANVAS_WIDTH  - 1,
          (int)std::floor((winX - v.x) * ((float)CANVAS_WIDTH  / v.w))));
    *cY = std::max(0, std::min(CANVAS_HEIGHT - 1,
          (int)std::floor((winY - v.y) * ((float)CANVAS_HEIGHT / v.h))));
}

void kPen::getWindowCoords(int canX, int canY, int* wX, int* wY) {
    SDL_Rect v = getViewport();
    *wX = v.x + (int)std::floor(canX * ((float)v.w / CANVAS_WIDTH));
    *wY = v.y + (int)std::floor(canY * ((float)v.h / CANVAS_HEIGHT));
}

int kPen::getWindowSize(int canSize) {
    return (int)std::round(canSize * ((float)getViewport().w / CANVAS_WIDTH));
}

// ── Tool management ───────────────────────────────────────────────────────────

void kPen::setTool(ToolType t) {
    if (currentTool) {
        SDL_SetRenderTarget(renderer, canvas);
        currentTool->deactivate(renderer);
        SDL_SetRenderTarget(renderer, nullptr);
    }
    originalType = t;
    currentType  = t;
    toolbar.currentType = t;
    switch (t) {
        case ToolType::BRUSH:
            currentTool = std::make_unique<BrushTool>(this);
            break;
        case ToolType::LINE:
            currentTool = std::make_unique<ShapeTool>(this, ToolType::LINE,
                [this](SDL_Texture* tex, SDL_Rect r){ activateShapeSelection(tex, r); });
            break;
        case ToolType::RECT:
            currentTool = std::make_unique<ShapeTool>(this, ToolType::RECT,
                [this](SDL_Texture* tex, SDL_Rect r){ activateShapeSelection(tex, r); });
            break;
        case ToolType::CIRCLE:
            currentTool = std::make_unique<ShapeTool>(this, ToolType::CIRCLE,
                [this](SDL_Texture* tex, SDL_Rect r){ activateShapeSelection(tex, r); });
            break;
        case ToolType::SELECT:
            currentTool = std::make_unique<SelectTool>(this);
            break;
    }
}

void kPen::activateShapeSelection(SDL_Texture* tex, SDL_Rect bounds) {
    currentType = ToolType::SELECT;
    toolbar.currentType = ToolType::SELECT;
    currentTool = std::make_unique<SelectTool>(this);
    auto* st = static_cast<SelectTool*>(currentTool.get());
    st->activateWithTexture(tex, bounds);
    saveState(undoStack);
}

// ── Undo ──────────────────────────────────────────────────────────────────────

void kPen::saveState(std::vector<std::vector<uint32_t>>& stack) {
    std::vector<uint32_t> pixels(CANVAS_WIDTH * CANVAS_HEIGHT);
    SDL_SetRenderTarget(renderer, canvas);
    SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), CANVAS_WIDTH * 4);
    stack.push_back(pixels);
    SDL_SetRenderTarget(renderer, nullptr);
}

void kPen::applyState(std::vector<uint32_t>& pixels) {
    SDL_UpdateTexture(canvas, nullptr, pixels.data(), CANVAS_WIDTH * 4);
}

// ── Run loop ──────────────────────────────────────────────────────────────────

void kPen::run() {
    bool running      = true;
    bool needsRedraw  = true;
    bool overlayDirty = false;
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }

            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_b: setTool(ToolType::BRUSH);  needsRedraw = true; break;
                    case SDLK_l: setTool(ToolType::LINE);   needsRedraw = true; break;
                    case SDLK_r: setTool(ToolType::RECT);   needsRedraw = true; break;
                    case SDLK_o: setTool(ToolType::CIRCLE); needsRedraw = true; break;
                    case SDLK_s: setTool(ToolType::SELECT); needsRedraw = true; break;
                    case SDLK_UP:
                        toolbar.brushSize = std::min(20, toolbar.brushSize + 1);
                        needsRedraw = true; break;
                    case SDLK_DOWN:
                        toolbar.brushSize = std::max(1, toolbar.brushSize - 1);
                        needsRedraw = true; break;
                    case SDLK_z:
                        if (e.key.keysym.mod & KMOD_CTRL) {
                            if (undoStack.size() > 1) {
                                undoStack.pop_back();
                                applyState(undoStack.back());
                                needsRedraw = true;
                            }
                        }
                        break;
                }
            }

            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED)
                needsRedraw = true;

            int cX, cY;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (toolbar.onMouseDown(e.button.x, e.button.y)) { needsRedraw = true; continue; }
                getCanvasCoords(e.button.x, e.button.y, &cX, &cY);

                if (currentType == ToolType::SELECT) {
                    auto* st = static_cast<SelectTool*>(currentTool.get());
                    if (st->isSelectionActive() && !st->isHit(cX, cY)) {
                        SDL_SetRenderTarget(renderer, canvas);
                        st->deactivate(renderer);
                        SDL_SetRenderTarget(renderer, nullptr);
                        saveState(undoStack);
                        setTool(originalType);
                    }
                }

                SDL_SetRenderTarget(renderer, canvas);
                currentTool->onMouseDown(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor);
                SDL_SetRenderTarget(renderer, nullptr);
                needsRedraw = true; overlayDirty = true;
            }

            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                toolbar.onMouseUp(e.button.x, e.button.y);
                getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
                SDL_SetRenderTarget(renderer, canvas);
                if (currentTool->onMouseUp(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor)) {
                    SDL_SetRenderTarget(renderer, nullptr);
                    saveState(undoStack);
                }
                SDL_SetRenderTarget(renderer, nullptr);
                needsRedraw = true; overlayDirty = true;
            }

            if (e.type == SDL_MOUSEMOTION) {
                if (toolbar.onMouseMotion(e.motion.x, e.motion.y)) { needsRedraw = true; continue; }
                getCanvasCoords(e.motion.x, e.motion.y, &cX, &cY);
                SDL_SetRenderTarget(renderer, canvas);
                currentTool->onMouseMove(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor);
                SDL_SetRenderTarget(renderer, nullptr);
                needsRedraw = true; overlayDirty = true;
            }
        }

        if (!needsRedraw) { SDL_Delay(4); continue; }
        needsRedraw = false;

        // 1. Overlay texture
        bool hasOverlay = currentTool->hasOverlayContent();
        if (overlayDirty) {
            SDL_SetRenderTarget(renderer, overlay);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
            if (hasOverlay) currentTool->onOverlayRender(renderer);
            SDL_SetRenderTarget(renderer, nullptr);
            overlayDirty = false;
        }

        // 2. Composite
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderClear(renderer);
        SDL_Rect v = getViewport();
        SDL_RenderCopy(renderer, canvas, nullptr, &v);
        if (hasOverlay) SDL_RenderCopy(renderer, overlay, nullptr, &v);

        // 3. Tool preview (selection outline etc. — drawn in window coords)
        currentTool->onPreviewRender(renderer, toolbar.brushSize, toolbar.brushColor);

        // 4. Toolbar
        toolbar.draw();

        SDL_RenderPresent(renderer);
    }
}

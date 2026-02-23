#define _USE_MATH_DEFINES
#include "kPen.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────

kPen::kPen() : toolbar(nullptr, this) {
    // Must be set BEFORE SDL_Init so the macOS trackpad is treated as a
    // multitouch device and fires SDL_MULTIGESTURE / SDL_FINGERDOWN events.
    // Without these, pinch-to-zoom events are silently swallowed on macOS.
    SDL_SetHint(SDL_HINT_TRACKPAD_IS_TOUCH_ONLY, "1");   // SDL >= 2.24.0
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,     "1");

    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window   = SDL_CreateWindow("kPen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1000, 700, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

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
    zoomTarget = zoom;
}

kPen::~kPen() {
    if (checkerboard) SDL_DestroyTexture(checkerboard);
    SDL_DestroyTexture(canvas);
    SDL_DestroyTexture(overlay);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

// ── Viewport ──────────────────────────────────────────────────────────────────

SDL_Rect kPen::getFitViewport() {
    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    int availW = winW - Toolbar::TB_W;
    float canvasAspect = (float)CANVAS_WIDTH / CANVAS_HEIGHT;
    float windowAspect = (float)availW / winH;
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

SDL_Rect kPen::getViewport() {
    SDL_FRect f = getViewportF();
    return { (int)f.x, (int)f.y, (int)f.w, (int)f.h };
}

SDL_FRect kPen::getViewportF() {
    SDL_Rect fit = getFitViewport();
    float zW = fit.w * zoom;
    float zH = fit.h * zoom;
    float x = fit.x + (fit.w - zW) / 2.f + panX;
    float y = fit.y + (fit.h - zH) / 2.f + panY;
    return { x, y, zW, zH };
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
    SDL_Rect v = getViewport();
    return (int)std::round(canSize * ((float)v.w / CANVAS_WIDTH));
}

// ── Pan / Zoom ────────────────────────────────────────────────────────────────

void kPen::zoomAround(float newZoom, int pivotWinX, int pivotWinY) {
    SDL_Rect fit = getFitViewport();
    float dz = newZoom / zoom;
    // Shift pan so the canvas point under the pivot stays fixed
    float pivotRelX = pivotWinX - (fit.x + fit.w / 2.f);
    float pivotRelY = pivotWinY - (fit.y + fit.h / 2.f);
    panX = pivotRelX + (panX - pivotRelX) * dz;
    panY = pivotRelY + (panY - pivotRelY) * dz;
    zoom = newZoom;
}

bool kPen::tickView() {
    bool animating = false;
    const float k = 0.18f;

    // Always lerp zoom toward zoomTarget using the live mouse position as pivot.
    // This runs even during viewScrolling so ctrl+scroll feels smooth, not jumpy.
    {
        float clamped = std::max(MIN_ZOOM, std::min(MAX_ZOOM, zoomTarget));
        float diff = clamped - zoom;
        if (std::abs(diff) > 0.0002f) {
            int mx, my; SDL_GetMouseState(&mx, &my);
            zoomAround(zoom + diff * k, mx, my);
            animating = true;
        } else if (zoom != clamped) {
            int mx, my; SDL_GetMouseState(&mx, &my);
            zoomAround(clamped, mx, my);
        }
    }

    if (viewScrolling) return animating;

    // Compute pan limits
    SDL_Rect fit = getFitViewport();
    float zW = fit.w * zoom;
    float zH = fit.h * zoom;
    float maxPanX = std::max(0.f, (zW - fit.w) / 2.f);
    float maxPanY = std::max(0.f, (zH - fit.h) / 2.f);

    auto snapAxis = [&](float& pan, float maxPan) {
        float lo = -maxPan, hi = maxPan;
        if (pan < lo)      { pan += (lo - pan) * k; if (std::abs(pan-lo)<0.5f) pan=lo; else animating=true; }
        else if (pan > hi) { pan += (hi - pan) * k; if (std::abs(pan-hi)<0.5f) pan=hi; else animating=true; }
    };
    snapAxis(panX, maxPanX);
    snapAxis(panY, maxPanY);

    return animating;
}

void kPen::onCanvasScroll(int winX, int winY, float dx, float dy, bool ctrl) {
    SDL_Rect fit = getFitViewport();

    if (ctrl) {
        // Zoom: position-based from gesture start.
        // Reset pan baseline so switching from pan to ctrl+zoom gets a clean slate.
        if (!viewScrolling) {
            viewScrollBaseZoom = zoom;
            viewScrollRawZoom  = 0.f;
            viewScrolling      = true;
        } else if (viewScrollRawX != 0.f || viewScrollRawY != 0.f) {
            // Was panning — reset zoom baseline from current zoom
            viewScrollBaseZoom = zoom;
            viewScrollRawZoom  = 0.f;
            viewScrollRawX     = 0.f;
            viewScrollRawY     = 0.f;
        }
        viewScrollRawZoom += dy * 0.1f;

        float rawZoom = viewScrollBaseZoom * expf(viewScrollRawZoom);

        // Rubber-band resistance past zoom limits
        float displayZoom = rawZoom;
        const float kz = 0.3f;
        if (rawZoom < MIN_ZOOM) {
            float rawOver = MIN_ZOOM / rawZoom - 1.f;
            float dispOver = rawOver * kz / (rawOver + kz);
            displayZoom = MIN_ZOOM / (1.f + dispOver);
        } else if (rawZoom > MAX_ZOOM) {
            float rawOver = rawZoom / MAX_ZOOM - 1.f;
            float dispOver = rawOver * kz / (rawOver + kz);
            displayZoom = MAX_ZOOM * (1.f + dispOver);
        }
        // Only update the target — tickView lerps zoom smoothly toward it
        // each tick using the live mouse position, so no jitter.
        zoomTarget = std::max(MIN_ZOOM, std::min(MAX_ZOOM, rawZoom));
    } else {
        // Pan: position-based from gesture start.
        // Reset zoom baseline so switching from ctrl+zoom to pan gets a clean slate.
        if (!viewScrolling) {
            viewScrollBaseX   = panX;
            viewScrollBaseY   = panY;
            viewScrollRawX    = 0.f;
            viewScrollRawY    = 0.f;
            viewScrolling     = true;
        } else if (viewScrollRawZoom != 0.f) {
            // Was zooming — reset pan baseline from current pan
            viewScrollBaseX   = panX;
            viewScrollBaseY   = panY;
            viewScrollRawX    = 0.f;
            viewScrollRawY    = 0.f;
            viewScrollRawZoom = 0.f;
        }
        viewScrollRawX += dx * 2.5f;
        viewScrollRawY += dy * 2.5f;

        float zW = fit.w * zoom;
        float zH = fit.h * zoom;
        float maxPanX = std::max(0.f, (zW - fit.w) / 2.f);
        float maxPanY = std::max(0.f, (zH - fit.h) / 2.f);

        // Same hyperbolic rubber-band as toolbar
        const float k = 80.f;
        auto resist = [&](float base, float raw, float lo, float hi) -> float {
            float t = base + raw;
            if (t < lo) { float o = lo-t; return lo - o*k/(o+k); }
            if (t > hi) { float o = t-hi; return hi + o*k/(o+k); }
            return t;
        };
        panX = resist(viewScrollBaseX, viewScrollRawX, -maxPanX, maxPanX);
        panY = resist(viewScrollBaseY, viewScrollRawY, -maxPanY, maxPanY);
    }
}

// ── Tool management ───────────────────────────────────────────────────────────

// Helper: set render target to canvas, run f, restore to nullptr
template<typename F> void kPen::withCanvas(F f) {
    SDL_SetRenderTarget(renderer, canvas); f(); SDL_SetRenderTarget(renderer, nullptr);
}

void kPen::setTool(ToolType t) {
    if (currentTool) {
        withCanvas([&]{ currentTool->deactivate(renderer); });
        if (currentType == ToolType::RESIZE) {
            saveState(undoStack);
        } else if (currentType == ToolType::SELECT) {
            if (static_cast<SelectTool*>(currentTool.get())->hasMoved())
                saveState(undoStack);
        }
    }
    originalType = currentType = toolbar.currentType = t;
    auto cb = [this](ToolType st, SDL_Rect b, int sx, int sy, int ex, int ey, int bs, SDL_Color c) {
        activateResizeTool(st, b, sx, sy, ex, ey, bs, c);
    };
    switch (t) {
        case ToolType::BRUSH:  currentTool = std::make_unique<BrushTool>(this); break;
        case ToolType::LINE:   currentTool = std::make_unique<ShapeTool>(this, ToolType::LINE,   cb); break;
        case ToolType::RECT:   currentTool = std::make_unique<ShapeTool>(this, ToolType::RECT,   cb); break;
        case ToolType::CIRCLE: currentTool = std::make_unique<ShapeTool>(this, ToolType::CIRCLE, cb); break;
        case ToolType::SELECT: currentTool = std::make_unique<SelectTool>(this); break;
        case ToolType::FILL:   currentTool = std::make_unique<FillTool>(this); break;
        case ToolType::RESIZE: break; // only created via activateResizeTool
    }
}

void kPen::activateResizeTool(ToolType shapeType, SDL_Rect bounds,
                               int sx, int sy, int ex, int ey,
                               int brushSize, SDL_Color color) {
    currentType = toolbar.currentType = ToolType::RESIZE;
    currentTool = std::make_unique<ResizeTool>(this, shapeType, bounds,
                                               sx, sy, ex, ey, brushSize, color);
}

// ── Undo ──────────────────────────────────────────────────────────────────────

void kPen::saveState(std::vector<std::vector<uint32_t>>& stack) {
    std::vector<uint32_t> pixels(CANVAS_WIDTH * CANVAS_HEIGHT);
    withCanvas([&]{ SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), CANVAS_WIDTH * 4); });
    stack.push_back(std::move(pixels));
    if (&stack == &undoStack) redoStack.clear();
}

void kPen::applyState(std::vector<uint32_t>& pixels) {
    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive()) st->activateWithTexture(nullptr, {0,0,0,0});
    }
    if (currentType == ToolType::SELECT || currentType == ToolType::RESIZE) {
        currentTool.reset(); // prevent setTool from deactivating+saving
        setTool(originalType);
    }
    SDL_UpdateTexture(canvas, nullptr, pixels.data(), CANVAS_WIDTH * 4);
}

// Stamp the active SELECT or RESIZE tool onto a pixel buffer for redo, then restore canvas.
void kPen::stampForRedo(AbstractTool* tool) {
    std::vector<uint32_t> redoPixels(CANVAS_WIDTH * CANVAS_HEIGHT);
    withCanvas([&]{
        tool->deactivate(renderer);
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, redoPixels.data(), CANVAS_WIDTH * 4);
        SDL_UpdateTexture(canvas, nullptr, undoStack.back().data(), CANVAS_WIDTH * 4);
    });
    redoStack.clear();
    redoStack.push_back(std::move(redoPixels));
}

void kPen::undo() {
    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive()) {
            if (st->hasMoved()) stampForRedo(st);
            currentTool.reset();
            setTool(originalType);
            SDL_UpdateTexture(canvas, nullptr, undoStack.back().data(), CANVAS_WIDTH * 4);
            return;
        }
    }
    if (currentType == ToolType::RESIZE) {
        // Shape is in-progress — just discard it, restore last committed state.
        currentTool.reset(); // prevent setTool from deactivating+saving
        setTool(originalType);
        SDL_UpdateTexture(canvas, nullptr, undoStack.back().data(), CANVAS_WIDTH * 4);
        return;
    }
    if (undoStack.size() > 1) {
        saveState(redoStack);
        undoStack.pop_back();
        applyState(undoStack.back());
    }
}

void kPen::redo() {
    if (!redoStack.empty()) {
        undoStack.push_back(redoStack.back());
        applyState(redoStack.back());
        redoStack.pop_back();
    }
}

// ── Run loop ──────────────────────────────────────────────────────────────────

void kPen::run() {
    bool running      = true;
    bool needsRedraw  = true;
    bool overlayDirty = false;
    SDL_Event e;

    SDL_EventState(SDL_MULTIGESTURE, SDL_ENABLE);

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
                    case SDLK_f: setTool(ToolType::FILL);   needsRedraw = true; break;
                    case SDLK_BACKSPACE:
                    case SDLK_DELETE:
                        if (currentType == ToolType::SELECT) {
                            currentTool = std::make_unique<SelectTool>(this);
                            needsRedraw = true;
                        }
                        break;
                    case SDLK_UP:
                        toolbar.brushSize = std::min(20, toolbar.brushSize + 1);
                        needsRedraw = true; break;
                    case SDLK_DOWN:
                        toolbar.brushSize = std::max(1, toolbar.brushSize - 1);
                        needsRedraw = true; break;
                    case SDLK_z:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) { undo(); needsRedraw = true; }
                        break;
                    case SDLK_y:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) { redo(); needsRedraw = true; }
                        break;
                    case SDLK_0:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
                            zoom = 1.f; zoomTarget = 1.f; panX = 0.f; panY = 0.f;
                            needsRedraw = true;
                        }
                        break;
                }
            }

            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED)
                needsRedraw = true;

            // ── Mouse wheel ──
            if (e.type == SDL_MOUSEWHEEL) {
                int mx, my; SDL_GetMouseState(&mx, &my);
                #if SDL_VERSION_ATLEAST(2, 0, 18)
                    float precX = e.wheel.preciseX;
                    float precY = e.wheel.preciseY;
                #else
                    float precX = (float)e.wheel.x;
                    float precY = (float)e.wheel.y;
                #endif

                if (toolbar.onMouseWheel(mx, my, precY)) {
                    needsRedraw = true;
                } else if (toolbar.inToolbar(mx, my)) {
                    // Toolbar region: consumed, don't touch canvas
                } else if (SDL_GetModState() & (KMOD_GUI | KMOD_CTRL)) {
                    // Ctrl/Cmd held: zoom toward cursor, block all scrolling and gesture zoom
                    onCanvasScroll(mx, my, 0.f, precY, true);
                    needsRedraw = true;
                } else if (multiGestureActive) {
                    // Multigesture active: multigesture handler owns pan, drop wheel pan
                } else {
                    onCanvasScroll(mx, my, -precX, precY, false);
                    needsRedraw = true;
                }
            }

            // ── Pinch-to-zoom + two-finger pan ──
            if (e.type == SDL_MULTIGESTURE) {
                int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
                float cx = e.mgesture.x * winW;
                float cy = e.mgesture.y * winH;

                // SDL_GetMouseState gives the actual cursor/finger position on macOS,
                // which is reliable for toolbar detection (centroid coords are not).
                int mx, my; SDL_GetMouseState(&mx, &my);
                bool overToolbar = toolbar.inToolbar(mx, my);
                bool ctrlHeld    = (SDL_GetModState() & (KMOD_GUI | KMOD_CTRL)) != 0;

                // ① Zoom first — skip if over toolbar or ctrl held (ctrl+scroll owns zoom)
                if (std::abs(e.mgesture.dDist) > 0.0002f && !overToolbar && !ctrlHeld) {
                    if (!pinchActive) {
                        pinchBaseZoom = zoom;
                        pinchRawDist  = 0.f;
                        pinchActive   = true;
                        viewScrolling = true;
                    }
                    pinchRawDist += e.mgesture.dDist * 6.f;
                    float rawZoom = pinchBaseZoom * expf(pinchRawDist);
                    zoomTarget = std::max(MIN_ZOOM, std::min(MAX_ZOOM, rawZoom));
                }

                // ② Pan second — skip if over toolbar or ctrl held
                if (multiGestureActive && !overToolbar && !ctrlHeld) {
                    panX += cx - lastGestureCX;
                    panY += cy - lastGestureCY;
                    viewScrolling = true;
                }
                lastGestureCX      = cx;
                lastGestureCY      = cy;
                multiGestureActive = true;

                needsRedraw = true;
            }

            // Reset gesture tracking when fingers lift
            if (e.type == SDL_FINGERUP) {
                multiGestureActive = false;
                pinchActive        = false;
                viewScrolling      = false;
            }

            int cX, cY;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT
                && !multiGestureActive) {
                viewScrolling = false;
                if (toolbar.onMouseDown(e.button.x, e.button.y)) { needsRedraw = true; continue; }
                getCanvasCoords(e.button.x, e.button.y, &cX, &cY);

                if (currentType == ToolType::SELECT) {
                    auto* st = static_cast<SelectTool*>(currentTool.get());
                    if (st->isSelectionActive() && !st->isHit(cX, cY)) {
                        bool moved = st->hasMoved();
                        withCanvas([&]{ st->deactivate(renderer); });
                        if (moved) { saveState(undoStack); }
                        // Fall through — onMouseDown below starts a new rubber-band selection
                    }
                }

                if (currentType == ToolType::RESIZE) {
                    auto* rt = static_cast<ResizeTool*>(currentTool.get());
                    if (!rt->isHit(cX, cY)) {
                        withCanvas([&]{ rt->deactivate(renderer); });
                        saveState(undoStack);
                        currentTool.reset(); // already saved; skip setTool's deactivate+save
                        setTool(originalType);
                        // Fall through so this click also starts drawing the next shape
                    }
                }

                // Only begin drawing if the click landed inside the canvas viewport
                SDL_Rect v = getViewport();
                SDL_Point pt = { e.button.x, e.button.y };
                if (!SDL_PointInRect(&pt, &v)) { needsRedraw = true; continue; }

                withCanvas([&]{ currentTool->onMouseDown(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });
                needsRedraw = true; overlayDirty = true;
            }

            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT
                && !multiGestureActive) {
                toolbar.onMouseUp(e.button.x, e.button.y);
                getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
                bool changed = false;
                withCanvas([&]{ changed = currentTool->onMouseUp(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });
                if (changed && currentType != ToolType::SELECT && currentType != ToolType::RESIZE)
                    saveState(undoStack);
                needsRedraw = true; overlayDirty = true;
            }

            if (e.type == SDL_MOUSEMOTION && !multiGestureActive) {
                viewScrolling = false;
                if (toolbar.onMouseMotion(e.motion.x, e.motion.y)) { needsRedraw = true; continue; }
                getCanvasCoords(e.motion.x, e.motion.y, &cX, &cY);
                withCanvas([&]{ currentTool->onMouseMove(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });

                // Clear redo history the moment the user starts dragging a
                // selection or resize handle — any future redo would be stale.
                if (currentType == ToolType::SELECT) {
                    if (static_cast<SelectTool*>(currentTool.get())->isMutating()) redoStack.clear();
                } else if (currentType == ToolType::RESIZE) {
                    if (static_cast<ResizeTool*>(currentTool.get())->isMutating()) redoStack.clear();
                }

                needsRedraw = true; overlayDirty = true;
            }
        }

        if (!needsRedraw) {
            bool ta = toolbar.tickScroll();
            bool va = tickView();
            if (ta || va) needsRedraw = true;
            else { SDL_Delay(4); continue; }
        } else {
            toolbar.tickScroll();
            tickView();
        }
        needsRedraw = false;

        // 1. Overlay
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
        SDL_FRect vf = getViewportF();
        SDL_Rect  vi = getViewport(); // integer version for checkerboard bounds

        // Checkerboard behind canvas — clipped to visible screen area
        {
            int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
            int sx0 = std::max(vi.x, Toolbar::TB_W);
            int sy0 = std::max(vi.y, 0);
            int sx1 = std::min(vi.x + vi.w, winW);
            int sy1 = std::min(vi.y + vi.h, winH);
            int sw  = sx1 - sx0, sh = sy1 - sy0;

            if (sw > 0 && sh > 0) {
                if (sw != checkerW || sh != checkerH) {
                    if (checkerboard) SDL_DestroyTexture(checkerboard);
                    checkerboard = SDL_CreateTexture(renderer,
                        SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, sw, sh);
                    SDL_SetRenderTarget(renderer, checkerboard);
                    // Offset pattern so it aligns with canvas origin regardless of pan
                    int offX = ((sx0 - vi.x) % (12*2) + 12*2) % (12*2);
                    int offY = ((sy0 - vi.y) % (12*2) + 12*2) % (12*2);
                    const int cs = 12;
                    for (int ty = -offY; ty < sh; ty += cs) {
                        for (int tx = -offX; tx < sw; tx += cs) {
                            bool light = (((tx+offX)/cs) + ((ty+offY)/cs)) % 2 == 0;
                            SDL_SetRenderDrawColor(renderer,
                                light?200:160, light?200:160, light?200:160, 255);
                            SDL_Rect cell = {
                                std::max(0,tx), std::max(0,ty),
                                std::min(cs, sw - std::max(0,tx)),
                                std::min(cs, sh - std::max(0,ty))
                            };
                            if (cell.w > 0 && cell.h > 0)
                                SDL_RenderFillRect(renderer, &cell);
                        }
                    }
                    SDL_SetRenderTarget(renderer, nullptr);
                    checkerW = sw;
                    checkerH = sh;
                }
                SDL_Rect dst = { sx0, sy0, sw, sh };
                SDL_RenderCopy(renderer, checkerboard, nullptr, &dst);
            }
        }

        // Only copy the portion of the canvas actually visible on screen.
        // At high zoom vf extends far off-screen; clipping to the window bounds
        // and computing the corresponding canvas source rect avoids GPU work on
        // invisible pixels and eliminates zoom-lag entirely.
        {
            int winW, winH; SDL_GetWindowSize(window, &winW, &winH);

            // Screen-space bounds of the canvas (may be outside window at high zoom)
            float cx0 = vf.x, cy0 = vf.y, cx1 = vf.x + vf.w, cy1 = vf.y + vf.h;

            // Clip to window
            float sx0 = std::max(cx0, (float)Toolbar::TB_W);
            float sy0 = std::max(cy0, 0.f);
            float sx1 = std::min(cx1, (float)winW);
            float sy1 = std::min(cy1, (float)winH);

            if (sx1 > sx0 && sy1 > sy0) {
                // Map clipped screen rect back to canvas texel coords
                float scaleX = CANVAS_WIDTH  / vf.w;
                float scaleY = CANVAS_HEIGHT / vf.h;
                SDL_FRect srcF = {
                    (sx0 - cx0) * scaleX,
                    (sy0 - cy0) * scaleY,
                    (sx1 - sx0) * scaleX,
                    (sy1 - sy0) * scaleY
                };
                SDL_Rect src = {
                    (int)srcF.x, (int)srcF.y,
                    (int)std::ceil(srcF.w), (int)std::ceil(srcF.h)
                };
                SDL_FRect dst = { sx0, sy0, sx1 - sx0, sy1 - sy0 };

                SDL_RenderCopyF(renderer, canvas, &src, &dst);
                if (hasOverlay) SDL_RenderCopyF(renderer, overlay, &src, &dst);
            }
        }

        // 3. Tool preview
        currentTool->onPreviewRender(renderer, toolbar.brushSize, toolbar.brushColor);

        // 4. Toolbar
        toolbar.draw();

        SDL_RenderPresent(renderer);
    }
}

#define _USE_MATH_DEFINES
#include "kPen.h"
#include "DrawingUtils.h"
#include "CanvasResizer.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────

kPen::kPen() : toolbar(nullptr, this), canvasResizer(this) {
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
                                SDL_TEXTUREACCESS_TARGET, canvasW, canvasH);
    overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, canvasW, canvasH);
    SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);

    SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(renderer, canvas);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    toolbar.syncCanvasSize(canvasW, canvasH);
    setTool(ToolType::BRUSH);
    saveState(undoStack);
    zoomTarget = zoom;
}

kPen::~kPen() {
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
    static const int GAP = 50;           // minimum gap on all non-toolbar edges
    int fitW = availW - GAP * 2;
    int fitH = winH    - GAP * 2;
    if (fitW < 1) fitW = 1;
    if (fitH < 1) fitH = 1;
    float canvasAspect = (float)canvasW / canvasH;
    float windowAspect = (float)fitW / fitH;
    SDL_Rect v;
    if (windowAspect > canvasAspect) {
        v.h = fitH; v.w = (int)(fitH * canvasAspect);
        v.x = Toolbar::TB_W + GAP + (fitW - v.w) / 2; v.y = GAP;
    } else {
        v.w = fitW; v.h = (int)(fitW / canvasAspect);
        v.x = Toolbar::TB_W + GAP; v.y = GAP + (fitH - v.h) / 2;
    }
    return v;
}

SDL_Rect kPen::getViewport() {
    SDL_FRect f = getViewportF();
    int x = (int)std::floor(f.x);
    int y = (int)std::floor(f.y);
    int x2 = (int)std::ceil(f.x + f.w);
    int y2 = (int)std::ceil(f.y + f.h);
    return { x, y, x2 - x, y2 - y };
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
    SDL_FRect v = getViewportF();
    *cX = (int)std::floor((winX - v.x) * ((float)canvasW / v.w));
    *cY = (int)std::floor((winY - v.y) * ((float)canvasH / v.h));
}

void kPen::getWindowCoords(int canX, int canY, int* wX, int* wY) {
    SDL_FRect v = getViewportF();
    *wX = (int)std::round(v.x + canX * (v.w / canvasW));
    *wY = (int)std::round(v.y + canY * (v.h / canvasH));
}

int kPen::getWindowSize(int canSize) {
    SDL_FRect v = getViewportF();
    return (int)std::round(canSize * (v.w / canvasW));
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

    // Compute pan limits — allow canvas to drift up to 50px outside the window edge
    SDL_Rect fit = getFitViewport();
    float zW = fit.w * zoom;
    float zH = fit.h * zoom;
    float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
    float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;

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
        float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
        float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;

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
            if (static_cast<ResizeTool*>(currentTool.get())->willRender())
                saveState(undoStack);
        } else if (currentType == ToolType::SELECT) {
            if (static_cast<SelectTool*>(currentTool.get())->isDirty())
                saveState(undoStack);
        }
    }
    originalType = currentType = toolbar.currentType = t;
    auto cb = [this](ToolType st, SDL_Rect b, SDL_Rect ob, int sx, int sy, int ex, int ey, int bs, SDL_Color c, bool filled) {
        activateResizeTool(st, b, ob, sx, sy, ex, ey, bs, c, filled);
    };
    switch (t) {
        case ToolType::BRUSH:  currentTool = std::make_unique<BrushTool>(this, toolbar.squareBrush); break;
        case ToolType::ERASER: currentTool = std::make_unique<EraserTool>(this, toolbar.squareBrush); break;
        case ToolType::LINE:   currentTool = std::make_unique<ShapeTool>(this, ToolType::LINE,   cb, false); break;
        case ToolType::RECT:   currentTool = std::make_unique<ShapeTool>(this, ToolType::RECT,   cb, toolbar.fillShape); break;
        case ToolType::CIRCLE: currentTool = std::make_unique<ShapeTool>(this, ToolType::CIRCLE, cb, toolbar.fillShape); break;
        case ToolType::SELECT: currentTool = std::make_unique<SelectTool>(this); break;
        case ToolType::FILL:   currentTool = std::make_unique<FillTool>(this); break;
        case ToolType::RESIZE: break; // only created via activateResizeTool
    }
}

void kPen::activateResizeTool(ToolType shapeType, SDL_Rect bounds, SDL_Rect origBounds,
                               int sx, int sy, int ex, int ey,
                               int brushSize, SDL_Color color, bool filled) {
    currentType = toolbar.currentType = ToolType::RESIZE;
    currentTool = std::make_unique<ResizeTool>(this, shapeType, bounds, origBounds,
                                               sx, sy, ex, ey, brushSize, color, filled);
}

// ── Undo ──────────────────────────────────────────────────────────────────────

void kPen::saveState(std::vector<CanvasState>& stack) {
    CanvasState s;
    s.w = canvasW; s.h = canvasH;
    s.pixels.resize(canvasW * canvasH);
    withCanvas([&]{ SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s.pixels.data(), canvasW * 4); });
    stack.push_back(std::move(s));
    if (&stack == &undoStack) redoStack.clear();
}

void kPen::applyState(CanvasState& s) {
    if (currentType == ToolType::SELECT || currentType == ToolType::RESIZE) {
        currentTool.reset(); // prevent setTool from deactivating+saving
        setTool(originalType);
    }
    if (s.w != canvasW || s.h != canvasH) {
        SDL_DestroyTexture(canvas);
        SDL_DestroyTexture(overlay);
        canvasW = s.w; canvasH = s.h;
        canvas  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_TARGET, canvasW, canvasH);
        overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_TARGET, canvasW, canvasH);
        SDL_SetTextureBlendMode(canvas,  SDL_BLENDMODE_BLEND);
        SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer, overlay);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_SetRenderTarget(renderer, nullptr);
        toolbar.syncCanvasSize(canvasW, canvasH);
    }
    SDL_UpdateTexture(canvas, nullptr, s.pixels.data(), canvasW * 4);
}

// Stamp the active SELECT or RESIZE tool onto a pixel buffer for redo, then restore canvas.
void kPen::stampForRedo(AbstractTool* tool) {
    CanvasState s;
    s.w = canvasW; s.h = canvasH;
    s.pixels.resize(canvasW * canvasH);
    withCanvas([&]{
        tool->deactivate(renderer);
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s.pixels.data(), canvasW * 4);
        SDL_UpdateTexture(canvas, nullptr, undoStack.back().pixels.data(), canvasW * 4);
    });
    redoStack.clear();
    redoStack.push_back(std::move(s));
}

void kPen::undo() {
    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive()) {
            if (st->isDirty()) stampForRedo(st);
            currentTool.reset();
            setTool(originalType);
            applyState(undoStack.back());
            return;
        }
    }
    if (currentType == ToolType::RESIZE) {
        // Shape is in-progress — just discard it, restore last committed state.
        currentTool.reset(); // prevent setTool from deactivating+saving
        setTool(originalType);
        applyState(undoStack.back());
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
        applyState(redoStack.back());
        undoStack.push_back(std::move(redoStack.back()));
        redoStack.pop_back();
    }
}

// ── Canvas resize ─────────────────────────────────────────────────────────────

void kPen::resizeCanvas(int newW, int newH, bool scaleContent, int originX, int originY) {
    newW = std::max(1, std::min(16384, newW));
    newH = std::max(1, std::min(16384, newH));
    if (newW == canvasW && newH == canvasH) return;

    // Commit any active tool so its pixels are stamped onto the canvas before
    // we snapshot it. We do NOT call saveState — the top-of-stack refresh below
    // captures any committed tool pixels without adding an extra undo entry.
    if (currentTool) {
        withCanvas([&]{ currentTool->deactivate(renderer); });
        setTool(originalType);
    }

    // Read the current canvas pixels for building the new buffer.
    // We do NOT push a pre-resize state — undoStack.back() already IS the current
    // state (that invariant is maintained everywhere). We only push the post-resize
    // state below, so one undo step correctly returns to this pre-resize state.
    std::vector<uint32_t> oldPixels(canvasW * canvasH);
    withCanvas([&]{
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                             oldPixels.data(), canvasW * 4);
    });
    // Update the existing top-of-stack entry to capture any tool pixels just committed.
    // (If a tool was active, deactivate() stamped it; saveState wasn't called, so we
    // refresh the top entry manually to keep the invariant accurate.)
    if (!undoStack.empty()) {
        undoStack.back().w = canvasW;
        undoStack.back().h = canvasH;
        undoStack.back().pixels = oldPixels;
    }

    // Build new pixel buffer — transparent background.
    std::vector<uint32_t> newPixels(newW * newH, 0x00000000);

    if (scaleContent) {
        // Nearest-neighbour scale — origin shift ignored, whole image resampled.
        for (int y = 0; y < newH; y++) {
            int srcY = std::min((int)((float)y / newH * canvasH), canvasH - 1);
            for (int x = 0; x < newW; x++) {
                int srcX = std::min((int)((float)x / newW * canvasW), canvasW - 1);
                newPixels[y * newW + x] = oldPixels[srcY * canvasW + srcX];
            }
        }
    } else {
        // Crop / pad with origin shift.
        // originX/Y: how much the old top-left corner moved in canvas pixels.
        // Positive origin means the old content shifts right/down in the new buffer
        // (i.e. padding is added on the left/top). Negative means content is cropped.
        int dstOffX = -originX;
        int dstOffY = -originY;
        for (int oldY = 0; oldY < canvasH; oldY++) {
            int newY = oldY + dstOffY;
            if (newY < 0 || newY >= newH) continue;
            for (int oldX = 0; oldX < canvasW; oldX++) {
                int newX = oldX + dstOffX;
                if (newX < 0 || newX >= newW) continue;
                newPixels[newY * newW + newX] = oldPixels[oldY * canvasW + oldX];
            }
        }
    }

    SDL_DestroyTexture(canvas);
    SDL_DestroyTexture(overlay);

    canvasW = newW;
    canvasH = newH;

    canvas  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, canvasW, canvasH);
    overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, canvasW, canvasH);
    SDL_SetTextureBlendMode(canvas,  SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);

    SDL_UpdateTexture(canvas, nullptr, newPixels.data(), canvasW * 4);

    SDL_SetRenderTarget(renderer, overlay);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    // Push the post-resize state. undoStack.back() (refreshed above) holds the
    // pre-resize pixels+size, so one undo correctly restores the old canvas.
    // Do NOT clear undoStack — resize is undoable.
    redoStack.clear();
    CanvasState newState;
    newState.w = canvasW; newState.h = canvasH;
    newState.pixels = std::move(newPixels);
    undoStack.push_back(std::move(newState));

    toolbar.syncCanvasSize(canvasW, canvasH);
}

// ── Clipboard / delete helpers ────────────────────────────────────────────────

// Delete the active SelectTool or ResizeTool content without stamping it back,
// saving an undo state so the deletion can be undone.
void kPen::deleteSelection() {
    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (!st->isSelectionActive()) return;
        // The canvas already has a transparent hole; save that state, then drop the floating pixels.
        saveState(undoStack);
        currentTool = std::make_unique<SelectTool>(this);
    } else if (currentType == ToolType::RESIZE) {
        // Shape was never stamped onto the canvas — just discard it.
        // No undo state needed: the canvas is unchanged from the last undoStack entry.
        currentTool.reset(); // skip deactivate so shape isn't drawn
        setTool(originalType);
    }
}

// Copy the pixels from the active SelectTool or ResizeTool to the OS clipboard
// as a native image (PNG on macOS, DIB+PNG on Windows) so other apps can paste it.
void kPen::copySelectionToClipboard() {
    SDL_Rect bounds = {0, 0, 0, 0};
    std::vector<uint32_t> pixels;

    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (!st->isSelectionActive()) return;
        bounds = st->getFloatingBounds();
        pixels = st->getFloatingPixels(renderer);
    } else if (currentType == ToolType::RESIZE) {
        auto* rt = static_cast<ResizeTool*>(currentTool.get());
        bounds = rt->getFloatingBounds();
        pixels = rt->getFloatingPixels(renderer);
    } else {
        return;
    }

    if (bounds.w <= 0 || bounds.h <= 0 || pixels.empty()) return;

    DrawingUtils::setClipboardImage(pixels.data(), bounds.w, bounds.h);
}

void kPen::pasteFromClipboard() {
    std::vector<uint32_t> pixels;
    int w = 0, h = 0;
    if (!DrawingUtils::getClipboardImage(pixels, w, h)) return;
    if (w <= 0 || h <= 0 || pixels.empty()) return;

    // Commit any in-progress selection/resize before pasting
    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive()) {
            withCanvas([&]{ st->deactivate(renderer); });
            if (st->isDirty()) saveState(undoStack);
        }
        currentTool = std::make_unique<SelectTool>(this);
    } else if (currentType == ToolType::RESIZE) {
        auto* rt_ = static_cast<ResizeTool*>(currentTool.get());
        bool renders_ = rt_->willRender();
        withCanvas([&]{ currentTool->deactivate(renderer); });
        if (renders_) saveState(undoStack);
        currentTool = std::make_unique<SelectTool>(this);
    } else {
        setTool(ToolType::SELECT);
        currentTool = std::make_unique<SelectTool>(this);
    }
    currentType = toolbar.currentType = ToolType::SELECT;

    // Place pasted content centred on the mouse cursor, clamped to canvas
    int mouseWinX, mouseWinY, mouseCX, mouseCY;
    SDL_GetMouseState(&mouseWinX, &mouseWinY);
    getCanvasCoords(mouseWinX, mouseWinY, &mouseCX, &mouseCY);
    SDL_Rect pasteBounds = {
        std::max(0, std::min(canvasW - w, mouseCX - w / 2)),
        std::max(0, std::min(canvasH - h, mouseCY - h / 2)),
        w, h
    };

    // Upload pixels into a streaming texture and hand it to SelectTool
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex) return;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    void* texPixels; int pitch;
    if (SDL_LockTexture(tex, nullptr, &texPixels, &pitch) == 0) {
        for (int row = 0; row < h; ++row)
            memcpy((uint8_t*)texPixels + row * pitch, pixels.data() + row * w, w * 4);
        SDL_UnlockTexture(tex);
    }

    static_cast<SelectTool*>(currentTool.get())->activateWithTexture(tex, pasteBounds);
    // tex ownership transferred to SelectTool
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

            // ── Text input for toolbar resize fields ──
            if (e.type == SDL_TEXTINPUT) {
                if (toolbar.onTextInput(e.text.text)) { needsRedraw = true; continue; }
            }

            if (e.type == SDL_KEYDOWN) {
                // Track shift for aspect-lock override
                if (e.key.keysym.sym == SDLK_LSHIFT || e.key.keysym.sym == SDLK_RSHIFT) {
                    shiftHeld = true;
                    if (canvasResizer.isDragging()) {
                        toolbar.setShiftLockAspect(true);
                        needsRedraw = true;
                    }
                }

                // Let toolbar consume resize field keys first
                if (toolbar.onResizeKey(e.key.keysym.sym)) { needsRedraw = true; continue; }

                switch (e.key.keysym.sym) {
                    case SDLK_b:
                        if (originalType == ToolType::BRUSH)  toolbar.squareBrush = !toolbar.squareBrush;
                        setTool(ToolType::BRUSH);  needsRedraw = true; break;
                    case SDLK_l: setTool(ToolType::LINE);   needsRedraw = true; break;
                    case SDLK_r:
                        if (originalType == ToolType::RECT)   toolbar.fillShape = !toolbar.fillShape;
                        setTool(ToolType::RECT);   needsRedraw = true; break;
                    case SDLK_o:
                        if (originalType == ToolType::CIRCLE) toolbar.fillShape = !toolbar.fillShape;
                        setTool(ToolType::CIRCLE); needsRedraw = true; break;
                    case SDLK_s: setTool(ToolType::SELECT); needsRedraw = true; break;
                    case SDLK_e:
                        if (originalType == ToolType::ERASER) toolbar.squareBrush = !toolbar.squareBrush;
                        setTool(ToolType::ERASER); needsRedraw = true; break;
                    case SDLK_f: setTool(ToolType::FILL);   needsRedraw = true; break;
                    case SDLK_BACKSPACE:
                    case SDLK_DELETE:
                        deleteSelection();
                        needsRedraw = true;
                        break;
                    case SDLK_UP:
                        if (currentType == ToolType::RESIZE) {
                            auto* rt = static_cast<ResizeTool*>(currentTool.get());
                            rt->shapeBrushSize = std::min(99, rt->shapeBrushSize + 1);
                            toolbar.brushSize  = rt->shapeBrushSize;
                        } else {
                            toolbar.brushSize = std::min(99, toolbar.brushSize + 1);
                        }
                        toolbar.syncBrushSize();
                        needsRedraw = true;
                        if (currentTool->hasOverlayContent()) overlayDirty = true;
                        break;
                    case SDLK_DOWN:
                        if (currentType == ToolType::RESIZE) {
                            auto* rt = static_cast<ResizeTool*>(currentTool.get());
                            rt->shapeBrushSize = std::max(1, rt->shapeBrushSize - 1);
                            toolbar.brushSize  = rt->shapeBrushSize;
                        } else {
                            toolbar.brushSize = std::max(1, toolbar.brushSize - 1);
                        }
                        toolbar.syncBrushSize();
                        needsRedraw = true;
                        if (currentTool->hasOverlayContent()) overlayDirty = true;
                        break;
                    case SDLK_z:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) { undo(); needsRedraw = true; }
                        break;
                    case SDLK_y:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) { redo(); needsRedraw = true; }
                        break;
                    case SDLK_c:
                    case SDLK_x:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
                            copySelectionToClipboard();
                            if (e.key.keysym.sym == SDLK_x) deleteSelection();
                            needsRedraw = true;
                        }
                        break;
                    case SDLK_v:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
                            pasteFromClipboard();
                            needsRedraw = true; overlayDirty = true;
                        }
                        break;
                    case SDLK_0:
                        if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
                            zoom = 1.f; zoomTarget = 1.f; panX = 0.f; panY = 0.f;
                            needsRedraw = true;
                        }
                        break;
                }
            }

            if (e.type == SDL_KEYUP) {
                if (e.key.keysym.sym == SDLK_LSHIFT || e.key.keysym.sym == SDLK_RSHIFT) {
                    shiftHeld = false;
                    toolbar.setShiftLockAspect(false);
                    if (canvasResizer.isDragging()) needsRedraw = true;
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
                    if (currentType == ToolType::RESIZE && toolbar.inToolbar(mx, my)) {
                        // Redirect brush size change to the active ResizeTool
                        auto* rt = static_cast<ResizeTool*>(currentTool.get());
                        rt->shapeBrushSize = std::max(1, std::min(99, toolbar.brushSize));
                        toolbar.brushSize = rt->shapeBrushSize;
                        toolbar.syncBrushSize();
                    }
                    needsRedraw = true;
                    if (currentTool->hasOverlayContent()) overlayDirty = true;
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

            // ── Track live finger count ──
            if (e.type == SDL_FINGERDOWN) activeFingers++;
            if (e.type == SDL_FINGERUP)   activeFingers = std::max(0, activeFingers - 1);

            // ── Second-finger tap detection ──
            // macOS holds MOUSEBUTTONDOWN until the primary finger stops moving,
            // so we detect the tap via FINGER events and synthesize the click ourselves.
            if (e.type == SDL_FINGERDOWN) {
                int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
                tapFingerId    = e.tfinger.fingerId;
                tapDownX       = e.tfinger.x * winW;
                tapDownY       = e.tfinger.y * winH;
                tapDownTime    = e.tfinger.timestamp;
                tapPending     = true;
                tapSawGesture  = false;
            }
            if (e.type == SDL_FINGERUP && tapPending && e.tfinger.fingerId == tapFingerId) {
                tapPending = false;
                int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
                float upX = e.tfinger.x * winW;
                float upY = e.tfinger.y * winH;
                float dx = upX - tapDownX, dy = upY - tapDownY;
                Uint32 dt = e.tfinger.timestamp - tapDownTime;
                // Tap = short duration (<300ms), little movement (<10px), and
                // cursor was moving (confirms a drag+tap, not a plain press+release)
                if (dt < 300 && dx*dx + dy*dy < 100.f && tapSawGesture) {
                    int tx = (int)upX, ty = (int)upY;

                    // Toolbar tap — route through normal toolbar click path.
                    // Don't set tapConsumed: let the real MOUSEBUTTONDOWN be suppressed
                    // only for canvas taps; toolbar can safely handle a double-fire
                    // because onMouseDown is guarded by inToolbar and returns false on miss.
                    if (toolbar.inToolbar(tx, ty)) {
                        if (toolbar.onMouseDown(tx, ty)) {
                            toolbar.onMouseUp(tx, ty);
                            tapConsumed = true;  // suppress the delayed real MOUSEBUTTONDOWN
                            needsRedraw = true;
                        }
                        // if onMouseDown missed (returned false), leave tapConsumed=false
                        // so the real MOUSEBUTTONDOWN can still fire normally
                    } else {
                        tapConsumed = true;
                        // Canvas tap — use primary finger (mouse) position for draw coords
                        int mx, my; SDL_GetMouseState(&mx, &my);
                        int tapCX, tapCY; getCanvasCoords(mx, my, &tapCX, &tapCY);

                        if (currentType == ToolType::RESIZE) {
                            auto* rt = static_cast<ResizeTool*>(currentTool.get());
                            if (!rt->isHit(tapCX, tapCY)) {
                                bool renders = rt->willRender();
                                withCanvas([&]{ rt->deactivate(renderer); });
                                if (renders) saveState(undoStack);
                                currentTool.reset();
                                setTool(originalType);
                            }
                        }
                        if (currentType == ToolType::SELECT) {
                            auto* st = static_cast<SelectTool*>(currentTool.get());
                            if (st->isSelectionActive() && !st->isHit(tapCX, tapCY)) {
                                bool dirty = st->isDirty();
                                withCanvas([&]{ st->deactivate(renderer); });
                                if (dirty) saveState(undoStack);
                            }
                        }

                        withCanvas([&]{ currentTool->onMouseDown(tapCX, tapCY, renderer, toolbar.brushSize, toolbar.brushColor); });
                        if (currentType == ToolType::FILL) saveState(undoStack);
                        withCanvas([&]{ currentTool->onMouseUp  (tapCX, tapCY, renderer, toolbar.brushSize, toolbar.brushColor); });
                        needsRedraw = true; overlayDirty = true;
                    }
                }
            }
            // Cancel tap if the second finger moves significantly before lifting
            if (e.type == SDL_FINGERMOTION && tapPending && e.tfinger.fingerId == tapFingerId) {
                int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
                float mx = e.tfinger.x * winW - tapDownX;
                float my = e.tfinger.y * winH - tapDownY;
                if (mx*mx + my*my > 100.f) tapPending = false;
            }

            // ── Pinch-to-zoom + two-finger pan ──
            if (e.type == SDL_MULTIGESTURE && tapPending) { tapSawGesture = true; }
            if (e.type == SDL_MULTIGESTURE) {
                // 3-finger gesture: suppress pan/zoom entirely so the real mouse
                // events (which SDL maps to the primary finger) drive drawing normally.
                if (activeFingers >= 3) {
                    needsRedraw = true;
                } else {
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
                } // end 2-finger branch
            }

            // Reset gesture tracking when fingers lift
            if (e.type == SDL_FINGERUP && activeFingers <= 1) {
                multiGestureActive = false;
                pinchActive        = false;
                if (activeFingers == 0) {
                    viewScrolling   = false;
                    tapConsumed     = false;  // ensure stale tapConsumed never blocks a real click
                }
            }

            int cX, cY;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT
                && !multiGestureActive) {
                if (tapConsumed) { tapConsumed = false; continue; }
                viewScrolling = false;
                // Canvas edge resize handles take priority
                if (canvasResizer.onMouseDown(e.button.x, e.button.y, canvasW, canvasH)) {
                    needsRedraw = true; continue;
                }
                if (toolbar.onMouseDown(e.button.x, e.button.y)) { needsRedraw = true; continue; }
                if (toolbar.inToolbar(e.button.x, e.button.y)) { toolbar.notifyClickOutside(); continue; }
                toolbar.notifyClickOutside();  // revert resize fields if focused
                getCanvasCoords(e.button.x, e.button.y, &cX, &cY);

                if (currentType == ToolType::SELECT) {
                    auto* st = static_cast<SelectTool*>(currentTool.get());
                    if (st->isSelectionActive() && !st->isHit(cX, cY)) {
                        bool dirty = st->isDirty();
                        withCanvas([&]{ st->deactivate(renderer); });
                        if (dirty) { saveState(undoStack); }
                        // Fall through — onMouseDown below starts a new rubber-band selection
                    }
                }

                if (currentType == ToolType::RESIZE) {
                    auto* rt = static_cast<ResizeTool*>(currentTool.get());
                    if (!rt->isHit(cX, cY)) {
                        bool renders = rt->willRender();
                        withCanvas([&]{ rt->deactivate(renderer); });
                        if (renders) saveState(undoStack);
                        currentTool.reset(); // already saved; skip setTool's deactivate+save
                        setTool(originalType);
                        // Fall through so this click also starts drawing the next shape
                    }
                }

                withCanvas([&]{ currentTool->onMouseDown(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });
                if (currentType == ToolType::FILL) saveState(undoStack);
                needsRedraw = true; overlayDirty = true;
            }

            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT
                && !multiGestureActive) {
                if (canvasResizer.isDragging()) {
                    int newW, newH, ox = 0, oy = 0;
                    bool lock = toolbar.getEffectiveLockAspect();
                    toolbar.setShiftLockAspect(false);  // clear shift override on release
                    if (canvasResizer.onMouseUp(e.button.x, e.button.y, canvasW, canvasH, newW, newH, ox, oy, lock))
                        resizeCanvas(newW, newH, toolbar.getResizeScaleMode(), ox, oy);
                    else
                        toolbar.syncCanvasSize(canvasW, canvasH);  // restore if no actual resize
                    showResizePreview = false;
                    needsRedraw = true; continue;
                }
                toolbar.onMouseUp(e.button.x, e.button.y);
                getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
                bool changed = false;
                withCanvas([&]{ changed = currentTool->onMouseUp(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });
                if (changed && currentType != ToolType::SELECT && currentType != ToolType::RESIZE)
                    saveState(undoStack);
                needsRedraw = true; overlayDirty = true;
            }

            if (e.type == SDL_MOUSEMOTION && !multiGestureActive) {
                if (canvasResizer.isDragging()) {
                    bool lock = toolbar.getEffectiveLockAspect();
                    canvasResizer.onMouseMove(e.motion.x, e.motion.y, previewW, previewH, previewOriginX, previewOriginY, lock);
                    showResizePreview = true;
                    toolbar.syncCanvasSize(previewW, previewH);  // live-update size text while dragging
                    needsRedraw = true; continue;
                }
                viewScrolling = false;
                if (toolbar.onMouseMotion(e.motion.x, e.motion.y)) { needsRedraw = true; continue; }
                getCanvasCoords(e.motion.x, e.motion.y, &cX, &cY);
                withCanvas([&]{ currentTool->onMouseMove(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });

                // Clear redo history the moment the user starts dragging a
                // selection or resize handle — any future redo would be stale.
                if (currentType == ToolType::SELECT || currentType == ToolType::RESIZE) {
                    if (static_cast<TransformTool*>(currentTool.get())->isMutating())
                        redoStack.clear();
                }

                needsRedraw = true; overlayDirty = true;
            }
        }

        // Poll toolbar for a committed canvas resize (Enter key in text field)
        {
            auto req = toolbar.getResizeRequest();
            if (req.pending) {
                resizeCanvas(req.w, req.h, req.scale);
                needsRedraw = true;
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
            // Sync cachedBrushSize/cachedColor before redrawing the overlay
            // so brush-size changes take effect immediately.
            currentTool->onPreviewRender(renderer, toolbar.brushSize, toolbar.brushColor);
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

        // Checkerboard behind canvas. Tiles are sized in canvas pixels (cs=8) so
        // the pattern scales and pans with the canvas exactly. Each tile maps to
        // cs*(vf.w/canvasW) window pixels, drawn with float rects so tile edges
        // align precisely to canvas pixel boundaries at any zoom level.
        {
            // Clip to the canvas rect — ceil origin so checkerboard never bleeds
            // outside; canvas texture composites on top via BLENDMODE_BLEND.
            SDL_Rect cbClip = {
                (int)std::ceil(vf.x),
                (int)std::ceil(vf.y),
                (int)std::floor(vf.x + vf.w) - (int)std::ceil(vf.x),
                (int)std::floor(vf.y + vf.h) - (int)std::ceil(vf.y)
            };
            SDL_RenderSetClipRect(renderer, &cbClip);

            // cs = tile size in canvas pixels
            const int cs = 8;
            float tileW = vf.w / canvasW * cs;  // tile size in window pixels (float)
            float tileH = vf.h / canvasH * cs;

            if (tileW > 0.f && tileH > 0.f) {
                // First tile whose top-left is <= vf.x / vf.y
                int startCol = (int)std::floor((vf.x - vf.x) / tileW); // always 0
                int startRow = 0;
                // Number of tiles needed to cover the canvas width/height
                int numCols = (int)std::ceil((float)canvasW / cs) + 1;
                int numRows = (int)std::ceil((float)canvasH / cs) + 1;

                for (int row = 0; row < numRows; ++row) {
                    for (int col = 0; col < numCols; ++col) {
                        bool light = ((col + row) % 2) == 0;
                        SDL_SetRenderDrawColor(renderer,
                            light ? 200 : 190, light ? 200 : 190, light ? 200 : 190, 255);
                        SDL_FRect cell = {
                            vf.x + col * tileW,
                            vf.y + row * tileH,
                            tileW,
                            tileH
                        };
                        SDL_RenderFillRectF(renderer, &cell);
                    }
                }
            }
            SDL_RenderSetClipRect(renderer, nullptr);
        }

        // Render the canvas at the exact float viewport position.
        // Use SDL's clip rect to restrict drawing to the visible window area.
        // The old approach (manually computing a shifted src rect with floor/ceil
        // expansion) caused a sub-pixel mismatch: the expanded dst origin didn't
        // match vf, so pixels landed in different positions than getCanvasCoords
        // expected. Using SDL_RenderSetClipRect keeps the mapping exact.
        {
            int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
            SDL_Rect clip = { Toolbar::TB_W, 0, winW - Toolbar::TB_W, winH };
            SDL_RenderSetClipRect(renderer, &clip);
            SDL_RenderCopyF(renderer, canvas, nullptr, &vf);
            if (hasOverlay) SDL_RenderCopyF(renderer, overlay, nullptr, &vf);
            SDL_RenderSetClipRect(renderer, nullptr);
        }

        // 3. Tool preview
        currentTool->onPreviewRender(renderer, toolbar.brushSize, toolbar.brushColor);

        // 4. Canvas edge resize handles (window-space, outside canvas texture)
        // Hide while the current tool is actively drawing, or while a select/resize
        // tool is moving or dragging a handle.
        bool toolBusy = currentTool && (
            currentTool->isActive() ||
            ((currentType == ToolType::SELECT || currentType == ToolType::RESIZE) &&
             static_cast<TransformTool*>(currentTool.get())->isMutating())
        );
        if (!toolBusy)
            canvasResizer.draw(renderer, canvasW, canvasH);

        // 5. Ghost outline while dragging a canvas handle
        if (showResizePreview && canvasResizer.isDragging() && previewW > 0 && previewH > 0) {
            // Map previewW/H through the same viewport scale as the canvas.
            SDL_FRect vf2 = getViewportF();
            float scX = vf2.w / canvasW;
            float scY = vf2.h / canvasH;
            int wx1, wy1;
            getWindowCoords(0, 0, &wx1, &wy1);
            wx1 += (int)(previewOriginX * scX);
            wy1 += (int)(previewOriginY * scY);
            SDL_Rect ghost = { wx1, wy1, (int)(previewW * scX), (int)(previewH * scY) };
            SDL_SetRenderDrawColor(renderer, 70, 130, 220, 200);
            SDL_RenderDrawRect(renderer, &ghost);
            SDL_Rect ghost2 = { ghost.x + 1, ghost.y + 1, ghost.w - 2, ghost.h - 2 };
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
            SDL_RenderDrawRect(renderer, &ghost2);
        }

        // 6. Toolbar
        toolbar.draw();

        SDL_RenderPresent(renderer);
    }
}

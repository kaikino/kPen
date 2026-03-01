#define _USE_MATH_DEFINES
#include "kPen.h"
#include "DrawingUtils.h"
#include "CanvasResizer.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include "menu/MacMenu.h"

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
    cursorManager.init();  // must be after SDL_CreateWindow
    MacMenu::install();    // native macOS menu bar (no-op on other platforms)

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
    setTool(ToolType::SELECT);

    // When the user picks a color while a selection is floating, fill it immediately.
    // The selection texture is updated in-place; undo is handled naturally when the
    // selection is committed (deactivate saves state just like any other move/resize).
    toolbar.onColorChanged = [this](SDL_Color color) {
        if (currentType != ToolType::SELECT) return;
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (!st || !st->isSelectionActive()) return;
        st->fillWithColor(renderer, color);
    };

    savedStateId = nextStateSerial;  // pre-mark: saveState will assign this serial, so title shows clean
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

void kPen::commitActiveTool() {
    if (currentTool) {
        withCanvas([&]{ currentTool->deactivate(renderer); });
        setTool(originalType);
    }
}

void kPen::resetViewAndGestureState() {
    zoom = 1.f;
    zoomTarget = 1.f;
    panX = 0.f;
    panY = 0.f;
    viewScrolling      = false;
    viewScrollRawX     = 0.f;
    viewScrollRawY     = 0.f;
    viewScrollRawZoom  = 0.f;
    multiGestureActive = false;
    pinchActive        = false;
    activeFingers      = 0;
    tapPending         = false;
}

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

    // Lerp zoom toward zoomTarget. When pinching use the pivot stored when 2nd finger landed
    // (twoFingerPivot); else use mouse. That way pan→lift→zoom uses a stable reset pivot.
    {
        float clamped = std::max(MIN_ZOOM, std::min(MAX_ZOOM, zoomTarget));
        float diff = clamped - zoom;
        int pivotX, pivotY;
        if (pinchActive && twoFingerPivotSet) {
            pivotX = (int)twoFingerPivotX;
            pivotY = (int)twoFingerPivotY;
        } else {
            SDL_GetMouseState(&pivotX, &pivotY);
        }
        if (std::abs(diff) > 0.0002f) {
            zoomAround(zoom + diff * k, pivotX, pivotY);
            animating = true;
        } else if (zoom != clamped) {
            zoomAround(clamped, pivotX, pivotY);
        }
    }

    // Apply mouse wheel velocity accumulation (fast scroll = more accum = more pan, then coast)
    SDL_Rect fit = getFitViewport();
    float zW = fit.w * zoom;
    float zH = fit.h * zoom;
    float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
    float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;
    if (std::abs(wheelAccumX) > 0.01f || std::abs(wheelAccumY) > 0.01f) {
        const float applyFactor = 0.45f;
        const float decay = 0.88f;
        panX += wheelAccumX * applyFactor;
        panY += wheelAccumY * applyFactor;
        panX = std::max(-maxPanX, std::min(maxPanX, panX));
        panY = std::max(-maxPanY, std::min(maxPanY, panY));
        wheelAccumX *= decay;
        wheelAccumY *= decay;
        animating = true;
    }

    if (viewScrolling) return animating;

    auto snapAxis = [&](float& pan, float maxPan) {
        float lo = -maxPan, hi = maxPan;
        if (pan < lo)      { pan += (lo - pan) * k; if (std::abs(pan-lo)<0.5f) pan=lo; else animating=true; }
        else if (pan > hi) { pan += (hi - pan) * k; if (std::abs(pan-hi)<0.5f) pan=hi; else animating=true; }
    };
    snapAxis(panX, maxPanX);
    snapAxis(panY, maxPanY);

    return animating;
}

void kPen::addPanDelta(float winDx, float winDy) {
    SDL_Rect fit = getFitViewport();
    float zW = fit.w * zoom;
    float zH = fit.h * zoom;
    float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
    float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;
    panX = std::max(-maxPanX, std::min(maxPanX, panX + winDx));
    panY = std::max(-maxPanY, std::min(maxPanY, panY + winDy));
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

bool kPen::getScrollbarRects(int winW, int winH, SDL_Rect* trackV, SDL_Rect* thumbV, SDL_Rect* trackH, SDL_Rect* thumbH, bool* hasV, bool* hasH) {
    const int SB_SZ = 8;
    const int THUMB_MIN = 24;
    SDL_Rect fit = getFitViewport();
    float zW = fit.w * zoom;
    float zH = fit.h * zoom;
    float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
    float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;

    *hasV = (zH > (float)fit.h && maxPanY > 0.f);
    *hasH = (zW > (float)fit.w && maxPanX > 0.f);
    if (!*hasV && !*hasH) return false;

    if (*hasV) {
        float thumbRatio = (float)fit.h / zH;
        int thumbH = (int)(fit.h * thumbRatio);
        if (thumbH < THUMB_MIN) thumbH = THUMB_MIN;
        if (thumbH > fit.h) thumbH = fit.h;
        float range = (float)(fit.h - thumbH);
        float norm = (panY + maxPanY) / (2.f * maxPanY);
        if (norm < 0.f) norm = 0.f;
        if (norm > 1.f) norm = 1.f;
        int thumbY = fit.y + (int)(norm * range + 0.5f);
        trackV->x = winW - SB_SZ;
        trackV->y = fit.y;
        trackV->w = SB_SZ;
        trackV->h = fit.h;
        thumbV->x = winW - SB_SZ;
        thumbV->y = thumbY;
        thumbV->w = SB_SZ;
        thumbV->h = thumbH;
    }
    if (*hasH) {
        float thumbRatio = (float)fit.w / zW;
        int thumbW = (int)(fit.w * thumbRatio);
        if (thumbW < THUMB_MIN) thumbW = THUMB_MIN;
        if (thumbW > fit.w) thumbW = fit.w;
        float range = (float)(fit.w - thumbW);
        float norm = (panX + maxPanX) / (2.f * maxPanX);
        if (norm < 0.f) norm = 0.f;
        if (norm > 1.f) norm = 1.f;
        int thumbX = fit.x + (int)(norm * range + 0.5f);
        trackH->x = fit.x;
        trackH->y = winH - SB_SZ;
        trackH->w = fit.w;
        trackH->h = SB_SZ;
        thumbH->x = thumbX;
        thumbH->y = winH - SB_SZ;
        thumbH->w = thumbW;
        thumbH->h = SB_SZ;
    }
    return true;
}

// ── Tool management ───────────────────────────────────────────────────────────

// Helper: set render target to canvas, run f, restore to nullptr
template<typename F> void kPen::withCanvas(F f) {
    SDL_SetRenderTarget(renderer, canvas); f(); SDL_SetRenderTarget(renderer, nullptr);
}

void kPen::setTool(ToolType t) {
    handToggledOn = false;  // clicking a tool or switching tool turns off hand
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
        case ToolType::ERASER: currentTool = std::make_unique<EraserTool>(this, toolbar.squareEraser); break;
        case ToolType::LINE:   currentTool = std::make_unique<ShapeTool>(this, ToolType::LINE,   cb, false); break;
        case ToolType::RECT:   currentTool = std::make_unique<ShapeTool>(this, ToolType::RECT,   cb, toolbar.fillRect); break;
        case ToolType::CIRCLE: currentTool = std::make_unique<ShapeTool>(this, ToolType::CIRCLE, cb, toolbar.fillCircle); break;
        case ToolType::SELECT: currentTool = std::make_unique<SelectTool>(this); break;
        case ToolType::FILL:   currentTool = std::make_unique<FillTool>(this); break;
        case ToolType::PICK: {
            auto pickCb = [this](SDL_Color picked) {
                // If a custom swatch is selected, update it with the picked color.
                // If a preset swatch is selected, deselect it.
                // Always apply the picked color as the active brush color.
                if (toolbar.selectedCustomSlot >= 0)
                    toolbar.customColors[toolbar.selectedCustomSlot] = picked;
                toolbar.selectedPresetSlot = -1;
                toolbar.brushColor = picked;
                Toolbar::rgbToHsv(picked, toolbar.hue, toolbar.sat, toolbar.val);
            };
            currentTool = std::make_unique<PickTool>(this, pickCb);
            break;
        }
        case ToolType::RESIZE: break; // only created via activateResizeTool
        case ToolType::HAND: break;    // hand is space/handToggledOn only, no tool switch
    }
}

void kPen::activateResizeTool(ToolType shapeType, SDL_Rect bounds, SDL_Rect origBounds,
                               int sx, int sy, int ex, int ey,
                               int /*brushSize*/, SDL_Color /*color*/, bool filled) {
    currentType = toolbar.currentType = ToolType::RESIZE;
    currentTool = std::make_unique<ResizeTool>(this, shapeType, bounds, origBounds,
                                               sx, sy, ex, ey, &toolbar.brushSize, &toolbar.brushColor, filled);
}

// ── Undo ──────────────────────────────────────────────────────────────────────

void kPen::updateWindowTitle() {
    std::string base = currentFilePath.empty() ? "kPen" : [&]() -> std::string {
        auto s = currentFilePath.rfind('/');
        if (s == std::string::npos) s = currentFilePath.rfind('\\');
        return std::string("kPen — ") + (s != std::string::npos
               ? currentFilePath.substr(s + 1) : currentFilePath);
    }();
    SDL_SetWindowTitle(window,
        hasUnsavedChanges() ? (base + " •").c_str() : base.c_str());
}

void kPen::saveState(std::vector<CanvasState>& stack) {
    CanvasState s;
    s.w = canvasW; s.h = canvasH;
    s.pixels.resize(canvasW * canvasH);
    withCanvas([&]{ SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s.pixels.data(), canvasW * 4); });
    if (&stack == &undoStack) s.serial = nextStateSerial++;
    stack.push_back(std::move(s));
    if (&stack == &undoStack) { redoStack.clear(); updateWindowTitle(); }
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
        // The state we just snapshotted into redoStack is the same content as
        // the current undoStack top — carry its serial so redo can match savedStateId.
        redoStack.back().serial = undoStack.back().serial;
        undoStack.pop_back();
        applyState(undoStack.back());
    }
    updateWindowTitle();
}

void kPen::redo() {
    if (!redoStack.empty()) {
        applyState(redoStack.back());
        undoStack.push_back(std::move(redoStack.back()));
        redoStack.pop_back();
    }
    updateWindowTitle();
}

// ── Canvas resize ─────────────────────────────────────────────────────────────

void kPen::resizeCanvas(int newW, int newH, bool scaleContent, int originX, int originY) {
    newW = std::max(1, std::min(16384, newW));
    newH = std::max(1, std::min(16384, newH));
    if (newW == canvasW && newH == canvasH) return;

    // Commit any active tool so its pixels are stamped onto the canvas before
    // we snapshot it. We do NOT call saveState — the top-of-stack refresh below
    // captures any committed tool pixels without adding an extra undo entry.
    commitActiveTool();

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
    newState.serial = nextStateSerial++;
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

    // Expand canvas if the pasted image is larger (never scale — always crop/pad).
    if (w > canvasW || h > canvasH) {
        int newW = std::max(canvasW, w);
        int newH = std::max(canvasH, h);
        resizeCanvas(newW, newH, /*scaleContent=*/false);
    }

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

// ── File I/O ────────────────────────────────────────────

#include "menu/tinyfiledialogs.h"

// Guard: macOS fires both SDL_USEREVENT (menu) and SDL_KEYDOWN for Cmd+S.
static bool gDialogOpen = false;

static void resetCursorForDialog() {
    // Call the native macOS API directly — SDL_SetCursor isn't flushed to the
    // OS before the blocking dialog takes over, so the tool cursor would remain.
    MacMenu::useArrowCursor();
}

// After a blocking native dialog returns, macOS has queued both the
// SDL_USEREVENT (from the menu bar) and the SDL_KEYDOWN (Cmd+key) that
// triggered the same action.  Flushing them here prevents the second
// event from re-opening the dialog once gDialogOpen is cleared.
// We also re-apply the arrow cursor: macOS restores SDL's last-known
// cursor when the window regains focus, overwriting the pre-dialog reset.
static void postDialogCleanup() {
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_USEREVENT);
    // Re-apply the arrow natively: macOS restores SDL's last-known cursor
    // when the window regains focus, overwriting the pre-dialog reset.
    MacMenu::useArrowCursor();
}

static std::string nativeSaveDialog(const std::string& defaultPath) {
    if (gDialogOpen) return "";
    gDialogOpen = true;
    resetCursorForDialog();
    const char* filters[] = { "*.png", "*.jpg", "*.jpeg" };
    const char* result = tinyfd_saveFileDialog(
        "Save image", defaultPath.empty() ? "untitled.png" : defaultPath.c_str(),
        3, filters, "Image files (PNG, JPEG)");
    gDialogOpen = false;
    postDialogCleanup();
    return result ? result : "";
}

static std::string nativeOpenDialog() {
    if (gDialogOpen) return "";
    gDialogOpen = true;
    resetCursorForDialog();
    const char* filters[] = { "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.gif" };
    const char* result = tinyfd_openFileDialog(
        "Open image", "", 5, filters, "Image files", 0);
    gDialogOpen = false;
    postDialogCleanup();
    return result ? result : "";
}

bool kPen::promptSaveIfNeeded() {
    if (!hasUnsavedChanges()) return true;
    if (gDialogOpen) return true;
    gDialogOpen = true;
    resetCursorForDialog();
    int choice = tinyfd_messageBox(
        "Unsaved changes",
        "You have unsaved changes. Save before continuing?",
        "yesnocancel", "question", 1);
    gDialogOpen = false;
    postDialogCleanup();
    if (choice == 0) return false;
    if (choice == 1) { doSave(false); if (hasUnsavedChanges()) return false; }
    return true;
}

void kPen::doSave(bool forceSaveAs) {
    commitActiveTool();

    std::string path = (!forceSaveAs && !currentFilePath.empty()) ? currentFilePath : "";
    if (path.empty()) {
        path = nativeSaveDialog(currentFilePath);
        if (path.empty()) return;  // user cancelled
    }

    // Read canvas pixels
    std::vector<uint32_t> pixels(canvasW * canvasH);
    withCanvas([&]{
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                             pixels.data(), canvasW * 4);
    });

    // Encode and write
    bool ok = false;
    auto lower = [](std::string s){ for (auto& c : s) c = (char)tolower(c); return s; };
    std::string ext;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) ext = lower(path.substr(dot));

    if (ext == ".jpg" || ext == ".jpeg") {
        auto bytes = DrawingUtils::encodeJPEG(pixels.data(), canvasW, canvasH);
        if (!bytes.empty()) {
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fwrite(bytes.data(), 1, bytes.size(), f); fclose(f); ok = true; }
        }
    } else {
        // Default to PNG
        if (ext != ".png") path += ".png";
        auto bytes = DrawingUtils::encodePNG(pixels.data(), canvasW, canvasH);
        if (!bytes.empty()) {
            FILE* f = fopen(path.c_str(), "wb");
            if (f) { fwrite(bytes.data(), 1, bytes.size(), f); fclose(f); ok = true; }
        }
    }

    if (ok) {
        currentFilePath  = path;
        savedStateId = undoStack.back().serial;
        updateWindowTitle();
    } else {
        tinyfd_messageBox("Save failed", ("Could not write to:\n" + path).c_str(),
                          "ok", "error", 1);
    }
}

void kPen::doOpen() {
    std::string path = nativeOpenDialog();
    if (path.empty()) return;

    // Read and decode the file
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    std::vector<uint8_t> raw(sz);
    fread(raw.data(), 1, sz, f);
    fclose(f);

    int iw = 0, ih = 0;
    auto pixels = DrawingUtils::decodeImage(raw.data(), (int)sz, iw, ih);
    if (pixels.empty() || iw <= 0 || ih <= 0) {
        tinyfd_messageBox("Open failed", ("Could not read image:\n" + path).c_str(),
                          "ok", "error", 1);
        return;
    }

    commitActiveTool();

    // Reset history and replace canvas
    undoStack.clear(); redoStack.clear();
    resizeCanvas(iw, ih, /*scaleContent=*/false);  // creates new textures
    if (undoStack.empty()) saveState(undoStack);  // same-size: resizeCanvas returned early

    // Upload pixels (resizeCanvas left canvas transparent; overwrite with image)
    SDL_UpdateTexture(canvas, nullptr, pixels.data(), iw * 4);
    undoStack.back().pixels = pixels;

    currentFilePath = path;
    savedStateId = undoStack.back().serial;
    updateWindowTitle();
    resetViewAndGestureState();
}

// ── Run loop ──────────────────────────────────────────────────────────────────

// ── dispatchCommand ───────────────────────────────────────────────────────────
// Single source of truth for all menu-driven actions.
// Called from the SDL_USEREVENT handler (macOS native menu) and, on
// Windows/Linux, synthesised from SDL_KEYDOWN modifier shortcuts.

void kPen::dispatchCommand(int code, bool& running, bool& needsRedraw, bool& overlayDirty) {
    switch (code) {
        case MacMenu::FILE_NEW:
            if (promptSaveIfNeeded()) {
                commitActiveTool();
                undoStack.clear(); redoStack.clear();
                currentFilePath.clear();
                withCanvas([&]{
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                    SDL_RenderClear(renderer);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                });
                resizeCanvas(1200, 800, false);
                if (undoStack.empty()) saveState(undoStack);  // same-size: resizeCanvas returned early
                savedStateId = undoStack.back().serial;
                updateWindowTitle();
                resetViewAndGestureState();
                needsRedraw = true;
            }
            break;
        case MacMenu::FILE_OPEN:
            if (promptSaveIfNeeded()) { doOpen(); needsRedraw = true; }
            break;
        case MacMenu::FILE_SAVE:    doSave(currentFilePath.empty()); needsRedraw = true; break;
        case MacMenu::FILE_SAVE_AS: doSave(true); needsRedraw = true; break;
        case MacMenu::FILE_CLOSE:
        case MacMenu::QUIT:
            if (promptSaveIfNeeded()) { running = false; }
            break;
        case MacMenu::EDIT_UNDO:  undo(); needsRedraw = true; break;
        case MacMenu::EDIT_REDO:  redo(); needsRedraw = true; break;
        case MacMenu::EDIT_CUT:
            copySelectionToClipboard(); deleteSelection(); needsRedraw = true; break;
        case MacMenu::EDIT_COPY:
            copySelectionToClipboard(); needsRedraw = true; break;
        case MacMenu::EDIT_PASTE:
            pasteFromClipboard(); needsRedraw = true; overlayDirty = true; break;
        case MacMenu::EDIT_SELECT_ALL: {
            setTool(ToolType::SELECT);
            auto* st = static_cast<SelectTool*>(currentTool.get());
            SDL_Rect all = {0, 0, canvasW, canvasH};
            std::vector<uint32_t> px(canvasW * canvasH);
            withCanvas([&]{ SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), canvasW*4); });
            SDL_Texture* selTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, canvasW, canvasH);
            if (selTex) {
                SDL_SetTextureBlendMode(selTex, SDL_BLENDMODE_BLEND);
                void* tp; int pitch;
                if (SDL_LockTexture(selTex, nullptr, &tp, &pitch) == 0) {
                    for (int row = 0; row < canvasH; ++row)
                        memcpy((uint8_t*)tp + row*pitch, px.data() + row*canvasW, canvasW*4);
                    SDL_UnlockTexture(selTex);
                }
                withCanvas([&]{ SDL_SetRenderDrawColor(renderer,0,0,0,0); SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE); SDL_RenderFillRect(renderer,nullptr); SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND); });
                st->activateWithTexture(selTex, all);
            }
            currentType = toolbar.currentType = ToolType::SELECT;
            needsRedraw = true; overlayDirty = true;
            break;
        }
        default: break;
    }
}

void kPen::processEvent(SDL_Event& e, bool& running, bool& needsRedraw, bool& overlayDirty) {
    if (e.type == SDL_QUIT) { handleQuit(running); return; }
    if (e.type == SDL_USEREVENT) { handleUserEvent(e, running, needsRedraw, overlayDirty); return; }
    if (e.type == SDL_TEXTINPUT) { handleTextInput(e, needsRedraw); return; }
    if (e.type == SDL_KEYDOWN) { handleKeyDown(e, running, needsRedraw, overlayDirty); return; }
    if (e.type == SDL_KEYUP) { handleKeyUp(e, needsRedraw); return; }
    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) { handleWindowEvent(e, needsRedraw); return; }
    if (e.type == SDL_MOUSEWHEEL) { handleMouseWheel(e, needsRedraw, overlayDirty); return; }
    if (e.type == SDL_FINGERDOWN) { handleFingerDown(e); return; }
    if (e.type == SDL_FINGERUP) { handleFingerUp(e, needsRedraw, overlayDirty); return; }
    if (e.type == SDL_FINGERMOTION) { handleFingerMotion(e); return; }
    if (e.type == SDL_MULTIGESTURE) { handleMultiGesture(e, needsRedraw); return; }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !multiGestureActive) { handleMouseButtonDown(e, needsRedraw, overlayDirty); return; }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && !multiGestureActive) { handleMouseButtonUp(e, needsRedraw, overlayDirty); return; }
    if (e.type == SDL_MOUSEMOTION && !multiGestureActive) { handleMouseMotion(e, needsRedraw, overlayDirty); return; }
}

void kPen::handleQuit(bool& running) {
    if (promptSaveIfNeeded()) { running = false; }
}

void kPen::handleUserEvent(SDL_Event& e, bool& running, bool& needsRedraw, bool& overlayDirty) {
    dispatchCommand(e.user.code, running, needsRedraw, overlayDirty);
}

void kPen::handleTextInput(SDL_Event& e, bool& needsRedraw) {
    if (toolbar.onTextInput(e.text.text)) { needsRedraw = true; }
}

void kPen::handleKeyDown(SDL_Event& e, bool& running, bool& needsRedraw, bool& overlayDirty) {
    // Track shift for aspect-lock override
    if (e.key.keysym.sym == SDLK_LSHIFT || e.key.keysym.sym == SDLK_RSHIFT) {
        shiftHeld = true;
        if (canvasResizer.isDragging()) {
            toolbar.setShiftLockAspect(true);
            needsRedraw = true;
        }
    }

    // Let toolbar consume resize field keys first
    if (toolbar.onResizeKey(e.key.keysym.sym)) { needsRedraw = true; return; }

    // First tool-key press while hand is toggled on only disables hand (no tool switch)
    bool handKeyConsumed = false;
    if (handToggledOn && !(e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))) {
        switch (e.key.keysym.sym) {
            case SDLK_b: case SDLK_l: case SDLK_r: case SDLK_e: case SDLK_f: case SDLK_i:
            case SDLK_s: case SDLK_o: case SDLK_h:
                handToggledOn = false;
                needsRedraw = true;
                handKeyConsumed = true;
                break;
            default: break;
        }
    }
    if (handKeyConsumed) return;

    switch (e.key.keysym.sym) {
        case SDLK_b:
            if (originalType == ToolType::BRUSH)  toolbar.squareBrush = !toolbar.squareBrush;
            setTool(ToolType::BRUSH);  needsRedraw = true; break;
        case SDLK_l: setTool(ToolType::LINE);   needsRedraw = true; break;
        case SDLK_r:
            if (originalType == ToolType::RECT)   toolbar.fillRect     = !toolbar.fillRect;
            setTool(ToolType::RECT);   needsRedraw = true; break;
        case SDLK_e:
            if (originalType == ToolType::ERASER) toolbar.squareEraser = !toolbar.squareEraser;
            setTool(ToolType::ERASER); needsRedraw = true; break;
        case SDLK_f: setTool(ToolType::FILL);   needsRedraw = true; break;
        case SDLK_i: setTool(ToolType::PICK);   needsRedraw = true; break;
        case SDLK_h:
            if (!(e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))) {
                handToggledOn = !handToggledOn;
                needsRedraw = true;
            }
            break;
        case SDLK_SPACE: spaceHeld = true; break;
        case SDLK_BACKSPACE:
        case SDLK_DELETE:
            deleteSelection();
            needsRedraw = true;
            break;
        case SDLK_UP:
            toolbar.brushSize = std::min(99, toolbar.brushSize + 1);
            toolbar.syncBrushSize();
            needsRedraw = true;
            if (currentTool->hasOverlayContent()) overlayDirty = true;
            break;
        case SDLK_DOWN:
            toolbar.brushSize = std::max(1, toolbar.brushSize - 1);
            toolbar.syncBrushSize();
            needsRedraw = true;
            if (currentTool->hasOverlayContent()) overlayDirty = true;
            break;
        case SDLK_s:
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
#ifndef __APPLE__
                bool saveAs = (e.key.keysym.mod & KMOD_SHIFT) != 0 || currentFilePath.empty();
                dispatchCommand(saveAs ? MacMenu::FILE_SAVE_AS : MacMenu::FILE_SAVE,
                                running, needsRedraw, overlayDirty);
#endif
                break;
            }
            setTool(ToolType::SELECT); needsRedraw = true; break;
        case SDLK_o:
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
#ifndef __APPLE__
                dispatchCommand(MacMenu::FILE_OPEN, running, needsRedraw, overlayDirty);
#endif
                break;
            }
            if (originalType == ToolType::CIRCLE) toolbar.fillCircle = !toolbar.fillCircle;
            setTool(ToolType::CIRCLE); needsRedraw = true; break;
        case SDLK_n:
#ifndef __APPLE__
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))
                dispatchCommand(MacMenu::FILE_NEW, running, needsRedraw, overlayDirty);
#endif
            break;
        case SDLK_z:
#ifndef __APPLE__
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
                bool isRedo = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                dispatchCommand(isRedo ? MacMenu::EDIT_REDO : MacMenu::EDIT_UNDO,
                                running, needsRedraw, overlayDirty);
            }
#endif
            break;
        case SDLK_y:
#ifndef __APPLE__
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))
                dispatchCommand(MacMenu::EDIT_REDO, running, needsRedraw, overlayDirty);
#endif
            break;
        case SDLK_c:
        case SDLK_x:
#ifndef __APPLE__
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
                dispatchCommand(e.key.keysym.sym == SDLK_x
                                ? MacMenu::EDIT_CUT : MacMenu::EDIT_COPY,
                                running, needsRedraw, overlayDirty);
            }
#endif
            break;
        case SDLK_v:
#ifndef __APPLE__
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))
                dispatchCommand(MacMenu::EDIT_PASTE, running, needsRedraw, overlayDirty);
#endif
            break;
        case SDLK_a:
#ifndef __APPLE__
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL))
                dispatchCommand(MacMenu::EDIT_SELECT_ALL, running, needsRedraw, overlayDirty);
#endif
            break;
        case SDLK_0:
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
                zoom = 1.f; zoomTarget = 1.f; panX = 0.f; panY = 0.f;
                needsRedraw = true;
            }
            break;
    }
}

void kPen::handleKeyUp(SDL_Event& e, bool& needsRedraw) {
    if (e.key.keysym.sym == SDLK_LSHIFT || e.key.keysym.sym == SDLK_RSHIFT) {
        shiftHeld = false;
        toolbar.setShiftLockAspect(false);
        if (canvasResizer.isDragging()) needsRedraw = true;
    }
    if (e.key.keysym.sym == SDLK_SPACE) {
        spaceHeld = false;
        handToggledOn = false;
    }
}

void kPen::handleWindowEvent(SDL_Event& /* e */, bool& needsRedraw) {
    needsRedraw = true;
}

void kPen::handleMouseWheel(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
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
        if (currentTool->hasOverlayContent()) overlayDirty = true;
    } else if (toolbar.inToolbar(mx, my)) {
    } else if (SDL_GetModState() & (KMOD_GUI | KMOD_CTRL)) {
        onCanvasScroll(mx, my, 0.f, precY, true);
        needsRedraw = true;
    } else if (multiGestureActive) {
    } else {
        float dx, dy;
        if (SDL_GetModState() & KMOD_SHIFT) {
            float scrollAmount = (std::abs(precY) > std::abs(precX)) ? precY : (-precX);
            dx = scrollAmount * 4.2f;
            dy = 0.f;
        } else {
            dx = -precX * 4.2f;
            dy = precY * 4.2f;
        }
        wheelAccumX += dx;
        wheelAccumY += dy;
        const float cap = 400.f;
        wheelAccumX = std::max(-cap, std::min(cap, wheelAccumX));
        wheelAccumY = std::max(-cap, std::min(cap, wheelAccumY));
        viewScrolling = true;
        if (SDL_GetModState() & KMOD_SHIFT)
            scrollWheelWasVertical = false;
        else
            scrollWheelWasVertical = std::abs(precY) >= std::abs(precX);
        needsRedraw = true;
    }
}

void kPen::handleFingerDown(SDL_Event& e) {
    activeFingers++;
    if (activeFingers == 3 && multiGestureActive) {
        threeFingerPanMode = true;
        multiGestureActive = false;
        pinchActive        = false;
    }

    int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
    if (activeFingers == 2) {
        tapFingerId    = e.tfinger.fingerId;
        tapDownX       = e.tfinger.x * winW;
        tapDownY       = e.tfinger.y * winH;
        tapDownTime    = e.tfinger.timestamp;
        tapPending     = true;
        tapSawGesture  = multiGestureActive;
        gestureNeedsRecenter = true;
        pinchActive          = false;
        zoomPriorityEvents   = 3;
        int mx, my; SDL_GetMouseState(&mx, &my);
        twoFingerPivotX   = std::max(0.f, std::min((float)winW, (mx + e.tfinger.x * winW) * 0.5f));
        twoFingerPivotY   = std::max(0.f, std::min((float)winH, (my + e.tfinger.y * winH) * 0.5f));
        twoFingerPivotSet = true;
    } else {
        tapPending = false;
    }
}

void kPen::handleFingerUp(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
    int prevFingers = activeFingers;
    activeFingers = std::max(0, activeFingers - 1);
    if (prevFingers >= 3 && activeFingers == 2) {
        multiGestureActive = false;
        pinchActive        = false;
    }
    if (prevFingers == 2 && activeFingers == 1 && multiGestureActive) {
        multiGestureActive   = false;
        gestureNeedsRecenter = true;
        pinchActive          = false;
        zoomPriorityEvents   = 3;
    }
    if (activeFingers == 0) {
        threeFingerPanMode = false;
    }

    if (tapPending && e.tfinger.fingerId == tapFingerId) {
        tapPending = false;
        int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
        float upX = e.tfinger.x * winW;
        float upY = e.tfinger.y * winH;
        float dx = upX - tapDownX, dy = upY - tapDownY;
        Uint32 dt = e.tfinger.timestamp - tapDownTime;
        if (dt < 300 && dx*dx + dy*dy < 100.f && tapSawGesture && !pinchActive) {
            int tx = (int)upX, ty = (int)upY;

            if (toolbar.inToolbar(tx, ty)) {
                if (toolbar.onMouseDown(tx, ty)) {
                    toolbar.onMouseUp(tx, ty);
                    tapConsumed = true;
                    needsRedraw = true;
                }
            } else {
                toolbar.notifyClickOutside();
                tapConsumed = true;
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
                bool tapChanged = false;
                withCanvas([&]{ tapChanged = currentTool->onMouseUp(tapCX, tapCY, renderer, toolbar.brushSize, toolbar.brushColor); });
                if (tapChanged && currentType != ToolType::SELECT && currentType != ToolType::RESIZE)
                    saveState(undoStack);
                needsRedraw = true; overlayDirty = true;
            }
        }
    }

    if (activeFingers == 0) {
        multiGestureActive   = false;
        pinchActive          = false;
        gestureNeedsRecenter = false;
        zoomPriorityEvents   = 0;
        twoFingerPivotX      = 0.f;
        twoFingerPivotY      = 0.f;
        twoFingerPivotSet    = false;
        viewScrolling        = false;
        tapConsumed          = false;
    }
}

void kPen::handleFingerMotion(SDL_Event& e) {
    if (tapPending && e.tfinger.fingerId == tapFingerId) {
        int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
        float mx = e.tfinger.x * winW - tapDownX;
        float my = e.tfinger.y * winH - tapDownY;
        if (mx*mx + my*my > 100.f) tapPending = false;
    }
}

void kPen::handleMultiGesture(SDL_Event& e, bool& needsRedraw) {
    if (tapPending) { tapSawGesture = true; }
    if (activeFingers < 2) {
        activeFingers        = 2;
        gestureNeedsRecenter = true;
        zoomPriorityEvents   = 3;
        pinchActive          = false;
    }
    if (activeFingers >= 3 && !threeFingerPanMode) {
        needsRedraw = true;
    } else {
        int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
        float cx = e.mgesture.x * winW;
        float cy = e.mgesture.y * winH;

        int mx, my; SDL_GetMouseState(&mx, &my);
        bool overToolbar = toolbar.inToolbar(mx, my);
        bool ctrlHeld    = (SDL_GetModState() & (KMOD_GUI | KMOD_CTRL)) != 0;

        bool zoomEligible = !overToolbar && !ctrlHeld;
        bool pinchDetected = std::abs(e.mgesture.dDist) > 0.00008f;
        bool inZoomPriority = (zoomPriorityEvents > 0);
        if (zoomEligible && (pinchDetected || inZoomPriority)) {
            if (!pinchActive) {
                pinchBaseZoom = zoom;
                pinchRawDist  = 0.f;
                pinchActive   = true;
                viewScrolling = true;
                if (pinchDetected) tapPending = false;
            }
            pinchRawDist += e.mgesture.dDist * 6.f;
            float rawZoom = pinchBaseZoom * expf(pinchRawDist);
            zoomTarget = std::max(MIN_ZOOM, std::min(MAX_ZOOM, rawZoom));
        }

        if (multiGestureActive && !gestureNeedsRecenter && zoomPriorityEvents == 0 && !overToolbar && !ctrlHeld) {
            float dx = cx - lastGestureCX, dy = cy - lastGestureCY;
            panX += dx;
            panY += dy;
            viewScrolling = true;
            scrollWheelWasVertical = std::abs(dy) >= std::abs(dx);
        }
        if (zoomPriorityEvents > 0) zoomPriorityEvents--;
        gestureNeedsRecenter = false;
        lastGestureCX      = cx;
        lastGestureCY      = cy;
        if (!multiGestureActive && currentTool && currentTool->isActive()) {
            int mx, my; SDL_GetMouseState(&mx, &my);
            int fx, fy; getCanvasCoords(mx, my, &fx, &fy);
            bool strokeChanged = false;
            withCanvas([&]{ strokeChanged = currentTool->onMouseUp(fx, fy, renderer, toolbar.brushSize, toolbar.brushColor); });
            if (strokeChanged && currentType != ToolType::SELECT && currentType != ToolType::RESIZE)
                saveState(undoStack);
        }
        multiGestureActive = true;

        needsRedraw = true;
    }
}

void kPen::handleMouseButtonDown(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
    if (tapConsumed) { tapConsumed = false; return; }
    viewScrolling = false;
    if (!toolbar.inToolbar(e.button.x, e.button.y))
        toolbar.notifyClickOutside();
    {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_Rect trackV, thumbV, trackH, thumbH;
        bool hasV, hasH;
        if (getScrollbarRects(winW, winH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH)) {
            int mx = e.button.x, my = e.button.y;
            SDL_Point pt = { mx, my };
            if (hasV && SDL_PointInRect(&pt, &trackV)) {
                SDL_Rect fit = getFitViewport();
                float zH = fit.h * zoom;
                float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;
                float range = (float)(fit.h - thumbV.h);
                if (SDL_PointInRect(&pt, &thumbV)) {
                    scrollbarDragOffsetY = my - thumbV.y;
                    scrollbarDragV = true;
                } else {
                    int thumbY;
                    if (my < thumbV.y) {
                        thumbY = my;
                        scrollbarDragOffsetY = 0;
                    } else {
                        thumbY = my - thumbV.h;
                        scrollbarDragOffsetY = thumbV.h;
                    }
                    if (thumbY < fit.y) thumbY = fit.y;
                    if (thumbY > fit.y + fit.h - thumbV.h) thumbY = fit.y + fit.h - thumbV.h;
                    if (range > 0.f)
                        panY = (float)(thumbY - fit.y) / range * 2.f * maxPanY - maxPanY;
                    scrollbarDragV = true;
                }
                needsRedraw = true; return;
            }
            if (hasH && SDL_PointInRect(&pt, &trackH)) {
                SDL_Rect fit = getFitViewport();
                float zW = fit.w * zoom;
                float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
                float range = (float)(fit.w - thumbH.w);
                if (SDL_PointInRect(&pt, &thumbH)) {
                    scrollbarDragOffsetX = mx - thumbH.x;
                    scrollbarDragH = true;
                } else {
                    int thumbX;
                    if (mx < thumbH.x) {
                        thumbX = mx;
                        scrollbarDragOffsetX = 0;
                    } else {
                        thumbX = mx - thumbH.w;
                        scrollbarDragOffsetX = thumbH.w;
                    }
                    if (thumbX < fit.x) thumbX = fit.x;
                    if (thumbX > fit.x + fit.w - thumbH.w) thumbX = fit.x + fit.w - thumbH.w;
                    if (range > 0.f)
                        panX = (float)(thumbX - fit.x) / range * 2.f * maxPanX - maxPanX;
                    scrollbarDragH = true;
                }
                needsRedraw = true; return;
            }
        }
    }
    if (canvasResizer.onMouseDown(e.button.x, e.button.y, canvasW, canvasH)) {
        needsRedraw = true; return;
    }
    if (toolbar.onMouseDown(e.button.x, e.button.y)) { needsRedraw = true; return; }
    if (toolbar.inToolbar(e.button.x, e.button.y)) { return; }
    if (spaceHeld || handToggledOn) {
        handPanning = true;
        handPanStartWinX = e.button.x;
        handPanStartWinY = e.button.y;
        handPanStartPanX = panX;
        handPanStartPanY = panY;
        needsRedraw = true;
        return;
    }
    int cX, cY;
    getCanvasCoords(e.button.x, e.button.y, &cX, &cY);

    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive() && !st->isHit(cX, cY)) {
            bool dirty = st->isDirty();
            withCanvas([&]{ st->deactivate(renderer); });
            if (dirty) { saveState(undoStack); }
        }
    }

    if (currentType == ToolType::RESIZE) {
        auto* rt = static_cast<ResizeTool*>(currentTool.get());
        if (!rt->isHit(cX, cY)) {
            bool renders = rt->willRender();
            withCanvas([&]{ rt->deactivate(renderer); });
            if (renders) saveState(undoStack);
            currentTool.reset();
            setTool(originalType);
        }
    }

    withCanvas([&]{ currentTool->onMouseDown(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });
    if (currentType == ToolType::FILL) saveState(undoStack);
    needsRedraw = true; overlayDirty = true;
}

void kPen::handleMouseButtonUp(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
    if (scrollbarDragV || scrollbarDragH) {
        scrollbarDragV = false;
        scrollbarDragH = false;
        needsRedraw = true;
        return;
    }
    if (handPanning) {
        handPanning = false;
        needsRedraw = true;
        return;
    }
    if (canvasResizer.isDragging()) {
        int newW, newH, ox = 0, oy = 0;
        bool lock = toolbar.getEffectiveLockAspect();
        toolbar.setShiftLockAspect(false);
        if (canvasResizer.onMouseUp(e.button.x, e.button.y, canvasW, canvasH, newW, newH, ox, oy, lock))
            resizeCanvas(newW, newH, toolbar.getResizeScaleMode(), ox, oy);
        else
            toolbar.syncCanvasSize(canvasW, canvasH);
        showResizePreview = false;
        needsRedraw = true; return;
    }
    toolbar.onMouseUp(e.button.x, e.button.y);
    int cX, cY;
    getCanvasCoords(e.button.x, e.button.y, &cX, &cY);
    bool changed = false;
    withCanvas([&]{ changed = currentTool->onMouseUp(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });
    if (changed && currentType != ToolType::SELECT && currentType != ToolType::RESIZE)
        saveState(undoStack);
    needsRedraw = true; overlayDirty = true;
}

void kPen::handleMouseMotion(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
    if (scrollbarDragV) {
        SDL_Rect fit = getFitViewport();
        float zH = fit.h * zoom;
        float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;
        SDL_Rect trackV, thumbV, trackH, thumbH;
        bool hasV, hasH;
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        getScrollbarRects(winW, winH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH);
        int my = e.motion.y;
        int thumbY = my - scrollbarDragOffsetY;
        if (thumbY < fit.y) thumbY = fit.y;
        if (thumbY > fit.y + fit.h - thumbV.h) thumbY = fit.y + fit.h - thumbV.h;
        float range = (float)(fit.h - thumbV.h);
        if (range > 0.f)
            panY = (float)(thumbY - fit.y) / range * 2.f * maxPanY - maxPanY;
        needsRedraw = true; return;
    }
    if (scrollbarDragH) {
        SDL_Rect fit = getFitViewport();
        float zW = fit.w * zoom;
        float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
        SDL_Rect trackV, thumbV, trackH, thumbH;
        bool hasV, hasH;
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        getScrollbarRects(winW, winH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH);
        int mx = e.motion.x;
        int thumbX = mx - scrollbarDragOffsetX;
        if (thumbX < fit.x) thumbX = fit.x;
        if (thumbX > fit.x + fit.w - thumbH.w) thumbX = fit.x + fit.w - thumbH.w;
        float range = (float)(fit.w - thumbH.w);
        if (range > 0.f)
            panX = (float)(thumbX - fit.x) / range * 2.f * maxPanX - maxPanX;
        needsRedraw = true; return;
    }
    if (canvasResizer.isDragging()) {
        bool lock = toolbar.getEffectiveLockAspect();
        canvasResizer.onMouseMove(e.motion.x, e.motion.y, previewW, previewH, previewOriginX, previewOriginY, lock);
        showResizePreview = true;
        toolbar.syncCanvasSize(previewW, previewH);
        needsRedraw = true; return;
    }
    if (handPanning) {
        addPanDelta((float)(e.motion.x - handPanStartWinX), (float)(e.motion.y - handPanStartWinY));
        handPanStartWinX = e.motion.x;
        handPanStartWinY = e.motion.y;
        needsRedraw = true;
        return;
    }
    viewScrolling = false;
    if (toolbar.onMouseMotion(e.motion.x, e.motion.y)) { needsRedraw = true; overlayDirty = true; return; }
    int cX, cY;
    getCanvasCoords(e.motion.x, e.motion.y, &cX, &cY);
    withCanvas([&]{ currentTool->onMouseMove(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });

    if (currentType == ToolType::SELECT || currentType == ToolType::RESIZE) {
        if (static_cast<TransformTool*>(currentTool.get())->isMutating())
            redoStack.clear();
    }

    needsRedraw = true; overlayDirty = true;
}

void kPen::tickScrollbarFade(bool& needsRedraw) {
    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    SDL_Rect fit = getFitViewport();
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    bool hoverView = (mx >= fit.x && mx < fit.x + fit.w && my >= fit.y && my < fit.y + fit.h);
    SDL_Rect trackV, thumbV, trackH, thumbH;
    bool hasV, hasH;
    getScrollbarRects(winW, winH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH);
    SDL_Point pt = { mx, my };
    bool hoverV = hasV && (SDL_PointInRect(&pt, &trackV) || SDL_PointInRect(&pt, &thumbV));
    bool hoverH = hasH && (SDL_PointInRect(&pt, &trackH) || SDL_PointInRect(&pt, &thumbH));
    bool wantVisibleV = hasV && (scrollbarDragV || hoverV || (viewScrolling && scrollWheelWasVertical) || handPanning);
    bool wantVisibleH = hasH && (scrollbarDragH || hoverH || (viewScrolling && !scrollWheelWasVertical) || handPanning);
    const float SB_FADE_IN = 0.22f, SB_FADE_OUT = 0.028f;
    if (wantVisibleV)
        scrollbarAlphaV = std::min(1.f, scrollbarAlphaV + SB_FADE_IN);
    else
        scrollbarAlphaV = std::max(0.f, scrollbarAlphaV - SB_FADE_OUT);
    if (wantVisibleH)
        scrollbarAlphaH = std::min(1.f, scrollbarAlphaH + SB_FADE_IN);
    else
        scrollbarAlphaH = std::max(0.f, scrollbarAlphaH - SB_FADE_OUT);
    if ((scrollbarAlphaV > 0.001f && scrollbarAlphaV < 0.999f) || (scrollbarAlphaH > 0.001f && scrollbarAlphaH < 0.999f))
        needsRedraw = true;
}

void kPen::updateCursor(bool& /* needsRedraw */, bool& /* overlayDirty */) {
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    bool actAsEraser = (currentType == ToolType::ERASER) ||
                       (currentType == ToolType::BRUSH && toolbar.brushColor.a == 0);
    bool handActive = spaceHeld || handToggledOn;
    ToolType cursorType = (handActive || handPanning) ? ToolType::HAND
                            : (actAsEraser ? ToolType::ERASER : currentType);
    bool cursorSquare = (currentType == ToolType::ERASER) ? toolbar.squareEraser : toolbar.squareBrush;
    SDL_Rect vp = getViewport();
    bool overCanvas = (mx >= vp.x && mx < vp.x + vp.w && my >= vp.y && my < vp.y + vp.h);
    bool inToolbar  = toolbar.inToolbar(mx, my);
    if (inToolbar) {
        if (toolbar.isInteractive(mx, my))
            cursorManager.forceSetCursor(cursorManager.getHandCursor());
        else
            cursorManager.forceSetCursor(cursorManager.getArrowCursor());
    } else {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_Rect trackV, thumbV, trackH, thumbH;
        bool hasV, hasH;
        bool overScrollbar = false;
        if (getScrollbarRects(winW, winH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH)) {
            SDL_Point pt = { mx, my };
            overScrollbar = (hasV && (SDL_PointInRect(&pt, &trackV) || SDL_PointInRect(&pt, &thumbV)))
                || (hasH && (SDL_PointInRect(&pt, &trackH) || SDL_PointInRect(&pt, &thumbH)));
        }
        if (overScrollbar && !(handActive || handPanning)) {
            cursorManager.forceSetCursor(cursorManager.getArrowCursor());
        } else {
            bool nearHandle = canvasResizer.isDragging() ||
                (!overCanvas &&
                 canvasResizer.hitTest(mx, my, canvasW, canvasH) != CanvasResizer::Handle::NONE);
            SDL_Color pickHoverColor = { 0, 0, 0, 0 };
            const SDL_Color* pickHoverColorPtr = nullptr;
            if (cursorType == ToolType::PICK && overCanvas) {
                int cX, cY;
                getCanvasCoords(mx, my, &cX, &cY);
                int px = std::max(0, std::min(canvasW - 1, cX));
                int py = std::max(0, std::min(canvasH - 1, cY));
                withCanvas([&] {
                    SDL_Rect r = { px, py, 1, 1 };
                    Uint32 pixel = 0;
                    SDL_RenderReadPixels(renderer, &r, SDL_PIXELFORMAT_ARGB8888, &pixel, 4);
                    pickHoverColor.a = (pixel >> 24) & 0xFF;
                    pickHoverColor.r = (pixel >> 16) & 0xFF;
                    pickHoverColor.g = (pixel >>  8) & 0xFF;
                    pickHoverColor.b =  pixel        & 0xFF;
                });
                pickHoverColorPtr = &pickHoverColor;
            }
            cursorManager.update(this, cursorType, originalType, currentTool.get(),
                                 toolbar.brushSize, cursorSquare,
                                 toolbar.brushColor,
                                 mx, my, false, overCanvas, nearHandle,
                                 &canvasResizer, canvasW, canvasH,
                                 pickHoverColorPtr);
        }
    }
}

void kPen::renderFrame(bool& overlayDirty) {
    bool hasOverlay = currentTool->hasOverlayContent();
    if (overlayDirty) {
        currentTool->onPreviewRender(renderer, toolbar.brushSize, toolbar.brushColor);
        SDL_SetRenderTarget(renderer, overlay);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        if (hasOverlay) currentTool->onOverlayRender(renderer);
        SDL_SetRenderTarget(renderer, nullptr);
        overlayDirty = false;
    }

    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderClear(renderer);
    SDL_FRect vf = getViewportF();

    {
        SDL_Rect cbClip = {
            (int)std::ceil(vf.x),
            (int)std::ceil(vf.y),
            (int)std::floor(vf.x + vf.w) - (int)std::ceil(vf.x),
            (int)std::floor(vf.y + vf.h) - (int)std::ceil(vf.y)
        };
        SDL_RenderSetClipRect(renderer, &cbClip);
        const int cs = 8;
        float tileW = vf.w / canvasW * cs;
        float tileH = vf.h / canvasH * cs;
        if (tileW > 0.f && tileH > 0.f) {
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

    {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_Rect clip = { Toolbar::TB_W, 0, winW - Toolbar::TB_W, winH };
        SDL_RenderSetClipRect(renderer, &clip);
        SDL_RenderCopyF(renderer, canvas, nullptr, &vf);
        if (hasOverlay) SDL_RenderCopyF(renderer, overlay, nullptr, &vf);
        SDL_RenderSetClipRect(renderer, nullptr);
    }

    currentTool->onPreviewRender(renderer, toolbar.brushSize, toolbar.brushColor);

    bool toolBusy = currentTool && (
        currentTool->isActive() ||
        ((currentType == ToolType::SELECT || currentType == ToolType::RESIZE) &&
         static_cast<TransformTool*>(currentTool.get())->isMutating())
    );
    if (!toolBusy)
        canvasResizer.draw(renderer, canvasW, canvasH);

    // Scrollbars
    {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_Rect trackV, thumbV, trackH, thumbH;
        bool hasV, hasH;
        getScrollbarRects(winW, winH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH);
        if (scrollbarAlphaV > 0.001f || scrollbarAlphaH > 0.001f) {
            const Uint8 TRACK_R = 60, TRACK_G = 60, TRACK_B = 60, TRACK_A = 220;
            const Uint8 THUMB_R = 120, THUMB_G = 120, THUMB_B = 120, THUMB_A = 240;
            Uint8 trackAV = (Uint8)(TRACK_A * scrollbarAlphaV + 0.5f);
            Uint8 thumbAV = (Uint8)(THUMB_A * scrollbarAlphaV + 0.5f);
            Uint8 trackAH = (Uint8)(TRACK_A * scrollbarAlphaH + 0.5f);
            Uint8 thumbAH = (Uint8)(THUMB_A * scrollbarAlphaH + 0.5f);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            const int SB_RADIUS = 4;
            auto fillRoundedRect = [this, SB_RADIUS](const SDL_Rect* rect) {
                int r = SB_RADIUS;
                if (r <= 0 || rect->w < 2*r || rect->h < 2*r) {
                    SDL_RenderFillRect(renderer, rect);
                    return;
                }
                SDL_Rect c = { rect->x + r, rect->y + r, rect->w - 2*r, rect->h - 2*r };
                SDL_RenderFillRect(renderer, &c);
                SDL_Rect left  = { rect->x, rect->y + r, r, rect->h - 2*r };
                SDL_Rect right = { rect->x + rect->w - r, rect->y + r, r, rect->h - 2*r };
                SDL_Rect top   = { rect->x + r, rect->y, rect->w - 2*r, r };
                SDL_Rect bot   = { rect->x + r, rect->y + rect->h - r, rect->w - 2*r, r };
                SDL_RenderFillRect(renderer, &left);
                SDL_RenderFillRect(renderer, &right);
                SDL_RenderFillRect(renderer, &top);
                SDL_RenderFillRect(renderer, &bot);
                int cx_tl = rect->x + r, cy_tl = rect->y + r;
                int cx_tr = rect->x + rect->w - r, cy_tr = rect->y + r;
                int cx_bl = rect->x + r, cy_bl = rect->y + rect->h - r;
                int cx_br = rect->x + rect->w - r, cy_br = rect->y + rect->h - r;
                const int r2 = r * r;
                for (int dy = 0; dy <= r; dy++)
                    for (int dx = 0; dx <= r; dx++)
                        if (dx*dx + dy*dy <= r2) {
                            SDL_RenderDrawPoint(renderer, cx_tl - dx, cy_tl - dy);
                            SDL_RenderDrawPoint(renderer, cx_tr + dx, cy_tr - dy);
                            SDL_RenderDrawPoint(renderer, cx_bl - dx, cy_bl + dy);
                            SDL_RenderDrawPoint(renderer, cx_br + dx, cy_br + dy);
                        }
            };
            if (hasV && scrollbarAlphaV > 0.001f) {
                SDL_SetRenderDrawColor(renderer, TRACK_R, TRACK_G, TRACK_B, trackAV);
                fillRoundedRect(&trackV);
                SDL_SetRenderDrawColor(renderer, THUMB_R, THUMB_G, THUMB_B, thumbAV);
                fillRoundedRect(&thumbV);
            }
            if (hasH && scrollbarAlphaH > 0.001f) {
                SDL_SetRenderDrawColor(renderer, TRACK_R, TRACK_G, TRACK_B, trackAH);
                fillRoundedRect(&trackH);
                SDL_SetRenderDrawColor(renderer, THUMB_R, THUMB_G, THUMB_B, thumbAH);
                fillRoundedRect(&thumbH);
            }
        }
    }

    if (showResizePreview && canvasResizer.isDragging() && previewW > 0 && previewH > 0) {
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

    toolbar.draw(spaceHeld || handToggledOn);
    SDL_RenderPresent(renderer);
}

void kPen::run() {
    bool running      = true;
    bool needsRedraw  = true;
    bool overlayDirty = false;
    SDL_Event e;

    SDL_EventState(SDL_MULTIGESTURE, SDL_ENABLE);

    while (running) {
        while (SDL_PollEvent(&e)) {
            processEvent(e, running, needsRedraw, overlayDirty);
        }

        // Poll toolbar for a committed canvas resize (Enter key in text field)
        {
            auto req = toolbar.getResizeRequest();
            if (req.pending) {
                resizeCanvas(req.w, req.h, req.scale);
                needsRedraw = true;
            }
        }

        updateCursor(needsRedraw, overlayDirty);
        tickScrollbarFade(needsRedraw);

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

        renderFrame(overlayDirty);
    }
}

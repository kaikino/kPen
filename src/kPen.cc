#define _USE_MATH_DEFINES
#include "kPen.h"
#include "DrawingUtils.h"
#include "CanvasResizer.h"
#include "ViewController.h"
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
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC);
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
    SDL_GetWindowSize(window, &winW_, &winH_);
    setTool(ToolType::SELECT);

    // When the user picks a color while a selection is floating, fill it immediately.
    // Transparent is excluded so picking transparent only sets the brush for future drawing.
    toolbar.onColorChanged = [this](SDL_Color color) {
        if (toolbar.currentType != ToolType::SELECT) return;
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (!st || !st->isSelectionActive()) return;
        if (color.a == 0) return; // don't fill selection with transparent
        st->fillWithColor(renderer, color);
    };

    toolbar.onColorDroppedOnCanvas = [this](SDL_Color c) {
        commitActiveTool();
        saveState();
        withCanvas([&]{
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        });
        saveState();
        savedStateId = undoManager.currentSerial();
        updateWindowTitle();
    };

    saveState();
    savedStateId = undoManager.currentSerial();
    updateWindowTitle();
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
    return view_.getViewport(winW_, winH_, canvasW, canvasH);
}

SDL_FRect kPen::getViewportF() {
    return view_.getViewportF(winW_, winH_, canvasW, canvasH);
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
    view_.reset();
    resetGestureState();
}

void kPen::resetGestureState() {
    view_.endScrollGesture();
    view_.endScrollbarDrag();

    multiGestureActive   = false;
    lastGestureCX        = 0.f;
    lastGestureCY        = 0.f;
    activeFingers        = 0;
    gestureNeedsRecenter = false;
    zoomPriorityEvents   = 0;

    tapPending         = false;
    tapSawGesture      = false;
    tapConsumed        = false;
    threeFingerPanMode = false;

    spaceHeld         = false;
    handToggledOn     = false;
    handPanning       = false;
    handPanStartWinX  = 0;
    handPanStartWinY  = 0;

    pinchActive        = false;
    pinchBaseZoom      = 1.f;
    pinchRawDist       = 0.f;
    twoFingerPivotX   = 0.f;
    twoFingerPivotY   = 0.f;
    twoFingerPivotSet = false;

    lastMotionCX = -1;
    lastMotionCY = -1;
    lastPickCX   = -1;
    lastPickCY   = -1;
    lastPickHoverColor = { 0, 0, 0, 0 };

    showResizePreview = false;
    previewW = 0;
    previewH = 0;
    previewOriginX = 0;
    previewOriginY = 0;
    canvasResizer.cancelDrag();
    lastGestureTicks = 0;
}

bool kPen::tickView() {
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    return view_.tickView(winW_, winH_, canvasW, canvasH,
                          pinchActive, twoFingerPivotSet, twoFingerPivotX, twoFingerPivotY,
                          mx, my);
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
        if (toolbar.currentType == ToolType::RESIZE) {
            if (static_cast<ResizeTool*>(currentTool.get())->willRender())
                saveState();
        } else if (toolbar.currentType == ToolType::SELECT) {
            if (static_cast<SelectTool*>(currentTool.get())->isDirty())
                saveState();
        }
    }
    originalType = toolbar.currentType = t;
    auto cb = [this](ToolType st, SDL_Rect b, SDL_Rect ob, int sx, int sy, int ex, int ey, int bs, SDL_Color c, bool filled) {
        activateResizeTool(st, b, ob, sx, sy, ex, ey, bs, c, filled);
    };
    switch (t) {
        case ToolType::BRUSH:  currentTool = std::make_unique<BrushTool>(this, toolbar.squareBrush); break;
        case ToolType::ERASER: currentTool = std::make_unique<EraserTool>(this, toolbar.squareEraser); break;
        case ToolType::LINE:   currentTool = std::make_unique<ShapeTool>(this, ToolType::LINE,   cb, false, [this]{ saveState(); }); break;
        case ToolType::RECT:   currentTool = std::make_unique<ShapeTool>(this, ToolType::RECT,   cb, toolbar.fillRect); break;
        case ToolType::CIRCLE: currentTool = std::make_unique<ShapeTool>(this, ToolType::CIRCLE, cb, toolbar.fillCircle); break;
        case ToolType::SELECT: currentTool = std::make_unique<SelectTool>(this, toolbar.lassoSelect); break;
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
    toolbar.currentType = ToolType::RESIZE;
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

void kPen::saveState() {
    std::vector<uint32_t> pixels(static_cast<size_t>(canvasW) * canvasH);
    withCanvas([&]{ SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), canvasW * 4); });
    undoManager.pushUndo(canvasW, canvasH, pixels);
    updateWindowTitle();
}

void kPen::applyState(CanvasState& s) {
    if (toolbar.currentType == ToolType::SELECT || toolbar.currentType == ToolType::RESIZE) {
        currentTool.reset(); // prevent setTool from deactivating+saving
        setTool(originalType);
    }
    if (s.w != canvasW || s.h != canvasH) {
        SDL_Texture* newCanvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_TARGET, s.w, s.h);
        SDL_Texture* newOverlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_TARGET, s.w, s.h);
        if (!newCanvas || !newOverlay) {
            if (newCanvas) SDL_DestroyTexture(newCanvas);
            if (newOverlay) SDL_DestroyTexture(newOverlay);
            return;
        }
        SDL_DestroyTexture(canvas);
        SDL_DestroyTexture(overlay);
        canvas = newCanvas;
        overlay = newOverlay;
        canvasW = s.w;
        canvasH = s.h;
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

// Stamp the active SELECT or RESIZE tool onto redo, then restore canvas from undo top.
void kPen::stampForRedo(AbstractTool* tool) {
    CanvasState s;
    s.w = canvasW; s.h = canvasH;
    s.pixels.resize(static_cast<size_t>(canvasW) * canvasH);
    withCanvas([&]{
        tool->deactivate(renderer);
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s.pixels.data(), canvasW * 4);
    });
    undoManager.pushRedo(std::move(s));
    CanvasState* prev = undoManager.getUndoTop();
    if (prev)
        SDL_UpdateTexture(canvas, nullptr, prev->pixels.data(), canvasW * 4);
}

void kPen::undo() {
    if (toolbar.currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive()) {
            if (st->isDirty()) stampForRedo(st);
            currentTool.reset();
            setTool(originalType);
            CanvasState* top = undoManager.getUndoTop();
            if (top) applyState(*top);
            return;
        }
    }
    if (toolbar.currentType == ToolType::RESIZE) {
        currentTool.reset();
        setTool(originalType);
        CanvasState* top = undoManager.getUndoTop();
        if (top) applyState(*top);
        return;
    }
    if (undoManager.getUndoSize() > 1) {
        CanvasState current;
        current.w = canvasW; current.h = canvasH;
        current.pixels.resize(static_cast<size_t>(canvasW) * canvasH);
        withCanvas([&]{ SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, current.pixels.data(), canvasW * 4); });
        undoManager.pushRedo(std::move(current));
        undoManager.popUndo();
        CanvasState* top = undoManager.getUndoTop();
        if (top) applyState(*top);
    }
    updateWindowTitle();
}

void kPen::redo() {
    if (undoManager.redoEmpty()) return;
    CanvasState* r = undoManager.getRedoTop();
    if (!r) return;
    applyState(*r);
    undoManager.pushUndoKeepSerial(*r);
    undoManager.popRedo();
    updateWindowTitle();
}

// ── Canvas resize ─────────────────────────────────────────────────────────────

bool kPen::resizeCanvas(int newW, int newH, bool scaleContent, int originX, int originY) {
    newW = std::max(1, std::min(16384, newW));
    newH = std::max(1, std::min(16384, newH));
    if (newW == canvasW && newH == canvasH) return true;

    // Commit any active tool so its pixels are stamped onto the canvas before
    // we snapshot it. We do NOT call saveState — the top-of-stack refresh below
    // captures any committed tool pixels without adding an extra undo entry.
    commitActiveTool();

    // Read the current canvas pixels for building the new buffer.
    // We do NOT push a pre-resize state — replaceTopUndo refreshes the current
    // state. We only push the post-resize
    // state below, so one undo step correctly returns to this pre-resize state.
    std::vector<uint32_t> oldPixels(canvasW * canvasH);
    withCanvas([&]{
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                             oldPixels.data(), canvasW * 4);
    });
    // Refresh top undo entry with current pixels (commitActiveTool stamped them).
    undoManager.replaceTopUndo(canvasW, canvasH, oldPixels);

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

    SDL_Texture* newCanvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, newW, newH);
    SDL_Texture* newOverlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, newW, newH);
    if (!newCanvas || !newOverlay) {
        if (newCanvas) SDL_DestroyTexture(newCanvas);
        if (newOverlay) SDL_DestroyTexture(newOverlay);
        return false;
    }
    SDL_DestroyTexture(canvas);
    SDL_DestroyTexture(overlay);
    canvas = newCanvas;
    overlay = newOverlay;
    canvasW = newW;
    canvasH = newH;
    SDL_SetTextureBlendMode(canvas,  SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(canvas, nullptr, newPixels.data(), canvasW * 4);

    SDL_SetRenderTarget(renderer, overlay);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    // Push post-resize state; one undo restores pre-resize (replaceTopUndo above).
    undoManager.pushUndo(canvasW, canvasH, newPixels);
    toolbar.syncCanvasSize(canvasW, canvasH);
    return true;
}

// ── Clipboard / delete helpers ────────────────────────────────────────────────

// Delete the active SelectTool or ResizeTool content without stamping it back,
// saving an undo state so the deletion can be undone.
void kPen::deleteSelection() {
    if (toolbar.currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (!st->isSelectionActive()) return;
        // The canvas already has a transparent hole; save that state, then drop the floating pixels.
        saveState();
        currentTool = std::make_unique<SelectTool>(this, toolbar.lassoSelect);
    } else if (toolbar.currentType == ToolType::RESIZE) {
        // Shape was never stamped onto the canvas — just discard it.
        // No undo state needed: the canvas is unchanged from the last undo entry.
        currentTool.reset(); // skip deactivate so shape isn't drawn
        setTool(originalType);
    }
}

// Copy the pixels from the active SelectTool or ResizeTool to the OS clipboard
// as a native image (PNG on macOS, DIB+PNG on Windows) so other apps can paste it.
void kPen::copySelectionToClipboard() {
    SDL_Rect bounds = {0, 0, 0, 0};
    std::vector<uint32_t> pixels;

    if (toolbar.currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (!st->isSelectionActive()) return;
        bounds = st->getFloatingBounds();
        pixels = st->getFloatingPixels(renderer);
    } else if (toolbar.currentType == ToolType::RESIZE) {
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
    if (toolbar.currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive()) {
            withCanvas([&]{ st->deactivate(renderer); });
            if (st->isDirty()) saveState();
        }
        currentTool = std::make_unique<SelectTool>(this, toolbar.lassoSelect);
    } else if (toolbar.currentType == ToolType::RESIZE) {
        auto* rt_ = static_cast<ResizeTool*>(currentTool.get());
        bool renders_ = rt_->willRender();
        withCanvas([&]{ currentTool->deactivate(renderer); });
        if (renders_) saveState();
        currentTool = std::make_unique<SelectTool>(this, toolbar.lassoSelect);
    } else {
        setTool(ToolType::SELECT);
        currentTool = std::make_unique<SelectTool>(this, toolbar.lassoSelect);
    }
    toolbar.currentType = ToolType::SELECT;

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
    if (SDL_LockTexture(tex, nullptr, &texPixels, &pitch) != 0) {
        SDL_DestroyTexture(tex);
        return;
    }
    for (int row = 0; row < h; ++row)
        memcpy((uint8_t*)texPixels + row * pitch, pixels.data() + row * w, w * 4);
    SDL_UnlockTexture(tex);
    static_cast<SelectTool*>(currentTool.get())->activateWithTexture(tex, pasteBounds);
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
    const char* filters[] = { "*.png", "*.jpg", "*.jpeg" };
    const char* result = tinyfd_openFileDialog(
        "Open image", "", 3, filters, "Image files", 0);
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
        savedStateId = undoManager.currentSerial();
        updateWindowTitle();
    } else {
        tinyfd_messageBox("Save failed", ("Could not write to:\n" + path).c_str(),
                          "ok", "error", 1);
    }
}

void kPen::doOpen() {
    std::string path = nativeOpenDialog();
    if (path.empty()) return;

    // Reject unsupported formats
    auto lower = [](std::string s){ for (auto& c : s) c = (char)tolower(c); return s; };
    std::string ext = (path.size() >= 4) ? lower(path.substr(path.size() - 4)) : "";
    if (ext == ".gif") {
        tinyfd_messageBox("Open failed", "GIF images cannot be opened.", "ok", "error", 1);
        return;
    }
    if (ext == ".bmp") {
        tinyfd_messageBox("Open failed", "BMP images cannot be opened.", "ok", "error", 1);
        return;
    }

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

    undoManager.clear();
    if (!resizeCanvas(iw, ih, /*scaleContent=*/false)) {
        tinyfd_messageBox("Open failed", "Could not resize canvas.", "ok", "error", 1);
        return;
    }
    if (undoManager.getUndoSize() == 0) saveState();

    SDL_UpdateTexture(canvas, nullptr, pixels.data(), iw * 4);
    undoManager.setUndoTopPixels(pixels);

    currentFilePath = path;
    savedStateId = undoManager.currentSerial();
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
                undoManager.clear();
                currentFilePath.clear();
                withCanvas([&]{
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
                    SDL_RenderClear(renderer);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                });
                resizeCanvas(1200, 800, false);
                if (undoManager.getUndoSize() == 0) saveState();
                savedStateId = undoManager.currentSerial();
                updateWindowTitle();
                resetViewAndGestureState();
                needsRedraw = true;
                overlayDirty = true;
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
            withCanvas([&]{ st->commitRectSelection(renderer, canvasW, canvasH, {0, 0, canvasW, canvasH}); });
            toolbar.currentType = ToolType::SELECT;
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
    if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP ||
        e.type == SDL_FINGERMOTION || e.type == SDL_MULTIGESTURE) {
        Uint32 ts = 0;
        if (e.type == SDL_MULTIGESTURE)
            ts = e.mgesture.timestamp;
        else
            ts = e.tfinger.timestamp;
        lastGestureTicks = ts ? ts : SDL_GetTicks();
    }
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
            case SDLK_ESCAPE:
                handToggledOn = false;
                needsRedraw = true;
                handKeyConsumed = true;
                break;
            default: break;
        }
    }
    if (handKeyConsumed) return;

    switch (e.key.keysym.sym) {
        case SDLK_ESCAPE: {
            if (toolbar.currentType == ToolType::SELECT) {
                auto* st = static_cast<SelectTool*>(currentTool.get());
                if (st->isSelectionActive()) {
                    bool dirty = st->isDirty();
                    withCanvas([&]{ st->deactivate(renderer); });
                    if (dirty) saveState();
                    needsRedraw = true;
                    overlayDirty = true;
                }
            } else if (toolbar.currentType == ToolType::RESIZE) {
                auto* rt = static_cast<ResizeTool*>(currentTool.get());
                bool renders = rt->willRender();
                withCanvas([&]{ rt->deactivate(renderer); });
                if (renders) saveState();
                currentTool.reset();
                setTool(originalType);
                needsRedraw = true;
                overlayDirty = true;
            }
            break;
        }
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
        case SDLK_COMMA:  // , = brush size down
            toolbar.brushSize = std::max(1, toolbar.brushSize - 1);
            toolbar.syncBrushSize();
            needsRedraw = true;
            if (currentTool->hasOverlayContent()) overlayDirty = true;
            break;
        case SDLK_PERIOD:  // . = brush size up
            toolbar.brushSize = std::min(99, toolbar.brushSize + 1);
            toolbar.syncBrushSize();
            needsRedraw = true;
            if (currentTool->hasOverlayContent()) overlayDirty = true;
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_UP:
        case SDLK_DOWN: {
            int dx = 0, dy = 0;
            if (e.key.keysym.sym == SDLK_LEFT)  dx = -1;
            if (e.key.keysym.sym == SDLK_RIGHT) dx =  1;
            if (e.key.keysym.sym == SDLK_UP)    dy = -1;
            if (e.key.keysym.sym == SDLK_DOWN)  dy =  1;
            bool nudgeSelection = false;
            if (toolbar.currentType == ToolType::SELECT) {
                auto* st = static_cast<SelectTool*>(currentTool.get());
                if (st->isSelectionActive()) nudgeSelection = true;
            } else if (toolbar.currentType == ToolType::RESIZE) {
                nudgeSelection = true;
            }
            if (nudgeSelection) {
                // When key is held, SDL sends repeat events; move faster per repeat
                int step = e.key.repeat ? 8 : 1;
                dx *= step;
                dy *= step;
                auto* tt = static_cast<TransformTool*>(currentTool.get());
                tt->nudge(dx, dy);
                needsRedraw = true;
                overlayDirty = true;
            } else if (toolbar.onArrowKey(dx, dy)) {
                needsRedraw = true;
            }
            break;
        }
        case SDLK_s:
            if (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) {
#ifndef __APPLE__
                bool saveAs = (e.key.keysym.mod & KMOD_SHIFT) != 0 || currentFilePath.empty();
                dispatchCommand(saveAs ? MacMenu::FILE_SAVE_AS : MacMenu::FILE_SAVE,
                                running, needsRedraw, overlayDirty);
#endif
                break;
            }
            if (toolbar.currentType == ToolType::SELECT)
                toolbar.lassoSelect = !toolbar.lassoSelect;
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
                resetViewAndGestureState();
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
        view_.setScrollFromMouseWheel(false);
        view_.onCanvasScroll(mx, my, precY, true, winW_, winH_, canvasW, canvasH);
        needsRedraw = true;
    } else if (multiGestureActive) {
    } else {
        view_.setScrollFromMouseWheel(true);
        float dx, dy;
        if (SDL_GetModState() & KMOD_SHIFT) {
            float scrollAmount = (std::abs(precY) > std::abs(precX)) ? precY : (-precX);
            dx = scrollAmount * 4.2f;
            dy = 0.f;
        } else {
            dx = -precX * 4.2f;
            dy = precY * 4.2f;
        }
        view_.onWheelPan(dx, dy);
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

    if (activeFingers == 2) {
        tapFingerId    = e.tfinger.fingerId;
        tapDownX       = e.tfinger.x * winW_;
        tapDownY       = e.tfinger.y * winH_;
        tapDownTime    = e.tfinger.timestamp;
        tapPending     = true;
        tapSawGesture  = multiGestureActive;
        gestureNeedsRecenter = true;
        pinchActive          = false;
        zoomPriorityEvents   = 3;
        int mx, my; SDL_GetMouseState(&mx, &my);
        twoFingerPivotX   = std::max(0.f, std::min((float)winW_, (mx + e.tfinger.x * winW_) * 0.5f));
        twoFingerPivotY   = std::max(0.f, std::min((float)winH_, (my + e.tfinger.y * winH_) * 0.5f));
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
        float upX = e.tfinger.x * winW_;
        float upY = e.tfinger.y * winH_;
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

                if (toolbar.currentType == ToolType::RESIZE) {
                    auto* rt = static_cast<ResizeTool*>(currentTool.get());
                    if (!rt->isHit(tapCX, tapCY)) {
                        bool renders = rt->willRender();
                        withCanvas([&]{ rt->deactivate(renderer); });
                        if (renders) saveState();
                        currentTool.reset();
                        setTool(originalType);
                    }
                }
                if (toolbar.currentType == ToolType::SELECT) {
                    auto* st = static_cast<SelectTool*>(currentTool.get());
                    if (st->isSelectionActive() && !st->isHit(tapCX, tapCY)) {
                        bool dirty = st->isDirty();
                        withCanvas([&]{ st->deactivate(renderer); });
                        if (dirty) saveState();
                    }
                }

                withCanvas([&]{ currentTool->onMouseDown(tapCX, tapCY, renderer, toolbar.brushSize, toolbar.brushColor); });
                if (toolbar.currentType == ToolType::FILL) saveState();
                bool tapChanged = false;
                withCanvas([&]{ tapChanged = currentTool->onMouseUp(tapCX, tapCY, renderer, toolbar.brushSize, toolbar.brushColor); });
                if (tapChanged && toolbar.currentType != ToolType::SELECT && toolbar.currentType != ToolType::RESIZE)
                    saveState();
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
        view_.endScrollGesture();
        tapConsumed          = false;
    }
}

void kPen::handleFingerMotion(SDL_Event& e) {
    if (tapPending && e.tfinger.fingerId == tapFingerId) {
        float mx = e.tfinger.x * winW_ - tapDownX;
        float my = e.tfinger.y * winH_ - tapDownY;
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
        float cx = e.mgesture.x * winW_;
        float cy = e.mgesture.y * winH_;

        int mx, my; SDL_GetMouseState(&mx, &my);
        bool overToolbar = toolbar.inToolbar(mx, my);
        bool ctrlHeld    = (SDL_GetModState() & (KMOD_GUI | KMOD_CTRL)) != 0;

        bool zoomEligible = !overToolbar && !ctrlHeld;
        bool pinchDetected = std::abs(e.mgesture.dDist) > 0.00008f;
        bool inZoomPriority = (zoomPriorityEvents > 0);
        if (zoomEligible && (pinchDetected || inZoomPriority)) {
            if (!pinchActive) {
                pinchBaseZoom = view_.getZoom();
                pinchRawDist  = 0.f;
                pinchActive   = true;
                view_.setScrollFromMouseWheel(false);
                view_.onWheelPan(0.f, 0.f);  // set viewScrolling
                if (pinchDetected) tapPending = false;
            }
            pinchRawDist += e.mgesture.dDist * 6.f;
            float rawZoom = pinchBaseZoom * expf(pinchRawDist);
            view_.setZoomTarget(std::max(ViewController::MIN_ZOOM, std::min(ViewController::MAX_ZOOM, rawZoom)));
        }

        if (multiGestureActive && !gestureNeedsRecenter && zoomPriorityEvents == 0 && !overToolbar && !ctrlHeld) {
            float dx = cx - lastGestureCX, dy = cy - lastGestureCY;
            view_.setScrollFromMouseWheel(false);
            view_.onWheelPan(0.f, 0.f);  // show scrollbar during gesture
            view_.setScrollWheelWasVertical(std::abs(dy) >= std::abs(dx));
            view_.addPanDelta(dx, dy, winW_, winH_, canvasW, canvasH);
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
            if (strokeChanged && toolbar.currentType != ToolType::SELECT && toolbar.currentType != ToolType::RESIZE)
                saveState();
        }
        multiGestureActive = true;

        needsRedraw = true;
    }
}

void kPen::handleMouseButtonDown(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
    if (tapConsumed) { tapConsumed = false; return; }
    view_.endScrollGesture();
    if (!toolbar.inToolbar(e.button.x, e.button.y))
        toolbar.notifyClickOutside();
    {
        bool consumed = false;
        if (view_.onScrollbarMouseDown(winW_, winH_, e.button.x, e.button.y, canvasW, canvasH, &consumed)) {
            needsRedraw = true;
            return;
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
        needsRedraw = true;
        return;
    }
    int cX, cY;
    getCanvasCoords(e.button.x, e.button.y, &cX, &cY);

    if (toolbar.currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive() && !st->isHit(cX, cY)) {
            bool dirty = st->isDirty();
            withCanvas([&]{ st->deactivate(renderer); });
            if (dirty) { saveState(); }
        }
    }

    if (toolbar.currentType == ToolType::RESIZE) {
        auto* rt = static_cast<ResizeTool*>(currentTool.get());
        if (!rt->isHit(cX, cY)) {
            bool renders = rt->willRender();
            withCanvas([&]{ rt->deactivate(renderer); });
            if (renders) saveState();
            currentTool.reset();
            setTool(originalType);
        }
    }

    withCanvas([&]{ currentTool->onMouseDown(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });
    if (toolbar.currentType == ToolType::FILL) saveState();
    needsRedraw = true; overlayDirty = true;
}

void kPen::handleMouseButtonUp(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
    if (view_.getScrollbarDragV() || view_.getScrollbarDragH()) {
        view_.endScrollbarDrag();
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
    if (changed && toolbar.currentType != ToolType::SELECT && toolbar.currentType != ToolType::RESIZE)
        saveState();
    needsRedraw = true; overlayDirty = true;
}

void kPen::handleMouseMotion(SDL_Event& e, bool& needsRedraw, bool& overlayDirty) {
    if (view_.getScrollbarDragV() || view_.getScrollbarDragH()) {
        bool nr = false;
        view_.onScrollbarMouseMotion(winW_, winH_, e.motion.x, e.motion.y, canvasW, canvasH, &nr);
        if (nr) needsRedraw = true;
        return;
    }
    if (canvasResizer.isDragging()) {
        bool lock = toolbar.getEffectiveLockAspect();
        canvasResizer.onMouseMove(e.motion.x, e.motion.y, previewW, previewH, previewOriginX, previewOriginY, lock);
        showResizePreview = true;
        toolbar.syncCanvasSize(previewW, previewH);
        needsRedraw = true; return;
    }
    if (handPanning) {
        view_.addPanDelta((float)(e.motion.x - handPanStartWinX), (float)(e.motion.y - handPanStartWinY),
                          winW_, winH_, canvasW, canvasH);
        handPanStartWinX = e.motion.x;
        handPanStartWinY = e.motion.y;
        needsRedraw = true;
        return;
    }
    view_.endScrollGesture();
    if (toolbar.onMouseMotion(e.motion.x, e.motion.y)) { needsRedraw = true; overlayDirty = true; return; }
    int cX, cY;
    getCanvasCoords(e.motion.x, e.motion.y, &cX, &cY);
    withCanvas([&]{ currentTool->onMouseMove(cX, cY, renderer, toolbar.brushSize, toolbar.brushColor); });

    if (toolbar.currentType == ToolType::SELECT || toolbar.currentType == ToolType::RESIZE) {
        if (static_cast<TransformTool*>(currentTool.get())->isMutating())
            undoManager.clearRedo();
    }

    bool canvasPosChanged = (cX != lastMotionCX || cY != lastMotionCY);
    lastMotionCX = cX;
    lastMotionCY = cY;
    if (canvasPosChanged) {
        needsRedraw = true;
        overlayDirty = true;
    }
}

void kPen::tickScrollbarFade(bool& needsRedraw) {
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    view_.tickScrollbarFade(winW_, winH_, mx, my, canvasW, canvasH, handPanning, &needsRedraw);
}

void kPen::updateCursor(bool& /* needsRedraw */, bool& /* overlayDirty */) {
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    bool actAsEraser = (toolbar.currentType == ToolType::ERASER) ||
                       (toolbar.currentType == ToolType::BRUSH && toolbar.brushColor.a == 0);
    bool handActive = spaceHeld || handToggledOn;
    ToolType cursorType = (handActive || handPanning) ? ToolType::HAND
                            : (actAsEraser ? ToolType::ERASER : toolbar.currentType);
    bool cursorSquare = (toolbar.currentType == ToolType::ERASER) ? toolbar.squareEraser : toolbar.squareBrush;
    SDL_Rect vp = getViewport();
    bool overCanvas = (mx >= vp.x && mx < vp.x + vp.w && my >= vp.y && my < vp.y + vp.h);
    bool inToolbar  = toolbar.inToolbar(mx, my);
    if (inToolbar) {
        if (toolbar.isInteractive(mx, my))
            cursorManager.forceSetCursor(cursorManager.getHandCursor());
        else
            cursorManager.forceSetCursor(cursorManager.getArrowCursor());
    } else {
        bool hoverV, hoverH, hasV, hasH;
        view_.getScrollbarHover(winW_, winH_, mx, my, canvasW, canvasH, &hoverV, &hoverH, &hasV, &hasH);
        bool overScrollbar = hoverV || hoverH;
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
                if (px != lastPickCX || py != lastPickCY) {
                    withCanvas([&] {
                        SDL_Rect r = { px, py, 1, 1 };
                        Uint32 pixel = 0;
                        SDL_RenderReadPixels(renderer, &r, SDL_PIXELFORMAT_ARGB8888, &pixel, 4);
                        lastPickHoverColor.a = (pixel >> 24) & 0xFF;
                        lastPickHoverColor.r = (pixel >> 16) & 0xFF;
                        lastPickHoverColor.g = (pixel >>  8) & 0xFF;
                        lastPickHoverColor.b =  pixel        & 0xFF;
                    });
                    lastPickCX = px;
                    lastPickCY = py;
                }
                pickHoverColorPtr = &lastPickHoverColor;
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
        const int cs = 16;
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

    // Clip to the visible canvas view so we don't render into letterbox areas
    SDL_Rect viewClip = {
        (int)std::ceil(vf.x),
        (int)std::ceil(vf.y),
        (int)std::floor(vf.x + vf.w) - (int)std::ceil(vf.x),
        (int)std::floor(vf.y + vf.h) - (int)std::ceil(vf.y)
    };
    SDL_RenderSetClipRect(renderer, &viewClip);
    SDL_RenderCopyF(renderer, canvas, nullptr, &vf);
    if (hasOverlay) SDL_RenderCopyF(renderer, overlay, nullptr, &vf);
    // Line tool: draw endpoint handles in window space so they stay fixed size when zooming
    if (toolbar.currentType == ToolType::LINE) {
        auto* st = static_cast<ShapeTool*>(currentTool.get());
        if (st && st->isLineEditing()) {
            int x0, y0, x1, y1;
            st->getLineEndpoints(x0, y0, x1, y1);
            int wx0, wy0, wx1, wy1;
            getWindowCoords(x0, y0, &wx0, &wy0);
            getWindowCoords(x1, y1, &wx1, &wy1);
            const int handleRadius = 3;
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            DrawingUtils::drawFillCircle(renderer, wx0, wy0, handleRadius);
            DrawingUtils::drawFillCircle(renderer, wx1, wy1, handleRadius);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            for (int i = 0; i < 2; i++) {
                int hx = (i == 0) ? wx0 : wx1;
                int hy = (i == 0) ? wy0 : wy1;
                for (int deg = 0; deg < 360; deg += 4)
                    SDL_RenderDrawPoint(renderer, hx + (int)(handleRadius * std::cos(deg * M_PI / 180.0)), hy + (int)(handleRadius * std::sin(deg * M_PI / 180.0)));
            }
        }
    }
    currentTool->onPreviewRender(renderer, toolbar.brushSize, toolbar.brushColor);
    bool toolBusy = currentTool && (
        currentTool->isActive() ||
        ((toolbar.currentType == ToolType::SELECT || toolbar.currentType == ToolType::RESIZE) &&
         static_cast<TransformTool*>(currentTool.get())->isMutating())
    );
    if (!toolBusy)
        canvasResizer.draw(renderer, canvasW, canvasH);
    SDL_RenderSetClipRect(renderer, nullptr);

    // Scrollbars
    {
        SDL_Rect trackV, thumbV, trackH, thumbH;
        bool hasV, hasH;
        view_.getScrollbarRects(winW_, winH_, canvasW, canvasH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH);
        float scrollbarAlphaV = view_.getScrollbarAlphaV();
        float scrollbarAlphaH = view_.getScrollbarAlphaH();
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

    toolbar.draw(spaceHeld || handToggledOn, winW_, winH_);

    // Mouse coords at bottom-right (canvas coordinates); hide when mouse is out of canvas bounds
    {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        int cx, cy;
        getCanvasCoords(mx, my, &cx, &cy);
        if (cx >= 0 && cx < canvasW && cy >= 0 && cy < canvasH)
            toolbar.drawCoordDisplay(winW_, winH_, cx, cy);
    }

    // Swatch drag: show a small color preview at the cursor
    {
        SDL_Color dragColor;
        if (toolbar.getDraggingSwatchColor(&dragColor)) {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            const int size = 12;
            const int offset = 4;
            SDL_Rect preview = { mx + offset, my + offset, size, size };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 200);
            SDL_RenderDrawRect(renderer, &preview);
            Uint8 alpha = (Uint8)((int)dragColor.a * 180 / 255);
            SDL_SetRenderDrawColor(renderer, dragColor.r, dragColor.g, dragColor.b, alpha);
            SDL_Rect inner = { preview.x + 1, preview.y + 1, preview.w - 2, preview.h - 2 };
            SDL_RenderFillRect(renderer, &inner);
        }
    }

    SDL_RenderPresent(renderer);
}

void kPen::run() {
    bool running      = true;
    bool needsRedraw  = true;
    bool overlayDirty = false;
    SDL_Event e;
    Uint32 lastFrameTicks = 0;
    Uint32 lastCursorUpdateTicks = 0;
    int idleCount = 0;
    const int idleThreshold = 6;
    const int idleDelayShort = 16;
    const int idleDelayLong = 33;

    // Cap loop rate (input polling + rendering) at display refresh, max 120 fps
    int maxFps = 120;
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode = {};
    if (displayIndex >= 0 && SDL_GetCurrentDisplayMode(displayIndex, &mode) == 0 && mode.refresh_rate > 0)
        maxFps = std::min(maxFps, mode.refresh_rate);
    const Uint32 minFrameIntervalMs = (maxFps > 0) ? (1000u / (Uint32)maxFps) : 8u;

    SDL_EventState(SDL_MULTIGESTURE, SDL_ENABLE);

    while (running) {
        if (lastFrameTicks != 0) {
            Uint32 now = SDL_GetTicks();
            Uint32 elapsed = now - lastFrameTicks;
            if (elapsed < minFrameIntervalMs)
                SDL_Delay(minFrameIntervalMs - elapsed);
        }
        lastFrameTicks = SDL_GetTicks();

        SDL_GetWindowSize(window, &winW_, &winH_);
        bool hadEvent = false;
        while (SDL_PollEvent(&e)) {
            hadEvent = true;
            processEvent(e, running, needsRedraw, overlayDirty);
        }
        if (hadEvent) idleCount = 0;

        // Poll toolbar for a committed canvas resize (Enter key in text field)
        {
            auto req = toolbar.getResizeRequest();
            if (req.pending) {
                resizeCanvas(req.w, req.h, req.scale);
                needsRedraw = true;
            }
        }

        bool toolBusy = currentTool && (
            currentTool->isActive() ||
            ((toolbar.currentType == ToolType::SELECT || toolbar.currentType == ToolType::RESIZE) &&
             static_cast<TransformTool*>(currentTool.get())->isMutating())
        );

        if (toolBusy) {
            Uint32 now = SDL_GetTicks();
            if (now - lastCursorUpdateTicks >= 33)
                updateCursor(needsRedraw, overlayDirty), lastCursorUpdateTicks = now;
        } else {
            updateCursor(needsRedraw, overlayDirty);
        }

        if (view_.getScrollbarAlphaV() > 0.001f || view_.getScrollbarAlphaH() > 0.001f)
            tickScrollbarFade(needsRedraw);
        else if (view_.getViewScrolling() || handPanning)
            tickScrollbarFade(needsRedraw);
        else {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            bool hoverV, hoverH, hasV, hasH;
            view_.getScrollbarHover(winW_, winH_, mx, my, canvasW, canvasH, &hoverV, &hoverH, &hasV, &hasH);
            if (hoverV || hoverH)
                tickScrollbarFade(needsRedraw);
        }

        // If touch / gesture state has been idle for a while, force-reset it so
        // a fresh two-finger gesture always starts from a clean slate even if
        // finger-up events were missed while the app was inactive.
        if (lastGestureTicks != 0) {
            Uint32 now = SDL_GetTicks();
            const Uint32 kGestureIdleMs = 800;
            if (now - lastGestureTicks > kGestureIdleMs &&
                (activeFingers > 0 || multiGestureActive || pinchActive ||
                 tapPending || twoFingerPivotSet || threeFingerPanMode)) {
                resetGestureState();
            }
        }

        if (!needsRedraw) {
            bool ta = toolbar.tickScroll();
            bool va = tickView();
            if (ta || va) needsRedraw = true;
            else {
                idleCount++;
                lastFrameTicks = SDL_GetTicks();
                SDL_Delay(idleCount > idleThreshold ? idleDelayLong : idleDelayShort);
                continue;
            }
        } else {
            idleCount = 0;
            toolbar.tickScroll();
            tickView();
        }

        needsRedraw = false;

        renderFrame(overlayDirty);
    }
}

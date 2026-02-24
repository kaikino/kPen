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
            if (static_cast<SelectTool*>(currentTool.get())->isDirty())
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
            if (st->isDirty()) stampForRedo(st);
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

// ── Clipboard / delete helpers ────────────────────────────────────────────────

// Delete the active SelectTool or ResizeTool content without stamping it back,
// saving an undo state so the deletion can be undone.
void kPen::deleteSelection() {
    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (!st->isSelectionActive()) return;
        // The canvas already has a white hole; save that state, then drop the floating pixels.
        saveState(undoStack);
        currentTool = std::make_unique<SelectTool>(this);
    } else if (currentType == ToolType::RESIZE) {
        // Shape was never stamped onto the canvas — just discard it.
        // No undo state needed: the canvas is unchanged from the last undoStack entry.
        currentTool.reset(); // skip deactivate so shape isn't drawn
        setTool(originalType);
    }
}

// Copy the pixels from the active SelectTool or ResizeTool into the OS clipboard
// as raw ARGB8888 pixel data preceded by a small header: width(4) + height(4) + pixels.
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

    // Pack into a byte buffer: 4 bytes width, 4 bytes height, then raw ARGB pixels.
    int w = bounds.w, h = bounds.h;
    size_t headerSize = 8;
    size_t pixelBytes = (size_t)w * h * 4;
    std::vector<uint8_t> buf(headerSize + pixelBytes);
    memcpy(buf.data() + 0, &w, 4);
    memcpy(buf.data() + 4, &h, 4);
    memcpy(buf.data() + 8, pixels.data(), pixelBytes);

    // SDL_SetClipboardText expects a null-terminated string; we base64-encode to be safe.
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t* src = buf.data();
    size_t srcLen = buf.size();
    size_t outLen = ((srcLen + 2) / 3) * 4 + 1;
    std::vector<char> b64buf(outLen);
    size_t i = 0, o = 0;
    while (i < srcLen) {
        uint32_t v = (uint32_t)src[i++] << 16;
        bool b2 = i < srcLen, b3;
        if (b2) v |= (uint32_t)src[i++] << 8;
        b3 = i < srcLen;
        if (b3) v |= (uint32_t)src[i++];
        b64buf[o++] = b64[(v >> 18) & 63];
        b64buf[o++] = b64[(v >> 12) & 63];
        b64buf[o++] = b2 ? b64[(v >> 6) & 63] : '=';
        b64buf[o++] = b3 ? b64[v & 63] : '=';
    }
    b64buf[o] = '\0';

    // Prefix with a magic tag so paste can identify our data
    std::string clipText = "KPEN_PIXELS:";
    clipText += b64buf.data();
    SDL_SetClipboardText(clipText.c_str());
}

void kPen::pasteFromClipboard() {
    if (!SDL_HasClipboardText()) return;
    char* raw = SDL_GetClipboardText();
    if (!raw) return;
    std::string text(raw);
    SDL_free(raw);

    const std::string prefix = "KPEN_PIXELS:";
    if (text.rfind(prefix, 0) != 0) return;
    std::string b64str = text.substr(prefix.size());

    // Base64 decode
    static const int8_t dt[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::vector<uint8_t> decoded;
    decoded.reserve(b64str.size() * 3 / 4);
    uint32_t acc = 0; int bits = 0;
    for (char ch : b64str) {
        if (ch == '=') break;
        int8_t v = dt[(uint8_t)ch];
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; decoded.push_back((acc >> bits) & 0xFF); }
    }

    if (decoded.size() < 8) return;
    int w, h;
    memcpy(&w, decoded.data() + 0, 4);
    memcpy(&h, decoded.data() + 4, 4);
    if (w <= 0 || h <= 0 || (size_t)(w * h * 4 + 8) > decoded.size()) return;

    std::vector<uint32_t> pixels((size_t)w * h);
    memcpy(pixels.data(), decoded.data() + 8, (size_t)w * h * 4);

    // Commit any in-progress selection/resize before pasting, saving undo state once
    if (currentType == ToolType::SELECT) {
        auto* st = static_cast<SelectTool*>(currentTool.get());
        if (st->isSelectionActive()) {
            withCanvas([&]{ st->deactivate(renderer); });
            if (st->isDirty()) saveState(undoStack);
        }
        // Reset to a fresh SelectTool directly — don't go through setTool which would save again
        currentTool = std::make_unique<SelectTool>(this);
    } else if (currentType == ToolType::RESIZE) {
        withCanvas([&]{ currentTool->deactivate(renderer); });
        saveState(undoStack);
        currentTool.reset();
        currentTool = std::make_unique<SelectTool>(this);
    } else {
        setTool(ToolType::SELECT);
    }
    currentType = toolbar.currentType = ToolType::SELECT;

    // Centre the pasted content on the mouse cursor position
    int mouseWinX, mouseWinY, mouseCX, mouseCY;
    SDL_GetMouseState(&mouseWinX, &mouseWinY);
    getCanvasCoords(mouseWinX, mouseWinY, &mouseCX, &mouseCY);
    SDL_Rect pasteBounds = {
        std::max(0, std::min(CANVAS_WIDTH  - w, mouseCX - w / 2)),
        std::max(0, std::min(CANVAS_HEIGHT - h, mouseCY - h / 2)),
        w, h
    };

    // Create an SDL_Texture from the pixel data and hand it to SelectTool
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

    auto* st = static_cast<SelectTool*>(currentTool.get());
    st->activateWithTexture(tex, pasteBounds);
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
                        deleteSelection();
                        needsRedraw = true;
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
                        bool dirty = st->isDirty();
                        withCanvas([&]{ st->deactivate(renderer); });
                        if (dirty) { saveState(undoStack); }
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

        // Checkerboard behind canvas — drawn directly each frame so it always
        // aligns precisely with the float viewport edge (no 1-px cache drift).
        {
            int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
            const int cs = 12;
            // Integer pixel bounds that fully cover the float canvas rect
            int sx0 = std::max((int)std::floor(vf.x), Toolbar::TB_W);
            int sy0 = std::max((int)std::floor(vf.y), 0);
            int sx1 = std::min((int)std::ceil(vf.x + vf.w), winW);
            int sy1 = std::min((int)std::ceil(vf.y + vf.h), winH);

            if (sx1 > sx0 && sy1 > sy0) {
                // Tile origin anchored to the canvas float origin so pattern
                // shifts smoothly with pan instead of snapping by whole pixels.
                int originX = (int)std::floor(vf.x);
                int originY = (int)std::floor(vf.y);
                // First tile column/row that covers sx0, sy0
                int startTX = originX + ((sx0 - originX) / cs) * cs;
                int startTY = originY + ((sy0 - originY) / cs) * cs;
                if (startTX > sx0) startTX -= cs;
                if (startTY > sy0) startTY -= cs;

                for (int ty = startTY; ty < sy1; ty += cs) {
                    for (int tx = startTX; tx < sx1; tx += cs) {
                        int col = (tx - originX) / cs;
                        int row = (ty - originY) / cs;
                        // Handle negative tile indices correctly
                        bool light = (((col % 2) + (row % 2) + 4) % 2) == 0;
                        SDL_SetRenderDrawColor(renderer,
                            light ? 200 : 160, light ? 200 : 160, light ? 200 : 160, 255);
                        SDL_Rect cell = {
                            std::max(tx, sx0), std::max(ty, sy0),
                            std::min(tx + cs, sx1) - std::max(tx, sx0),
                            std::min(ty + cs, sy1) - std::max(ty, sy0)
                        };
                        if (cell.w > 0 && cell.h > 0)
                            SDL_RenderFillRect(renderer, &cell);
                    }
                }
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
                // Snap dst outward (floor start, ceil end) so the canvas always
                // covers full pixels and never leaves a 1-px gap at the edges.
                float dstX0 = std::floor(sx0), dstY0 = std::floor(sy0);
                float dstX1 = std::ceil(sx1),  dstY1 = std::ceil(sy1);

                // Map expanded dst back to canvas texel coords
                float scaleX = CANVAS_WIDTH  / vf.w;
                float scaleY = CANVAS_HEIGHT / vf.h;
                SDL_FRect srcF = {
                    (dstX0 - cx0) * scaleX,
                    (dstY0 - cy0) * scaleY,
                    (dstX1 - dstX0) * scaleX,
                    (dstY1 - dstY0) * scaleY
                };
                SDL_Rect src = {
                    (int)srcF.x, (int)srcF.y,
                    (int)std::ceil(srcF.w), (int)std::ceil(srcF.h)
                };
                SDL_FRect dst = { dstX0, dstY0, dstX1 - dstX0, dstY1 - dstY0 };

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

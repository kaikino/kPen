#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>
#include <cstring>

// When user picks transparent, we store this preview color for display; on commit we treat it as transparent.
static constexpr uint32_t TRANSPARENT_FILL_PREVIEW_ARGB =
    (static_cast<uint32_t>(200) << 24) | (100u << 16) | (149u << 8) | 237u;  // CornflowerBlue, same as line overlay

SelectTool::SelectTool(ICoordinateMapper* m, bool lassoMode)
    : TransformTool(m), lassoMode_(lassoMode) {}

SelectTool::~SelectTool() {
    if (selectionTexture) SDL_DestroyTexture(selectionTexture);
}

// Robust integer ray-cast: count crossings of horizontal line at py with segment (xj,yj)-(xi,yi).
static bool rayCrossesSegment(int px, int py, int xi, int yi, int xj, int yj) {
    if (yi == yj) return false;
    if (yi > yj) { std::swap(yi, yj); std::swap(xi, xj); }
    if (py <= yi || py > yj) return false;
    // intersection x of horizontal at py with segment
    int dx = xj - xi, dy = yj - yi;
    int64_t num = static_cast<int64_t>(px - xi) * dy - static_cast<int64_t>(py - yi) * dx;
    int den = dy;
    if (den < 0) { num = -num; den = -den; }
    return num < 0; // ray to +infty: segment is to the left of px
}

bool SelectTool::pointInPolygon(int px, int py, const std::vector<SDL_Point>& poly) {
    const int n = static_cast<int>(poly.size());
    if (n < 3) return false;
    int crossings = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (rayCrossesSegment(px, py, poly[i].x, poly[i].y, poly[j].x, poly[j].y))
            crossings++;
    }
    return (crossings & 1) != 0;
}

void SelectTool::commitLassoSelection(SDL_Renderer* r, int canvasW, int canvasH) {
    int minX = lassoPoints_[0].x, maxX = lassoPoints_[0].x;
    int minY = lassoPoints_[0].y, maxY = lassoPoints_[0].y;
    for (const auto& p : lassoPoints_) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    int rx = std::max(0, minX);
    int ry = std::max(0, minY);
    int rx2 = std::min(canvasW, maxX + 1);
    int ry2 = std::min(canvasH, maxY + 1);
    int rw = rx2 - rx, rh = ry2 - ry;
    if (rw <= 0 || rh <= 0) return;

    std::vector<uint32_t> canvasPixels(static_cast<size_t>(rw) * rh);
    SDL_Rect readRect = { rx, ry, rw, rh };
    SDL_RenderReadPixels(r, &readRect, SDL_PIXELFORMAT_ARGB8888, canvasPixels.data(), rw * 4);

    std::vector<uint32_t> texPixels(static_cast<size_t>(rw) * rh, 0);
    for (int py = 0; py < rh; py++) {
        for (int px = 0; px < rw; px++) {
            int cx = rx + px, cy = ry + py;
            if (pointInPolygon(cx, cy, lassoPoints_)) {
                texPixels[static_cast<size_t>(py) * rw + px] = canvasPixels[static_cast<size_t>(py) * rw + px];
            }
        }
    }

    if (selectionTexture) SDL_DestroyTexture(selectionTexture);
    selectionTexture = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_TARGET, rw, rh);
    if (!selectionTexture) {
        isDrawing = false;
        lassoPoints_.clear();
        return;
    }
    SDL_SetTextureBlendMode(selectionTexture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(selectionTexture, nullptr, texPixels.data(), rw * 4);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (int py = 0; py < rh; py++) {
        for (int px = 0; px < rw; px++) {
            int cx = rx + px, cy = ry + py;
            if (pointInPolygon(cx, cy, lassoPoints_)) {
                SDL_Rect one = { cx, cy, 1, 1 };
                SDL_RenderFillRect(r, &one);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    currentBounds = { rx, ry, rw, rh };
    rotation = 0.f;
    syncDrawCenterFromBounds();
    active = true;
    fillColorIsTransparent_ = false;
    isDrawing = false;
    lassoPoints_.clear();
}

void SelectTool::renderWithTransform(SDL_Renderer* r, const SDL_Rect& dst) const {
    SDL_RendererFlip flip = (SDL_RendererFlip)(
        (flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) |
        (flipY ? SDL_FLIP_VERTICAL   : SDL_FLIP_NONE));
    double angleDeg = std::fmod(getRotation() * 180.0 / M_PI, 360.0);
    if (angleDeg < 0.0) angleDeg += 360.0;
    float hw = dst.w * 0.5f, hh = dst.h * 0.5f;
    SDL_FRect dstF = { (float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h };
    SDL_FPoint centerF = { hw, hh };
    SDL_RenderCopyExF(r, selectionTexture, nullptr, &dstF, angleDeg, &centerF, flip);
}

void SelectTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (active && handleMouseDown(cX, cY)) return;
    AbstractTool::onMouseDown(cX, cY, r, brushSize, color);
    if (lassoMode_) {
        lassoPoints_.clear();
        lassoPoints_.push_back({ cX, cY });
    }
}

void SelectTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (!handleMouseMove(cX, cY) && isDrawing) {
        if (lassoMode_) {
            if (lassoPoints_.empty() ||
                std::abs(cX - lassoPoints_.back().x) > 2 || std::abs(cY - lassoPoints_.back().y) > 2)
                lassoPoints_.push_back({ cX, cY });
        } else {
            lastX = cX;
            lastY = cY;
        }
    }
}

bool SelectTool::onMouseUp(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (resizing != Handle::NONE || isMoving || isRotating) { handleMouseUp(); return false; }
    if (!isDrawing) { return false; }

    if (lassoMode_) {
        if (lassoPoints_.size() < 3) {
            isDrawing = false;
            lassoPoints_.clear();
            return false;
        }
        int canvasW, canvasH;
        mapper->getCanvasSize(&canvasW, &canvasH);
        commitLassoSelection(r, canvasW, canvasH);
        return true;
    }

    if (cX == startX && cY == startY) { isDrawing = false; return false; }

    currentBounds = { std::min(startX, cX), std::min(startY, cY),
                      std::max(1, std::abs(cX - startX)), std::max(1, std::abs(cY - startY)) };

    int canvasW, canvasH; mapper->getCanvasSize(&canvasW, &canvasH);
    int rx = std::max(0, currentBounds.x);
    int ry = std::max(0, currentBounds.y);
    int rx2 = std::min(canvasW, currentBounds.x + currentBounds.w);
    int ry2 = std::min(canvasH, currentBounds.y + currentBounds.h);
    int rw = rx2 - rx, rh = ry2 - ry;
    if (rw <= 0 || rh <= 0) { isDrawing = false; return false; }

    if (selectionTexture) SDL_DestroyTexture(selectionTexture);
    selectionTexture = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_TARGET, rw, rh);
    if (!selectionTexture) {
        isDrawing = false;
        return false;
    }
    SDL_SetTextureBlendMode(selectionTexture, SDL_BLENDMODE_BLEND);

    std::vector<uint32_t> pixels(rw * rh);
    SDL_Rect readRect = { rx, ry, rw, rh };
    SDL_RenderReadPixels(r, &readRect, SDL_PIXELFORMAT_ARGB8888, pixels.data(), rw * 4);
    SDL_UpdateTexture(selectionTexture, nullptr, pixels.data(), rw * 4);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderFillRect(r, &readRect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    currentBounds = { rx, ry, rw, rh };
    rotation = 0.f;
    syncDrawCenterFromBounds();

    active = true;
    isDrawing = false;
    return true;
}

void SelectTool::onOverlayRender(SDL_Renderer* r) {
    if (!active || !selectionTexture) return;
    renderWithTransform(r, currentBounds);
}

void SelectTool::onPreviewRender(SDL_Renderer* r, int /*brushSize*/, SDL_Color /*color*/) {
    if (isDrawing) {
        if (lassoMode_ && lassoPoints_.size() >= 2) {
            std::vector<SDL_Point> winPts;
            winPts.reserve(lassoPoints_.size() + 1);
            for (const auto& p : lassoPoints_) {
                int wx, wy;
                mapper->getWindowCoords(p.x, p.y, &wx, &wy);
                winPts.push_back({ wx, wy });
            }
            int mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            winPts.push_back({ mouseX, mouseY });
            DrawingUtils::drawMarchingPolyline(r, winPts.data(), static_cast<int>(winPts.size()), true);
        } else if (!lassoMode_) {
            int mouseX, mouseY; SDL_GetMouseState(&mouseX, &mouseY);
            int curX, curY;
            {
                int wx1, wy1, wx2, wy2;
                mapper->getWindowCoords(0, 0, &wx1, &wy1);
                int cw2, ch2; mapper->getCanvasSize(&cw2, &ch2);
                mapper->getWindowCoords(cw2, ch2, &wx2, &wy2);
                int vw = wx2 - wx1, vh = wy2 - wy1;
                curX = vw > 0 ? (int)std::floor((mouseX - wx1) * ((float)cw2 / vw)) : 0;
                curY = vh > 0 ? (int)std::floor((mouseY - wy1) * ((float)ch2 / vh)) : 0;
            }
            int wx1, wy1, wx2, wy2;
            mapper->getWindowCoords(std::min(startX, curX), std::min(startY, curY), &wx1, &wy1);
            mapper->getWindowCoords(std::max(startX, curX), std::max(startY, curY), &wx2, &wy2);
            SDL_Rect rect = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
            DrawingUtils::drawMarchingRect(r, &rect);
        }
    }
    if (active) drawHandles(r);
}

void SelectTool::deactivate(SDL_Renderer* r) {
    if (!active) return;
    if (selectionTexture) {
        if (fillColorIsTransparent_) {
            int w = currentBounds.w, h = currentBounds.h;
            std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
            SDL_Texture* prev = SDL_GetRenderTarget(r);
            SDL_SetRenderTarget(r, selectionTexture);
            SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);
            SDL_SetRenderTarget(r, prev);

            // Clear only canvas pixels that correspond to the transparent-fill (blue preview),
            // before we overwrite those with 0 in the texture.
            int cw, ch;
            mapper->getCanvasSize(&cw, &ch);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            for (int j = 0; j < h; j++) {
                for (int i = 0; i < w; i++) {
                    if (pixels[static_cast<size_t>(j) * w + i] != TRANSPARENT_FILL_PREVIEW_ARGB)
                        continue;
                    float px = currentBounds.x + (i + 0.5f) / (float)w * currentBounds.w;
                    float py = currentBounds.y + (j + 0.5f) / (float)h * currentBounds.h;
                    float outX, outY;
                    rotatePt(px, py, drawCenterX, drawCenterY, getRotation(), outX, outY);
                    int ix = (int)std::round(outX), iy = (int)std::round(outY);
                    if (ix >= 0 && ix < cw && iy >= 0 && iy < ch)
                        SDL_RenderDrawPoint(r, ix, iy);
                }
            }
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

            for (size_t i = 0; i < pixels.size(); i++) {
                if (pixels[i] == TRANSPARENT_FILL_PREVIEW_ARGB) pixels[i] = 0;
            }
            SDL_Texture* tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
            if (tmp) {
                SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_BLEND);
                SDL_UpdateTexture(tmp, nullptr, pixels.data(), w * 4);
                SDL_DestroyTexture(selectionTexture);
                selectionTexture = tmp;
            }
        }
        renderWithTransform(r, currentBounds);
        SDL_DestroyTexture(selectionTexture);
        selectionTexture = nullptr;
    }
    active = false;
}

bool SelectTool::isHit(int cX, int cY) const {
    return active && TransformTool::isHit(cX, cY);
}

void SelectTool::activateWithTexture(SDL_Texture* tex, SDL_Rect area) {
    if (selectionTexture) SDL_DestroyTexture(selectionTexture);
    selectionTexture = tex;
    currentBounds = area;
    rotation = 0.f;
    syncDrawCenterFromBounds();
    active = true;
    dirty  = true;
    fillColorIsTransparent_ = false;
    isMoving   = false;
    isRotating = false;
    resizing   = Handle::NONE;
    isDrawing  = false;
    lassoPoints_.clear();
}

void SelectTool::commitRectSelection(SDL_Renderer* r, int canvasW, int canvasH, SDL_Rect rect) {
    int rx = std::max(0, rect.x);
    int ry = std::max(0, rect.y);
    int rx2 = std::min(canvasW, rect.x + rect.w);
    int ry2 = std::min(canvasH, rect.y + rect.h);
    int rw = rx2 - rx, rh = ry2 - ry;
    if (rw <= 0 || rh <= 0) return;

    if (selectionTexture) SDL_DestroyTexture(selectionTexture);
    selectionTexture = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_TARGET, rw, rh);
    if (!selectionTexture) return;
    SDL_SetTextureBlendMode(selectionTexture, SDL_BLENDMODE_BLEND);

    std::vector<uint32_t> pixels(static_cast<size_t>(rw) * rh);
    SDL_Rect readRect = { rx, ry, rw, rh };
    SDL_RenderReadPixels(r, &readRect, SDL_PIXELFORMAT_ARGB8888, pixels.data(), rw * 4);
    SDL_UpdateTexture(selectionTexture, nullptr, pixels.data(), rw * 4);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderFillRect(r, &readRect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    currentBounds = { rx, ry, rw, rh };
    rotation = 0.f;
    syncDrawCenterFromBounds();
    active = true;
    dirty = true;
    fillColorIsTransparent_ = false;
    isMoving = false;
    isRotating = false;
    resizing = Handle::NONE;
    isDrawing = false;
    lassoPoints_.clear();
}

std::vector<uint32_t> SelectTool::getFloatingPixels(SDL_Renderer* r) const {
    if (!selectionTexture || currentBounds.w <= 0 || currentBounds.h <= 0)
        return {};
    int w = currentBounds.w, h = currentBounds.h;

    std::vector<uint32_t> pixels(w * h, 0);
    SDL_Texture* tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tmp) return pixels;
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tmp);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_Rect dst = { 0, 0, w, h };
    renderWithTransform(r, dst);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);
    SDL_SetRenderTarget(r, prev);
    SDL_DestroyTexture(tmp);
    if (fillColorIsTransparent_) {
        for (size_t i = 0; i < pixels.size(); i++) {
            if (pixels[i] == TRANSPARENT_FILL_PREVIEW_ARGB) pixels[i] = 0;
        }
    }
    return pixels;
}

void SelectTool::fillWithColor(SDL_Renderer* r, SDL_Color color) {
    if (!selectionTexture || !active) return;
    int w = currentBounds.w, h = currentBounds.h;
    if (w <= 0 || h <= 0) return;
    fillColorIsTransparent_ = (color.a == 0);
    const uint32_t newARGB = fillColorIsTransparent_
        ? TRANSPARENT_FILL_PREVIEW_ARGB
        : ((static_cast<uint32_t>(color.a) << 24) |
           (static_cast<uint32_t>(color.r) << 16) |
           (static_cast<uint32_t>(color.g) << 8) |
           static_cast<uint32_t>(color.b));

    void* texPixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(selectionTexture, nullptr, &texPixels, &pitch) == 0) {
        // STREAMING texture (e.g. Select All): locked buffer is write-only, so read via render target first
        int texW = 0, texH = 0;
        SDL_QueryTexture(selectionTexture, nullptr, nullptr, &texW, &texH);
        std::vector<uint32_t> pixels(static_cast<size_t>(texW) * texH);
        SDL_Texture* prev = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, selectionTexture);
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), texW * 4);
        SDL_SetRenderTarget(r, prev);
        bool anyOpaque = false;
        for (size_t i = 0; i < pixels.size(); i++) {
            if ((pixels[i] >> 24) != 0) { anyOpaque = true; break; }
        }
        for (size_t i = 0; i < pixels.size(); i++) {
            if (anyOpaque ? (pixels[i] >> 24) != 0 : true)
                pixels[i] = newARGB;
        }
        for (int row = 0; row < texH; row++)
            memcpy(static_cast<uint8_t*>(texPixels) + row * pitch, pixels.data() + row * texW, static_cast<size_t>(texW) * 4);
        SDL_UnlockTexture(selectionTexture);
    } else {
        // TARGET texture (rect/lasso selection): read via render target then update
        std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
        SDL_Texture* prev = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, selectionTexture);
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);
        SDL_SetRenderTarget(r, prev);
        bool anyOpaque = false;
        for (size_t i = 0; i < pixels.size(); i++) {
            if ((pixels[i] >> 24) != 0) { anyOpaque = true; break; }
        }
        for (size_t i = 0; i < pixels.size(); i++) {
            if (anyOpaque ? (pixels[i] >> 24) != 0 : true)
                pixels[i] = newARGB;
        }
        SDL_UpdateTexture(selectionTexture, nullptr, pixels.data(), w * 4);
    }
    dirty = true;
}

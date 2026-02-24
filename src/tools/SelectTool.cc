#include "Tools.h"
#include "DrawingUtils.h"

SelectTool::~SelectTool() {
    if (selectionTexture) SDL_DestroyTexture(selectionTexture);
}

void SelectTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (active && handleMouseDown(cX, cY)) return;
    AbstractTool::onMouseDown(cX, cY, r, brushSize, color); // start rubber-band draw
}

void SelectTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (!handleMouseMove(cX, cY) && isDrawing) { lastX = cX; lastY = cY; }
}

bool SelectTool::onMouseUp(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (resizing != Handle::NONE || isMoving) { handleMouseUp(); return false; }
    if (!isDrawing || (cX == startX && cY == startY)) { isDrawing = false; return false; }

    // Commit the rubber-band selection
    currentBounds = { std::min(startX, cX), std::min(startY, cY),
                      std::max(1, std::abs(cX - startX)), std::max(1, std::abs(cY - startY)) };

    if (selectionTexture) SDL_DestroyTexture(selectionTexture);
    selectionTexture = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         currentBounds.w, currentBounds.h);
    SDL_SetTextureBlendMode(selectionTexture, SDL_BLENDMODE_BLEND);

    std::vector<uint32_t> pixels(currentBounds.w * currentBounds.h);
    SDL_RenderReadPixels(r, &currentBounds, SDL_PIXELFORMAT_ARGB8888, pixels.data(), currentBounds.w * 4);
    SDL_UpdateTexture(selectionTexture, nullptr, pixels.data(), currentBounds.w * 4);

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderFillRect(r, &currentBounds);

    active = true;
    isDrawing = false;
    return true;
}

void SelectTool::onOverlayRender(SDL_Renderer* r) {
    if (!active || !selectionTexture) return;
    SDL_RendererFlip flip = (SDL_RendererFlip)(
        (flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) |
        (flipY ? SDL_FLIP_VERTICAL   : SDL_FLIP_NONE));
    SDL_RenderCopyEx(r, selectionTexture, nullptr, &currentBounds, 0.0, nullptr, flip);
}

void SelectTool::onPreviewRender(SDL_Renderer* r, int /*brushSize*/, SDL_Color /*color*/) {
    if (isDrawing) {
        int mouseX, mouseY; SDL_GetMouseState(&mouseX, &mouseY);
        int curX, curY; mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
        int wx1, wy1, wx2, wy2;
        mapper->getWindowCoords(std::min(startX, curX), std::min(startY, curY), &wx1, &wy1);
        mapper->getWindowCoords(std::max(startX, curX), std::max(startY, curY), &wx2, &wy2);
        SDL_Rect rect = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
        DrawingUtils::drawMarchingRect(r, &rect);
    }
    if (active) drawHandles(r);
}

void SelectTool::deactivate(SDL_Renderer* r) {
    if (!active) return;
    if (selectionTexture) {
        SDL_RendererFlip flip = (SDL_RendererFlip)(
            (flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) |
            (flipY ? SDL_FLIP_VERTICAL   : SDL_FLIP_NONE));
        SDL_RenderCopyEx(r, selectionTexture, nullptr, &currentBounds, 0.0, nullptr, flip);
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
    active = true;
    dirty  = true;   // pasted content always modifies the canvas on commit
    isMoving = false;
    resizing = Handle::NONE;
    isDrawing = false;
}

std::vector<uint32_t> SelectTool::getFloatingPixels(SDL_Renderer* r) const {
    if (!selectionTexture || currentBounds.w <= 0 || currentBounds.h <= 0)
        return {};
    int w = currentBounds.w, h = currentBounds.h;
    std::vector<uint32_t> pixels(w * h, 0);

    // selectionTexture is SDL_TEXTUREACCESS_TARGET so LockTexture won't work.
    // Read via a temporary streaming texture rendered into a target texture.
    SDL_Texture* tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tmp) return pixels;
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tmp);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, selectionTexture, nullptr, nullptr);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);
    SDL_SetRenderTarget(r, prev);
    SDL_DestroyTexture(tmp);
    return pixels;
}

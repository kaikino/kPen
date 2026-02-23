#include "Tools.h"
#include "DrawingUtils.h"

SelectTool::Handle SelectTool::getHandle(int cX, int cY) const {
    if (!state.active) return Handle::NONE;
    const SDL_Rect& a = state.area;
    bool onL = std::abs(cX - a.x) <= GRAB;
    bool onR = std::abs(cX - (a.x + a.w)) <= GRAB;
    bool onT = std::abs(cY - a.y) <= GRAB;
    bool onB = std::abs(cY - (a.y + a.h)) <= GRAB;
    bool inX = cX >= a.x - GRAB && cX <= a.x + a.w + GRAB;
    bool inY = cY >= a.y - GRAB && cY <= a.y + a.h + GRAB;
    if (onL && onT) return Handle::NW;
    if (onR && onT) return Handle::NE;
    if (onL && onB) return Handle::SW;
    if (onR && onB) return Handle::SE;
    if (onT && inX) return Handle::N;
    if (onB && inX) return Handle::S;
    if (onL && inY) return Handle::W;
    if (onR && inY) return Handle::E;
    return Handle::NONE;
}

SelectTool::~SelectTool() {
    if (state.selectionTexture) SDL_DestroyTexture(state.selectionTexture);
}

void SelectTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (state.active) {
        Handle h = getHandle(cX, cY);
        if (h != Handle::NONE) {
            state.resizing = h;
            // Store the opposite edge as the fixed anchor
            state.anchorX = (h == Handle::W || h == Handle::NW || h == Handle::SW)
                ? state.area.x + state.area.w : state.area.x;
            state.anchorY = (h == Handle::N || h == Handle::NW || h == Handle::NE)
                ? state.area.y + state.area.h : state.area.y;
            return;
        }
        SDL_Point pt = {cX, cY};
        if (SDL_PointInRect(&pt, &state.area)) {
            state.isMoving = true;
            state.dragOffsetX = cX - state.area.x;
            state.dragOffsetY = cY - state.area.y;
            return;
        }
    }
    AbstractTool::onMouseDown(cX, cY, canvasRenderer, brushSize, color);
}

void SelectTool::onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (state.resizing != Handle::NONE) {
        Handle h = state.resizing;
        int newX = state.area.x, newY = state.area.y;
        int newW = state.area.w, newH = state.area.h;
        // Update the dragged edge(s), keep anchor fixed
        if (h == Handle::W || h == Handle::NW || h == Handle::SW) {
            newX = std::min(cX, state.anchorX);
            newW = std::abs(state.anchorX - cX);
        }
        if (h == Handle::E || h == Handle::NE || h == Handle::SE) {
            newX = std::min(cX, state.anchorX);
            newW = std::abs(cX - state.anchorX);
        }
        if (h == Handle::N || h == Handle::NW || h == Handle::NE) {
            newY = std::min(cY, state.anchorY);
            newH = std::abs(state.anchorY - cY);
        }
        if (h == Handle::S || h == Handle::SW || h == Handle::SE) {
            newY = std::min(cY, state.anchorY);
            newH = std::abs(cY - state.anchorY);
        }
        if (newW > 0 && newH > 0) {
            state.area = { newX, newY, newW, newH };
        }
    } else if (state.isMoving) {
        state.hasMoved = true;
        state.area.x = cX - state.dragOffsetX;
        state.area.y = cY - state.dragOffsetY;
    } else if (isDrawing) {
        lastX = cX;
        lastY = cY;
    }
}

bool SelectTool::onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (state.resizing != Handle::NONE) {
        state.resizing = Handle::NONE;
        return false;
    }
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

        std::vector<uint32_t> pixels(state.area.w * state.area.h);
        SDL_RenderReadPixels(canvasRenderer, &state.area, SDL_PIXELFORMAT_ARGB8888, pixels.data(), state.area.w * 4);
        SDL_UpdateTexture(state.selectionTexture, NULL, pixels.data(), state.area.w * 4);

        SDL_SetRenderDrawColor(canvasRenderer, 255, 255, 255, 255);
        SDL_RenderFillRect(canvasRenderer, &state.area);
        
        state.active = true;
        isDrawing = false;
        return true;
    }
    return false;
}

void SelectTool::onOverlayRender(SDL_Renderer* overlayRenderer) {
    // Only render the texture content here (needs canvas coords).
    // The outline, handles, and drag rect are drawn in onPreviewRender
    // (window coords) so they are always exactly 1 window pixel wide.
    if (state.active && state.selectionTexture) {
        SDL_RenderCopy(overlayRenderer, state.selectionTexture, NULL, &state.area);
    }
}

void SelectTool::onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) {
    // Draw selection drag preview
    if (isDrawing) {
        int mouseX, mouseY; SDL_GetMouseState(&mouseX, &mouseY);
        int curX, curY; mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
        int wx1, wy1, wx2, wy2;
        mapper->getWindowCoords(std::min(startX, curX), std::min(startY, curY), &wx1, &wy1);
        mapper->getWindowCoords(std::max(startX, curX), std::max(startY, curY), &wx2, &wy2);
        SDL_Rect r = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
        DrawingUtils::drawMarchingRect(winRenderer, &r);
    }

    if (state.active) {
        int wx1, wy1, wx2, wy2;
        mapper->getWindowCoords(state.area.x,                state.area.y,                &wx1, &wy1);
        mapper->getWindowCoords(state.area.x + state.area.w, state.area.y + state.area.h, &wx2, &wy2);
        SDL_Rect outline = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
        DrawingUtils::drawMarchingRect(winRenderer, &outline);

        // Draw 8 handle squares in window coords
        const int hs = 4;
        int wmx = (wx1 + wx2) / 2, wmy = (wy1 + wy2) / 2;
        SDL_Point handles[] = {
            {wx1, wy1}, {wmx, wy1}, {wx2, wy1},
            {wx1, wmy},              {wx2, wmy},
            {wx1, wy2}, {wmx, wy2}, {wx2, wy2}
        };
        for (auto& p : handles) {
            SDL_Rect sq = { p.x - hs, p.y - hs, hs * 2 + 1, hs * 2 + 1 };
            SDL_SetRenderDrawColor(winRenderer, 255, 255, 255, 255);
            SDL_RenderFillRect(winRenderer, &sq);
            SDL_SetRenderDrawColor(winRenderer, 0, 0, 0, 255);
            SDL_RenderDrawRect(winRenderer, &sq);
        }
    }
}

void SelectTool::deactivate(SDL_Renderer* canvasRenderer) {
    if (!state.active) return;
    if (state.selectionTexture) {
        SDL_RenderCopy(canvasRenderer, state.selectionTexture, NULL, &state.area);
        SDL_DestroyTexture(state.selectionTexture);
        state.selectionTexture = nullptr;
    }
    state.active = false;
}

bool SelectTool::isSelectionActive() const { return state.active; }

bool SelectTool::isHit(int cX, int cY) const {
    if (!state.active) return false;
    // Consider a hit if clicking inside OR on a handle
    SDL_Point pt = {cX, cY};
    return SDL_PointInRect(&pt, &state.area) || getHandle(cX, cY) != Handle::NONE;
}

void SelectTool::activateWithTexture(SDL_Texture* tex, SDL_Rect area) {
    if (state.selectionTexture) SDL_DestroyTexture(state.selectionTexture);
    state.selectionTexture = tex;
    state.area = area;
    state.active = true;
    state.isMoving = false;
    state.resizing = Handle::NONE;
    isDrawing = false;
}

bool SelectTool::hasOverlayContent() { return state.active; }

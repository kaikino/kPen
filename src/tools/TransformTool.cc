#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>

// ── Handle hit-testing ────────────────────────────────────────────────────────

TransformTool::Handle TransformTool::getHandle(int cX, int cY) const {
    const SDL_Rect& a = currentBounds;
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

// ── Shared mouse handling ─────────────────────────────────────────────────────

bool TransformTool::handleMouseDown(int cX, int cY) {
    Handle h = getHandle(cX, cY);
    if (h != Handle::NONE) {
        resizing = h;
        anchorX = (h == Handle::W || h == Handle::NW || h == Handle::SW)
            ? currentBounds.x + currentBounds.w : currentBounds.x;
        anchorY = (h == Handle::N || h == Handle::NW || h == Handle::NE)
            ? currentBounds.y + currentBounds.h : currentBounds.y;
        return true;
    }
    SDL_Point pt = {cX, cY};
    if (SDL_PointInRect(&pt, &currentBounds)) {
        isMoving = true;
        dragOffX = cX - currentBounds.x;
        dragOffY = cY - currentBounds.y;
        return true;
    }
    return false;
}

bool TransformTool::handleMouseMove(int cX, int cY) {
    if (resizing != Handle::NONE) {
        moved = true;
        int newX = currentBounds.x, newY = currentBounds.y;
        int newW = currentBounds.w, newH = currentBounds.h;

        // X axis
        if (resizing == Handle::W || resizing == Handle::NW || resizing == Handle::SW ||
            resizing == Handle::E || resizing == Handle::NE || resizing == Handle::SE) {
            newX = std::min(cX, anchorX);
            newW = std::abs(cX - anchorX);
            if (cX < anchorX && (resizing == Handle::E || resizing == Handle::NE || resizing == Handle::SE)) {
                if      (resizing == Handle::E)  resizing = Handle::W;
                else if (resizing == Handle::NE) resizing = Handle::NW;
                else if (resizing == Handle::SE) resizing = Handle::SW;
                anchorX = newX + newW;
                flipX = !flipX;
            } else if (cX > anchorX && (resizing == Handle::W || resizing == Handle::NW || resizing == Handle::SW)) {
                if      (resizing == Handle::W)  resizing = Handle::E;
                else if (resizing == Handle::NW) resizing = Handle::NE;
                else if (resizing == Handle::SW) resizing = Handle::SE;
                anchorX = newX;
                flipX = !flipX;
            }
        }

        // Y axis
        if (resizing == Handle::N || resizing == Handle::NW || resizing == Handle::NE ||
            resizing == Handle::S || resizing == Handle::SW || resizing == Handle::SE) {
            newY = std::min(cY, anchorY);
            newH = std::abs(cY - anchorY);
            if (cY < anchorY && (resizing == Handle::S || resizing == Handle::SW || resizing == Handle::SE)) {
                if      (resizing == Handle::S)  resizing = Handle::N;
                else if (resizing == Handle::SW) resizing = Handle::NW;
                else if (resizing == Handle::SE) resizing = Handle::NE;
                anchorY = newY + newH;
                flipY = !flipY;
            } else if (cY > anchorY && (resizing == Handle::N || resizing == Handle::NW || resizing == Handle::NE)) {
                if      (resizing == Handle::N)  resizing = Handle::S;
                else if (resizing == Handle::NW) resizing = Handle::SW;
                else if (resizing == Handle::NE) resizing = Handle::SE;
                anchorY = newY;
                flipY = !flipY;
            }
        }

        if (newW > 0 && newH > 0) currentBounds = { newX, newY, newW, newH };
        return true;
    }
    if (isMoving) {
        moved = true;
        currentBounds.x = cX - dragOffX;
        currentBounds.y = cY - dragOffY;
        return true;
    }
    return false;
}

void TransformTool::handleMouseUp() {
    resizing = Handle::NONE;
    isMoving = false;
}

// ── Handle rendering ──────────────────────────────────────────────────────────

void TransformTool::drawHandles(SDL_Renderer* winRenderer) const {
    int wx1, wy1, wx2, wy2;
    mapper->getWindowCoords(currentBounds.x,                   currentBounds.y,                   &wx1, &wy1);
    mapper->getWindowCoords(currentBounds.x + currentBounds.w, currentBounds.y + currentBounds.h, &wx2, &wy2);

    SDL_Rect outline = { wx1, wy1, wx2 - wx1, wy2 - wy1 };
    DrawingUtils::drawMarchingRect(winRenderer, &outline);

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

// ── isHit ─────────────────────────────────────────────────────────────────────

bool TransformTool::isHit(int cX, int cY) const {
    SDL_Point pt = {cX, cY};
    return SDL_PointInRect(&pt, &currentBounds) || getHandle(cX, cY) != Handle::NONE;
}

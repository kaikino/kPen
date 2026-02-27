#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>

// ── Handle hit-testing ────────────────────────────────────────────────────────

TransformTool::Handle TransformTool::getHandle(int cX, int cY) const {
    // Convert canvas corners to window space so the grab radius is a fixed
    // number of window pixels, independent of zoom level.
    // Use SDL_GetMouseState for the cursor position
    const SDL_Rect& a = currentBounds;
    int wx1, wy1, wx2, wy2, wmx, wmy;
    mapper->getWindowCoords(a.x,        a.y,        &wx1, &wy1);
    mapper->getWindowCoords(a.x + a.w,  a.y + a.h,  &wx2, &wy2);
    wmx = (wx1 + wx2) / 2;
    wmy = (wy1 + wy2) / 2;
    int wX, wY;
    SDL_GetMouseState(&wX, &wY);

    const int G = GRAB_WIN;
    bool onL = std::abs(wX - wx1) <= G;
    bool onR = std::abs(wX - wx2) <= G;
    bool onT = std::abs(wY - wy1) <= G;
    bool onB = std::abs(wY - wy2) <= G;
    // Mid-edge handles: only trigger near the midpoint, not the full edge
    bool nearMX = std::abs(wX - wmx) <= G;
    bool nearMY = std::abs(wY - wmy) <= G;
    if (onL && onT) return Handle::NW;
    if (onR && onT) return Handle::NE;
    if (onL && onB) return Handle::SW;
    if (onR && onB) return Handle::SE;
    if (onT && nearMX) return Handle::N;
    if (onB && nearMX) return Handle::S;
    if (onL && nearMY) return Handle::W;
    if (onR && nearMY) return Handle::E;
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
        dragAspect = currentBounds.h > 0
            ? (float)currentBounds.w / currentBounds.h : 1.f;
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

bool TransformTool::handleMouseMove(int cX, int cY, bool aspectLock) {
    // If not explicitly provided, pick up shift state from SDL
    if (!aspectLock) aspectLock = (SDL_GetModState() & KMOD_SHIFT) != 0;
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

        // Enforce minimum size of 1 before aspect lock so the ratio is preserved
        // even at the smallest possible size.
        if (newW < 1) { newW = 1; if (resizing == Handle::W || resizing == Handle::NW || resizing == Handle::SW) newX = anchorX - 1; }
        if (newH < 1) { newH = 1; if (resizing == Handle::N || resizing == Handle::NW || resizing == Handle::NE) newY = anchorY - 1; }

        // Aspect-lock: for corner handles, clamp to preserve dragAspect.
        if (aspectLock && dragAspect > 0.f &&
            (resizing == Handle::NW || resizing == Handle::NE ||
             resizing == Handle::SW || resizing == Handle::SE)) {
            // Decide which axis is limiting, then derive the OTHER axis from it.
            int wFromH = std::max(1, (int)std::round(newH * dragAspect));
            int hFromW = std::max(1, (int)std::round(newW / dragAspect));
            if (wFromH <= newW) {
                // H is the limiting axis — derive W from H
                newW = wFromH;
                if (resizing == Handle::NW || resizing == Handle::SW)
                    newX = anchorX - newW;
            } else {
                // W is the limiting axis — derive H from W, then re-derive W from that H
                newH = hFromW;
                newW = std::max(1, (int)std::round(newH * dragAspect));
                if (resizing == Handle::NW || resizing == Handle::NE)
                    newY = anchorY - newH;
                if (resizing == Handle::NW || resizing == Handle::SW)
                    newX = anchorX - newW;
            }
        }

        currentBounds = { newX, newY, newW, newH };
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

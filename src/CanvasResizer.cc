#include "CanvasResizer.h"
#include "Tools.h"
#include <algorithm>
#include <cmath>

void CanvasResizer::getHandlePositions(int canvasW, int canvasH,
                                        HandlePos out[8]) const {
    int wx1, wy1, wx2, wy2;
    mapper->getWindowCoords(0,       0,       &wx1, &wy1);
    mapper->getWindowCoords(canvasW, canvasH, &wx2, &wy2);
    int wmx = (wx1 + wx2) / 2, wmy = (wy1 + wy2) / 2;
    out[0] = { Handle::NW, wx1, wy1 };
    out[1] = { Handle::N,  wmx, wy1 };
    out[2] = { Handle::NE, wx2, wy1 };
    out[3] = { Handle::W,  wx1, wmy };
    out[4] = { Handle::E,  wx2, wmy };
    out[5] = { Handle::SW, wx1, wy2 };
    out[6] = { Handle::S,  wmx, wy2 };
    out[7] = { Handle::SE, wx2, wy2 };
}

void CanvasResizer::draw(SDL_Renderer* r, int canvasW, int canvasH) const {
    HandlePos hp[8];
    getHandlePositions(canvasW, canvasH, hp);

    int mx, my;
    SDL_GetMouseState(&mx, &my);
    static const int SHOW_RADIUS = 10;
    if (isDragging()) return;
    int wx1 = hp[0].wx, wy1 = hp[0].wy;
    int wx2 = hp[7].wx, wy2 = hp[7].wy;
    int nearX = std::max(wx1, std::min(mx, wx2));
    int nearY = std::max(wy1, std::min(my, wy2));
    int dx = mx - nearX, dy = my - nearY;
    if (dx == 0 && dy == 0) return;
    int distSq = dx * dx + dy * dy;
    if (distSq > SHOW_RADIUS * SHOW_RADIUS) return;

    for (auto& p : hp) {
        SDL_Rect sq = { p.wx - HS, p.wy - HS, HS * 2 + 1, HS * 2 + 1 };
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderFillRect(r, &sq);
        SDL_SetRenderDrawColor(r, 70, 130, 220, 255);
        SDL_RenderDrawRect(r, &sq);
    }
}

CanvasResizer::Handle CanvasResizer::hitTest(int winX, int winY,
                                              int canvasW, int canvasH) const {
    HandlePos hp[8];
    getHandlePositions(canvasW, canvasH, hp);
    for (auto& p : hp)
        if (std::abs(winX - p.wx) <= HIT && std::abs(winY - p.wy) <= HIT)
            return p.h;
    return Handle::NONE;
}

bool CanvasResizer::onMouseDown(int winX, int winY, int canvasW, int canvasH) {
    int wx1, wy1, wx2, wy2;
    mapper->getWindowCoords(0,       0,       &wx1, &wy1);
    mapper->getWindowCoords(canvasW, canvasH, &wx2, &wy2);
    if (winX >= wx1 && winX <= wx2 && winY >= wy1 && winY <= wy2) return false;

    Handle h = hitTest(winX, winY, canvasW, canvasH);
    if (h == Handle::NONE) return false;
    activeHandle  = h;
    dragStartWinX = winX;
    dragStartWinY = winY;
    dragBaseW     = canvasW;
    dragBaseH     = canvasH;
    return true;
}

void CanvasResizer::compute(int winX, int winY,
                             int& newW, int& newH,
                             int& originX, int& originY,
                             bool aspectLock) const {
    float scale = (float)mapper->getWindowSize(1000) / 1000.f;
    if (scale <= 0.f) scale = 1.f;

    int dX = (int)std::round((winX - dragStartWinX) / scale);
    int dY = (int)std::round((winY - dragStartWinY) / scale);

    newW    = dragBaseW;
    newH    = dragBaseH;
    originX = 0;
    originY = 0;

    switch (activeHandle) {
        case Handle::E:  newW = dragBaseW + dX; break;
        case Handle::S:  newH = dragBaseH + dY; break;
        case Handle::SE: newW = dragBaseW + dX; newH = dragBaseH + dY; break;

        case Handle::N:
            newH    = dragBaseH - dY;
            originY = dY;
            break;

        case Handle::W:
            newW    = dragBaseW - dX;
            originX = dX;
            break;

        case Handle::NE:
            newW    = dragBaseW + dX;
            newH    = dragBaseH - dY;
            originY = dY;
            break;
        case Handle::SW:
            newW    = dragBaseW - dX;
            newH    = dragBaseH + dY;
            originX = dX;
            break;
        case Handle::NW:
            newW    = dragBaseW - dX;
            newH    = dragBaseH - dY;
            originX = dX;
            originY = dY;
            break;

        default: break;
    }

    newW = std::max(1, newW);
    newH = std::max(1, newH);

    if (aspectLock && dragBaseH > 0) {
        float aspect = (float)dragBaseW / dragBaseH;
        bool isCorner = (activeHandle == Handle::NE || activeHandle == Handle::NW ||
                         activeHandle == Handle::SE || activeHandle == Handle::SW);
        if (isCorner && aspect > 0.f) {
            int wFromH = std::max(1, (int)std::round(newH * aspect));
            int hFromW = std::max(1, (int)std::round(newW / aspect));
            if (wFromH <= newW) {
                int oldW = newW;
                newW = wFromH;
                if (activeHandle == Handle::NW || activeHandle == Handle::SW)
                    originX += oldW - newW;
            } else {
                int oldH = newH;
                newH = hFromW;
                newW = std::max(1, (int)std::round(newH * aspect));
                int oldW = (int)std::round(oldH * aspect); // what W was before H clamped
                if (activeHandle == Handle::NW || activeHandle == Handle::NE)
                    originY += oldH - newH;
                if (activeHandle == Handle::NW || activeHandle == Handle::SW)
                    originX += oldW - newW;
            }
        }
    }

    newW = std::max(1, std::min(16384, newW));
    newH = std::max(1, std::min(16384, newH));
    originX = std::max(-(newW - 1), std::min(dragBaseW - 1, originX));
    originY = std::max(-(newH - 1), std::min(dragBaseH - 1, originY));
}

bool CanvasResizer::onMouseMove(int winX, int winY,
                                 int& previewW, int& previewH,
                                 int& originX,  int& originY,
                                 bool aspectLock) const {
    if (activeHandle == Handle::NONE) return false;
    compute(winX, winY, previewW, previewH, originX, originY, aspectLock);
    return true;
}

bool CanvasResizer::onMouseUp(int winX, int winY, int canvasW, int canvasH,
                               int& newW, int& newH, int& originX, int& originY,
                               bool aspectLock) {
    if (activeHandle == Handle::NONE) return false;
    compute(winX, winY, newW, newH, originX, originY, aspectLock);
    activeHandle = Handle::NONE;
    return (newW != canvasW || newH != canvasH || originX != 0 || originY != 0);
}

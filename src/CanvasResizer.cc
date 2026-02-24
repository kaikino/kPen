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

    // Only draw handles when the mouse is near the canvas border.
    // While dragging we always show them regardless of distance.
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    static const int SHOW_RADIUS = 25;  // window-pixel proximity threshold

    // Hide handles while the user is actively dragging one (the ghost outline is shown instead)
    if (isDragging()) return;

    // Show handles when the mouse is outside the canvas but within SHOW_RADIUS pixels of any edge.
    int wx1 = hp[0].wx, wy1 = hp[0].wy;  // top-left corner (NW handle)
    int wx2 = hp[7].wx, wy2 = hp[7].wy;  // bottom-right corner (SE handle)

    bool insideCanvas = (mx >= wx1 && mx <= wx2 && my >= wy1 && my <= wy2);
    if (insideCanvas) return;

    // Distance from mouse to each edge
    int distLeft   = std::abs(mx - wx1);
    int distRight  = std::abs(mx - wx2);
    int distTop    = std::abs(my - wy1);
    int distBottom = std::abs(my - wy2);
    int minDist = std::min({distLeft, distRight, distTop, distBottom});
    if (minDist > SHOW_RADIUS) return;

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
    // Don't activate if the cursor is inside the canvas — handles aren't shown there
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
                             int& originX, int& originY) const {
    float scale = (float)mapper->getWindowSize(1000) / 1000.f;
    if (scale <= 0.f) scale = 1.f;

    int dX = (int)std::round((winX - dragStartWinX) / scale);
    int dY = (int)std::round((winY - dragStartWinY) / scale);

    newW    = dragBaseW;
    newH    = dragBaseH;
    originX = 0;
    originY = 0;

    switch (activeHandle) {
        // ── Bottom/right handles: extend canvas, origin stays fixed ──
        case Handle::E:  newW = dragBaseW + dX; break;
        case Handle::S:  newH = dragBaseH + dY; break;
        case Handle::SE: newW = dragBaseW + dX; newH = dragBaseH + dY; break;

        // ── Top handle: move top edge; bottom stays fixed ──
        // Dragging up (dY < 0) grows the canvas; origin shifts up by dY.
        // Dragging down (dY > 0) crops the top; origin shifts down by dY.
        case Handle::N:
            newH    = dragBaseH - dY;
            originY = dY;
            break;

        // ── Left handle: move left edge; right stays fixed ──
        case Handle::W:
            newW    = dragBaseW - dX;
            originX = dX;
            break;

        // ── Corner combinations ──
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

    newW = std::max(1, std::min(16384, newW));
    newH = std::max(1, std::min(16384, newH));
    // Clamp origin shift so we don't reference pixels outside the old buffer.
    originX = std::max(-(newW - 1), std::min(dragBaseW - 1, originX));
    originY = std::max(-(newH - 1), std::min(dragBaseH - 1, originY));
}

bool CanvasResizer::onMouseMove(int winX, int winY,
                                 int& previewW, int& previewH,
                                 int& originX,  int& originY) const {
    if (activeHandle == Handle::NONE) return false;
    compute(winX, winY, previewW, previewH, originX, originY);
    return true;
}

bool CanvasResizer::onMouseUp(int winX, int winY, int canvasW, int canvasH,
                               int& newW, int& newH, int& originX, int& originY) {
    if (activeHandle == Handle::NONE) return false;
    compute(winX, winY, newW, newH, originX, originY);
    activeHandle = Handle::NONE;
    return (newW != canvasW || newH != canvasH || originX != 0 || originY != 0);
}

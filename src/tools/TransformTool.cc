#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>
#include <climits>

// ── Rotation helpers ──────────────────────────────────────────────────────────

void TransformTool::rotatePt(float inX, float inY, float pivX, float pivY,
                              float angle, float& outX, float& outY) const {
    float c = std::cos(angle), s = std::sin(angle);
    float dx = inX - pivX, dy = inY - pivY;
    outX = pivX + dx * c - dy * s;
    outY = pivY + dx * s + dy * c;
}

bool TransformTool::pointInRotatedBounds(int cX, int cY) const {
    if (rotation == 0.f) {
        SDL_Point pt = {cX, cY};
        return SDL_PointInRect(&pt, &currentBounds) != 0;
    }
    float cx = currentBounds.x + currentBounds.w * 0.5f;
    float cy = currentBounds.y + currentBounds.h * 0.5f;
    float lx, ly;
    rotatePt((float)cX, (float)cY, cx, cy, -rotation, lx, ly);
    return lx >= currentBounds.x && lx < currentBounds.x + currentBounds.w
        && ly >= currentBounds.y && ly < currentBounds.y + currentBounds.h;
}

// ── Rotate handle position ────────────────────────────────────────────────────

void TransformTool::getRotateHandleWin(int& rhwx, int& rhwy) const {
    float ccx = currentBounds.x + currentBounds.w * 0.5f;
    float ccy = currentBounds.y + currentBounds.h * 0.5f;

    // Rotate handle always extends from the visual top edge (N handle side).
    // flipY swaps which local edge is visually "top", but currentBounds is
    // always canonical (positive size), so we always use the top edge (y)
    // and the flip is purely a rendering concern for the content inside.
    // The rotate handle stays at the geometric top of currentBounds.
    float edgeY = (float)currentBounds.y;

    float rnx, rny;
    rotatePt(ccx, edgeY, ccx, ccy, rotation, rnx, rny);
    int nwx, nwy;
    mapper->getWindowCoords((int)std::round(rnx), (int)std::round(rny), &nwx, &nwy);

    // Outward direction from N edge (0, -1) rotated by rotation:
    // gives (sin(rotation), -cos(rotation)) in canvas space, same in window space
    // (assuming uniform scaling). Extend by ROT_OFFSET window pixels.
    rhwx = (int)std::round(nwx + std::sin(rotation) * ROT_OFFSET);
    rhwy = (int)std::round(nwy - std::cos(rotation) * ROT_OFFSET);
}

// ── Handle hit-testing ────────────────────────────────────────────────────────
//
// currentBounds is always a canonical positive-size AABB. Flips are rendering-
// only and do NOT remap handle positions — the NW handle is always at the
// top-left corner of currentBounds (before rotation). This keeps handle logic
// simple and consistent regardless of flip state.

TransformTool::Handle TransformTool::getHandle(int cX, int cY) const {
    const SDL_Rect& a = currentBounds;
    float ccx = a.x + a.w * 0.5f;
    float ccy = a.y + a.h * 0.5f;

    int wX, wY; SDL_GetMouseState(&wX, &wY);

    // ① Rotate handle first
    {
        int rhwx, rhwy; getRotateHandleWin(rhwx, rhwy);
        const int G = GRAB_WIN;
        if (std::abs(wX - rhwx) <= G && std::abs(wY - rhwy) <= G)
            return Handle::ROTATE;
    }

    // ② Square resize handles — 8 positions in canvas space, rotated to world
    struct CPt { float x, y; Handle h; };
    CPt pts[] = {
        { (float)a.x,         (float)a.y,         Handle::NW },
        { ccx,                (float)a.y,          Handle::N  },
        { (float)(a.x + a.w), (float)a.y,         Handle::NE },
        { (float)a.x,         ccy,                 Handle::W  },
        { (float)(a.x + a.w), ccy,                 Handle::E  },
        { (float)a.x,         (float)(a.y + a.h), Handle::SW },
        { ccx,                (float)(a.y + a.h),  Handle::S  },
        { (float)(a.x + a.w),(float)(a.y + a.h),  Handle::SE },
    };

    const int G = GRAB_WIN;
    for (auto& p : pts) {
        float rx, ry;
        rotatePt(p.x, p.y, ccx, ccy, rotation, rx, ry);
        int wpx, wpy;
        mapper->getWindowCoords((int)std::round(rx), (int)std::round(ry), &wpx, &wpy);
        if (std::abs(wX - wpx) <= G && std::abs(wY - wpy) <= G)
            return p.h;
    }
    return Handle::NONE;
}

// ── Shared mouse handling ─────────────────────────────────────────────────────

bool TransformTool::handleMouseDown(int cX, int cY) {
    Handle h = getHandle(cX, cY);

    if (h == Handle::ROTATE) {
        isRotating    = true;
        rotPivotCX    = currentBounds.x + currentBounds.w * 0.5f;
        rotPivotCY    = currentBounds.y + currentBounds.h * 0.5f;
        rotBaseAngle  = rotation;
        rotLastAngle  = std::atan2((float)cY - rotPivotCY, (float)cX - rotPivotCX);
        rotStartAngle = rotLastAngle;
        return true;
    }

    if (h != Handle::NONE) {
        resizing   = h;
        dragAspect = currentBounds.h > 0
            ? (float)currentBounds.w / currentBounds.h : 1.f;

        // The anchor is the corner/edge diagonally opposite to the dragged handle,
        // in local (un-rotated) canvas space. currentBounds is always a canonical
        // positive-size AABB, so this is straightforward.
        float ccx = currentBounds.x + currentBounds.w * 0.5f;
        float ccy = currentBounds.y + currentBounds.h * 0.5f;

        // Local anchor X: opposite side from the handle being dragged
        float ancLX, ancLY;
        if (h == Handle::W || h == Handle::NW || h == Handle::SW)
            ancLX = (float)(currentBounds.x + currentBounds.w); // drag left  → anchor right
        else if (h == Handle::E || h == Handle::NE || h == Handle::SE)
            ancLX = (float)currentBounds.x;                     // drag right → anchor left
        else
            ancLX = ccx;                                         // N/S edge   → anchor center X

        if (h == Handle::N || h == Handle::NW || h == Handle::NE)
            ancLY = (float)(currentBounds.y + currentBounds.h); // drag top    → anchor bottom
        else if (h == Handle::S || h == Handle::SW || h == Handle::SE)
            ancLY = (float)currentBounds.y;                     // drag bottom → anchor top
        else
            ancLY = ccy;                                         // E/W edge   → anchor center Y

        anchorX = (int)std::round(ancLX);
        anchorY = (int)std::round(ancLY);

        // Rotate the local anchor into world space — this point stays fixed
        // throughout the drag, even as the shape rotates or flips.
        rotatePt(ancLX, ancLY, ccx, ccy, rotation, anchorWorldX, anchorWorldY);
        return true;
    }

    if (pointInRotatedBounds(cX, cY)) {
        isMoving = true;
        dragOffX = cX - currentBounds.x;
        dragOffY = cY - currentBounds.y;
        return true;
    }
    return false;
}

bool TransformTool::handleMouseMove(int cX, int cY, bool aspectLock) {
    if (!aspectLock) aspectLock = (SDL_GetModState() & KMOD_SHIFT) != 0;

    if (isRotating) {
        moved = true;
        float angle = std::atan2((float)cY - rotPivotCY, (float)cX - rotPivotCX);
        // Accumulate per-frame delta to avoid atan2 branch-cut jumps (±2π
        // discontinuity when the mouse crosses the negative-X axis).
        float delta = angle - rotLastAngle;
        if (delta >  (float)M_PI) delta -= 2.f * (float)M_PI;
        if (delta < -(float)M_PI) delta += 2.f * (float)M_PI;
        rotLastAngle = angle;
        rotation += delta;
        if (aspectLock) {
            const float snap = (float)M_PI / 12.f;
            rotation = std::round(rotation / snap) * snap;
        }
        return true;
    }

    if (resizing != Handle::NONE) {
        moved = true;

        // ── Step 1: Un-rotate the mouse into local (canonical) space ──────────
        //
        // We un-rotate the mouse around the world-space anchor point. The
        // anchor is fixed in world space and maps to (anchorX, anchorY) in
        // local space. After un-rotating, (cXl, cYl) is the mouse expressed
        // in the same coordinate system as currentBounds — axis-aligned, with
        // the anchor at (anchorX, anchorY).
        // Un-rotate using a lambda so after a flip updates anchorX/Y we can
        // re-run with the new local origin — eliminating the one-frame glitch.
        float c = std::cos(-rotation), s = std::sin(-rotation);
        auto unrotMouse = [&](float& outXl, float& outYl) {
            float dxW = (float)cX - anchorWorldX;
            float dyW = (float)cY - anchorWorldY;
            outXl = (float)anchorX + dxW * c - dyW * s;
            outYl = (float)anchorY + dxW * s + dyW * c;
        };
        float cXl, cYl;
        unrotMouse(cXl, cYl);

        // ── Step 2: Compute new bounds from local mouse position ───────────────
        //
        // The anchor is always the fixed edge/corner. The mouse determines the
        // opposite edge. We never let the dimension go below 1.
        //
        // If the mouse crosses to the other side of the anchor, we flip the
        // content and swap which side is being dragged — WITHOUT collapsing the
        // dimension. The crossing distance (how far past the anchor the mouse is)
        // becomes the new size, and we continue growing from there.

        int newX = currentBounds.x, newY = currentBounds.y;
        int newW = currentBounds.w, newH = currentBounds.h;

        // ── X axis ────────────────────────────────────────────────────────────
        //
        // "dragFromRight" means the anchor is on the RIGHT side and the drag
        // handle is on the LEFT (W, NW, SW). The mouse (cXl in local space)
        // should be left of the anchor (rawW < 0) while dragging normally.
        //
        // If the mouse crosses to the other side, we toggle flipX, swap to the
        // mirror handle, and update the local anchor so subsequent frames keep
        // reading correctly. The WORLD anchor (anchorWorldX/Y) never moves.
        bool affectsX = (resizing == Handle::W  || resizing == Handle::NW || resizing == Handle::SW ||
                         resizing == Handle::E  || resizing == Handle::NE || resizing == Handle::SE);
        if (affectsX) {
            bool dragFromRight = (resizing == Handle::W || resizing == Handle::NW || resizing == Handle::SW);
            float rawW = cXl - (float)anchorX; // signed distance: mouse - anchor in local space

            if (dragFromRight && rawW > 0.f) {
                // Crossed right: flip X, swap to E/NE/SE
                flipX = !flipX;
                resizing = (resizing == Handle::W)  ? Handle::E  :
                           (resizing == Handle::NW) ? Handle::NE : Handle::SE;
                // The world anchor stays fixed. Update local anchor to new left side.
                // After the swap the anchor is on the LEFT, so anchorX = old anchorX
                // (the right side that was fixed) is now the new left edge anchor.
                // But we recompute via the world anchor unrotated:
                {
                    float ua, dummy;
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    // Un-rotate world anchor back to local (using current newX/newY/newW/newH center — 
                    // they haven't been set yet so use currentBounds center)
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    ua = ccx2 + dxA * co2 - dyA * si2;
                    dummy = ccy2 + dxA * si2 + dyA * co2;
                    anchorX = (int)std::round(ua);
                    (void)dummy;
                }
                dragFromRight = false;
                unrotMouse(cXl, cYl);  // re-derive with updated anchorX
                rawW = cXl - (float)anchorX;
            } else if (!dragFromRight && rawW < 0.f) {
                // Crossed left: flip X, swap to W/NW/SW
                flipX = !flipX;
                resizing = (resizing == Handle::E)  ? Handle::W  :
                           (resizing == Handle::NE) ? Handle::NW : Handle::SW;
                {
                    float ua, dummy;
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    ua = ccx2 + dxA * co2 - dyA * si2;
                    dummy = ccy2 + dxA * si2 + dyA * co2;
                    anchorX = (int)std::round(ua);
                    (void)dummy;
                }
                dragFromRight = true;
                unrotMouse(cXl, cYl);  // re-derive with updated anchorX
                rawW = cXl - (float)anchorX;
            }

            newW = std::max(1, (int)std::round(std::abs(rawW)));
            if (dragFromRight)
                newX = (int)std::round((float)anchorX - (float)newW); // anchor on right
            else
                newX = anchorX;                                         // anchor on left
        }

        // ── Y axis ────────────────────────────────────────────────────────────
        bool affectsY = (resizing == Handle::N  || resizing == Handle::NW || resizing == Handle::NE ||
                         resizing == Handle::S  || resizing == Handle::SW || resizing == Handle::SE);
        if (affectsY) {
            bool dragFromBottom = (resizing == Handle::N || resizing == Handle::NW || resizing == Handle::NE);
            float rawH = cYl - (float)anchorY;

            if (dragFromBottom && rawH > 0.f) {
                flipY = !flipY;
                resizing = (resizing == Handle::N)  ? Handle::S  :
                           (resizing == Handle::NW) ? Handle::SW : Handle::SE;
                {
                    float dummy, ua;
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    dummy = ccx2 + dxA * co2 - dyA * si2;
                    ua    = ccy2 + dxA * si2 + dyA * co2;
                    anchorY = (int)std::round(ua);
                    (void)dummy;
                }
                dragFromBottom = false;
                unrotMouse(cXl, cYl);  // re-derive with updated anchorY
                rawH = cYl - (float)anchorY;
            } else if (!dragFromBottom && rawH < 0.f) {
                flipY = !flipY;
                resizing = (resizing == Handle::S)  ? Handle::N  :
                           (resizing == Handle::SW) ? Handle::NW : Handle::NE;
                {
                    float dummy, ua;
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    dummy = ccx2 + dxA * co2 - dyA * si2;
                    ua    = ccy2 + dxA * si2 + dyA * co2;
                    anchorY = (int)std::round(ua);
                    (void)dummy;
                }
                dragFromBottom = true;
                unrotMouse(cXl, cYl);  // re-derive with updated anchorY
                rawH = cYl - (float)anchorY;
            }

            newH = std::max(1, (int)std::round(std::abs(rawH)));
            if (dragFromBottom)
                newY = (int)std::round((float)anchorY - (float)newH);
            else
                newY = anchorY;
        }

        // ── Step 3: World-anchor correction for rotated shapes ────────────────
        //
        // When the shape is rotated, changing size shifts the center, which
        // would drift the fixed anchor away from its world position. We correct
        // by computing where the center must be so that the anchor (at its
        // local offset from center) maps to the fixed world position.
        //
        // localOffset = anchor position relative to new center (in local space)
        // worldAnchor = center + R(rotation) * localOffset
        // => center = worldAnchor - R(rotation) * localOffset
        if (rotation != 0.f) {
            float hw = newW * 0.5f, hh = newH * 0.5f;

            // Local offset from the new center to the anchor
            float offX = 0.f, offY = 0.f;
            bool dragsLeft   = (resizing == Handle::W  || resizing == Handle::NW || resizing == Handle::SW);
            bool dragsRight  = (resizing == Handle::E  || resizing == Handle::NE || resizing == Handle::SE);
            bool dragsTop    = (resizing == Handle::N  || resizing == Handle::NW || resizing == Handle::NE);
            bool dragsBottom = (resizing == Handle::S  || resizing == Handle::SW || resizing == Handle::SE);

            if      (dragsLeft)   offX = +hw;  // anchor is on the right of new center
            else if (dragsRight)  offX = -hw;  // anchor is on the left of new center
            if      (dragsTop)    offY = +hh;  // anchor is at the bottom of new center
            else if (dragsBottom) offY = -hh;  // anchor is at the top of new center

            float co = std::cos(rotation), si = std::sin(rotation);
            // center = worldAnchor - R * offset
            float cx = anchorWorldX - (co * offX - si * offY);
            float cy = anchorWorldY - (si * offX + co * offY);
            newX = (int)std::round(cx - hw);
            newY = (int)std::round(cy - hh);
        }

        // ── Step 4: Aspect lock (corners only) ────────────────────────────────
        if (aspectLock && dragAspect > 0.f &&
            (resizing == Handle::NW || resizing == Handle::NE ||
             resizing == Handle::SW || resizing == Handle::SE)) {
            int wFromH = std::max(1, (int)std::round(newH * dragAspect));
            int hFromW = std::max(1, (int)std::round(newW / dragAspect));
            if (wFromH <= newW) {
                newW = wFromH;
                bool fromRight = (resizing == Handle::NW || resizing == Handle::SW);
                if (fromRight) newX = (int)std::round(anchorWorldX) - newW; // reuse local in no-rot case
                // (rotation correction above already handled; for simplicity
                //  aspect-lock at non-zero rotation may have minor drift — acceptable)
            } else {
                newH = hFromW;
                newW = std::max(1, (int)std::round(newH * dragAspect));
                bool fromBottom = (resizing == Handle::NW || resizing == Handle::NE);
                if (fromBottom) newY = (int)std::round(anchorWorldY) - newH;
                bool fromRight  = (resizing == Handle::NW || resizing == Handle::SW);
                if (fromRight)  newX = (int)std::round(anchorWorldX) - newW;
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
    resizing   = Handle::NONE;
    isMoving   = false;
    isRotating = false;
}

// ── Handle rendering ──────────────────────────────────────────────────────────

void TransformTool::drawHandles(SDL_Renderer* winRenderer) const {
    float ccx = currentBounds.x + currentBounds.w * 0.5f;
    float ccy = currentBounds.y + currentBounds.h * 0.5f;

    // Shared dashed-line helper
    auto drawDashed = [&](int ax, int ay, int bx, int by) {
        int dx = bx - ax, dy = by - ay;
        int steps = std::max(std::abs(dx), std::abs(dy));
        if (steps == 0) return;
        for (int i = 0; i < steps; ++i) {
            int px = ax + dx * i / steps;
            int py = ay + dy * i / steps;
            bool white = (i / 4) % 2 == 0;
            SDL_SetRenderDrawColor(winRenderer,
                white ? 255 : 0, white ? 255 : 0, white ? 255 : 0, 255);
            SDL_RenderDrawPoint(winRenderer, px, py);
        }
    };

    // --- Bounding box: 4 rotated dashed edges ---
    {
        float corners[4][2] = {
            { (float)currentBounds.x,                     (float)currentBounds.y                     },
            { (float)(currentBounds.x + currentBounds.w), (float)currentBounds.y                     },
            { (float)(currentBounds.x + currentBounds.w), (float)(currentBounds.y + currentBounds.h) },
            { (float)currentBounds.x,                     (float)(currentBounds.y + currentBounds.h) },
        };
        SDL_Point wpts[4];
        for (int i = 0; i < 4; ++i) {
            float rx, ry;
            rotatePt(corners[i][0], corners[i][1], ccx, ccy, rotation, rx, ry);
            mapper->getWindowCoords((int)std::round(rx), (int)std::round(ry), &wpts[i].x, &wpts[i].y);
        }
        drawDashed(wpts[0].x, wpts[0].y, wpts[1].x, wpts[1].y);
        drawDashed(wpts[1].x, wpts[1].y, wpts[2].x, wpts[2].y);
        drawDashed(wpts[2].x, wpts[2].y, wpts[3].x, wpts[3].y);
        drawDashed(wpts[3].x, wpts[3].y, wpts[0].x, wpts[0].y);
    }

    // --- Square resize handles at 8 positions, rotated with the shape ---
    //
    // Each handle is a small square whose center is the rotated canvas point,
    // but whose body is itself rotated by `rotation` so it stays aligned to
    // the bounding-box edges rather than remaining axis-aligned.
    //
    // We draw each handle as a filled rotated quad: compute its 4 window-space
    // corners (center ± half-size rotated by `rotation`), fill with two
    // triangles using SDL lines, then draw the outline edges.
    const int hs = 4;  // half-size: handle is (2*hs+1) x (2*hs+1) pixels
    struct CPt { float x, y; };
    CPt pts[] = {
        { (float)currentBounds.x,                     (float)currentBounds.y                     },
        { ccx,                                         (float)currentBounds.y                     },
        { (float)(currentBounds.x + currentBounds.w), (float)currentBounds.y                     },
        { (float)currentBounds.x,                     ccy                                        },
        { (float)(currentBounds.x + currentBounds.w), ccy                                        },
        { (float)currentBounds.x,                     (float)(currentBounds.y + currentBounds.h) },
        { ccx,                                         (float)(currentBounds.y + currentBounds.h) },
        { (float)(currentBounds.x + currentBounds.w), (float)(currentBounds.y + currentBounds.h) },
    };

    // Handle half-extents in window pixels, rotated to match the shape orientation.
    // These are pure window-space offsets — fixed pixel size regardless of zoom.
    // u = rightward along shape edge, v = downward along shape edge (both in window px).
    float sinR = std::sin(rotation), cosR = std::cos(rotation);
    float uWx =  cosR * hs, uWy =  sinR * hs;
    float vWx = -sinR * hs, vWy =  cosR * hs;

    // Helper: fill a quad given 4 window-space corners (scanline fill via horizontal spans)
    auto fillQuad = [&](SDL_Point q[4]) {
        // Find vertical extent
        int yMin = q[0].y, yMax = q[0].y;
        for (int i = 1; i < 4; i++) { yMin = std::min(yMin, q[i].y); yMax = std::max(yMax, q[i].y); }
        // For each scanline, find the two x intersections across all 4 edges
        for (int y = yMin; y <= yMax; y++) {
            int xLeft = INT_MAX, xRight = INT_MIN;
            for (int i = 0; i < 4; i++) {
                SDL_Point a = q[i], b = q[(i+1)%4];
                if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                    float t = (float)(y - a.y) / (b.y - a.y);
                    int xi = (int)(a.x + t * (b.x - a.x));
                    xLeft  = std::min(xLeft,  xi);
                    xRight = std::max(xRight, xi);
                }
            }
            if (xLeft <= xRight)
                SDL_RenderDrawLine(winRenderer, xLeft, y, xRight, y);
        }
    };

    for (auto& p : pts) {
        float rx, ry;
        rotatePt(p.x, p.y, ccx, ccy, rotation, rx, ry);
        int wpx, wpy;
        mapper->getWindowCoords((int)std::round(rx), (int)std::round(ry), &wpx, &wpy);

        // 4 corners of the rotated handle square in window space
        SDL_Point corners[4] = {
            { (int)std::round(wpx - uWx - vWx), (int)std::round(wpy - uWy - vWy) },  // top-left
            { (int)std::round(wpx + uWx - vWx), (int)std::round(wpy + uWy - vWy) },  // top-right
            { (int)std::round(wpx + uWx + vWx), (int)std::round(wpy + uWy + vWy) },  // bottom-right
            { (int)std::round(wpx - uWx + vWx), (int)std::round(wpy - uWy + vWy) },  // bottom-left
        };

        // White fill
        SDL_SetRenderDrawColor(winRenderer, 255, 255, 255, 255);
        fillQuad(corners);

        // Black outline
        SDL_SetRenderDrawColor(winRenderer, 0, 0, 0, 255);
        for (int i = 0; i < 4; i++)
            SDL_RenderDrawLine(winRenderer, corners[i].x, corners[i].y,
                               corners[(i+1)%4].x, corners[(i+1)%4].y);
    }

    // --- Rotate handle: dashed stem + circle ---
    {
        // Stem roots at the top edge of currentBounds (N), rotated to world space
        float rnx, rny;
        rotatePt(ccx, (float)currentBounds.y, ccx, ccy, rotation, rnx, rny);
        int nwx, nwy;
        mapper->getWindowCoords((int)std::round(rnx), (int)std::round(rny), &nwx, &nwy);

        int rhwx, rhwy;
        getRotateHandleWin(rhwx, rhwy);

        drawDashed(nwx, nwy, rhwx, rhwy);

        // Circle handle
        SDL_SetRenderDrawColor(winRenderer, 255, 255, 255, 255);
        DrawingUtils::drawFillCircle(winRenderer, rhwx, rhwy, hs);
        SDL_SetRenderDrawColor(winRenderer, 0, 0, 0, 255);
        for (int deg = 0; deg < 360; deg += 3) {
            float rad = deg * (float)M_PI / 180.f;
            SDL_RenderDrawPoint(winRenderer,
                rhwx + (int)std::round(hs * std::cos(rad)),
                rhwy + (int)std::round(hs * std::sin(rad)));
        }
    }
}

// ── isHit ─────────────────────────────────────────────────────────────────────

bool TransformTool::isHit(int cX, int cY) const {
    return pointInRotatedBounds(cX, cY) || getHandle(cX, cY) != Handle::NONE;
}

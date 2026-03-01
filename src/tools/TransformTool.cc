#include "Tools.h"
#include "DrawingUtils.h"
#include <cmath>
#include <algorithm>
#include <climits>

void TransformTool::rotatePt(float inX, float inY, float pivX, float pivY,
                              float angle, float& outX, float& outY) const {
    float c = std::cos(angle), s = std::sin(angle);
    float dx = inX - pivX, dy = inY - pivY;
    outX = pivX + dx * c - dy * s;
    outY = pivY + dx * s + dy * c;
}

bool TransformTool::pointInRotatedBounds(int cX, int cY) const {
    float rot = getRotation();
    if (rot == 0.f) {
        SDL_Point pt = {cX, cY};
        return SDL_PointInRect(&pt, &currentBounds) != 0;
    }
    float hw = currentBounds.w * 0.5f, hh = currentBounds.h * 0.5f;
    double rotDeg = std::fmod(rot * 180.0 / M_PI, 360.0);
    if (rotDeg < 0.0) rotDeg += 360.0;
    bool parityDiff = (currentBounds.w & 1) != (currentBounds.h & 1);
    bool is90or270 = (std::fabs(rotDeg - 90.0) < 1.0 || std::fabs(rotDeg - 270.0) < 1.0);
    float boxLeft, boxTop, pivX, pivY;
    if (is90or270 && parityDiff) {
        boxLeft = (float)std::round(drawCenterX - hw); boxTop = (float)std::round(drawCenterY - hh);
        pivX = boxLeft + (float)std::round(hw); pivY = boxTop + (float)std::round(hh);
    } else {
        boxLeft = drawCenterX - hw; boxTop = drawCenterY - hh;
        pivX = drawCenterX; pivY = drawCenterY;
    }
    float lx, ly;
    rotatePt((float)cX, (float)cY, pivX, pivY, -rot, lx, ly);
    return lx >= boxLeft && lx < boxLeft + currentBounds.w
        && ly >= boxTop && ly < boxTop + currentBounds.h;
}

void TransformTool::getRotateHandleWin(int& rhwx, int& rhwy) const {
    float rot = getRotation();
    float ccx = drawCenterX, ccy = drawCenterY;
    float hw = currentBounds.w * 0.5f, hh = currentBounds.h * 0.5f;
    double rotDeg = std::fmod(rot * 180.0 / M_PI, 360.0);
    if (rotDeg < 0.0) rotDeg += 360.0;
    bool parityDiff = (currentBounds.w & 1) != (currentBounds.h & 1);
    bool is90or270 = (std::fabs(rotDeg - 90.0) < 1.0 || std::fabs(rotDeg - 270.0) < 1.0);
    float pivX = ccx, pivY = ccy;
    float edgeY = (float)currentBounds.y;
    if (is90or270 && parityDiff) {
        pivX = (float)std::round(ccx - hw) + (float)std::round(hw);
        pivY = (float)std::round(ccy - hh) + (float)std::round(hh);
        edgeY = (float)std::round(ccy - hh);
    }
    float rnx, rny;
    rotatePt(pivX, edgeY, pivX, pivY, rot, rnx, rny);
    int nwx, nwy;
    mapper->getWindowCoords((int)std::round(rnx), (int)std::round(rny), &nwx, &nwy);
    rhwx = (int)std::round(nwx + std::sin(rot) * ROT_OFFSET);
    rhwy = (int)std::round(nwy - std::cos(rot) * ROT_OFFSET);
}

TransformTool::Handle TransformTool::getHandle(int cX, int cY) const {
    const SDL_Rect& a = currentBounds;
    float ccx = drawCenterX, ccy = drawCenterY;
    float rot = getRotation();
    float hw = a.w * 0.5f, hh = a.h * 0.5f;
    double rotDeg = std::fmod(rot * 180.0 / M_PI, 360.0);
    if (rotDeg < 0.0) rotDeg += 360.0;
    bool parityDiff = (a.w & 1) != (a.h & 1);
    bool is90or270 = (std::fabs(rotDeg - 90.0) < 1.0 || std::fabs(rotDeg - 270.0) < 1.0);
    float boxLeft, boxTop, pivX, pivY;
    if (is90or270 && parityDiff) {
        boxLeft = (float)std::round(ccx - hw); boxTop = (float)std::round(ccy - hh);
        pivX = boxLeft + (float)std::round(hw); pivY = boxTop + (float)std::round(hh);
    } else {
        boxLeft = ccx - hw; boxTop = ccy - hh;
        pivX = ccx; pivY = ccy;
    }

    int wX, wY; SDL_GetMouseState(&wX, &wY);
    {
        int rhwx, rhwy; getRotateHandleWin(rhwx, rhwy);
        const int G = GRAB_WIN;
        if (std::abs(wX - rhwx) <= G && std::abs(wY - rhwy) <= G)
            return Handle::ROTATE;
    }

    struct CPt { float x, y; Handle h; };
    CPt pts[] = {
        { boxLeft, boxTop, Handle::NW }, { pivX, boxTop, Handle::N  }, { boxLeft + a.w, boxTop, Handle::NE },
        { boxLeft, pivY, Handle::W  },                 { boxLeft + a.w, pivY, Handle::E  },
        { boxLeft, boxTop + a.h, Handle::SW }, { pivX, boxTop + a.h, Handle::S  }, { boxLeft + a.w, boxTop + a.h, Handle::SE },
    };
    const int G = GRAB_WIN;
    for (auto& p : pts) {
        float rx, ry;
        rotatePt(p.x, p.y, pivX, pivY, rot, rx, ry);
        int wpx, wpy;
        mapper->getWindowCoords((int)std::round(rx), (int)std::round(ry), &wpx, &wpy);
        if (std::abs(wX - wpx) <= G && std::abs(wY - wpy) <= G)
            return p.h;
    }
    return Handle::NONE;
}

void TransformTool::syncDrawCenterFromBounds() {
    drawCenterX = currentBounds.x + currentBounds.w * 0.5f;
    drawCenterY = currentBounds.y + currentBounds.h * 0.5f;
}

float TransformTool::getRotation() const {
    if (isRotating && (SDL_GetModState() & KMOD_SHIFT)) {
        const float snap45 = (float)M_PI / 4.f;
        return std::round(rotation / snap45) * snap45;
    }
    return rotation;
}

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

        float ccx = currentBounds.x + currentBounds.w * 0.5f;
        float ccy = currentBounds.y + currentBounds.h * 0.5f;
        float ancLX, ancLY;
        if (h == Handle::W || h == Handle::NW || h == Handle::SW)
            ancLX = (float)(currentBounds.x + currentBounds.w);
        else if (h == Handle::E || h == Handle::NE || h == Handle::SE)
            ancLX = (float)currentBounds.x;
        else
            ancLX = ccx;

        if (h == Handle::N || h == Handle::NW || h == Handle::NE)
            ancLY = (float)(currentBounds.y + currentBounds.h);
        else if (h == Handle::S || h == Handle::SW || h == Handle::SE)
            ancLY = (float)currentBounds.y;
        else
            ancLY = ccy;

        anchorX = (int)std::round(ancLX);
        anchorY = (int)std::round(ancLY);
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
        float delta = angle - rotLastAngle;
        if (delta >  (float)M_PI) delta -= 2.f * (float)M_PI;
        if (delta < -(float)M_PI) delta += 2.f * (float)M_PI;
        rotLastAngle = angle;
        rotation += delta;
        if (!aspectLock && (currentBounds.w % 2) != (currentBounds.h % 2)) {
            const float quarterEps = (float)M_PI / 180.f * 0.5f;  // 0.5°
            float q = std::round(rotation / ((float)M_PI * 0.5f)) * ((float)M_PI * 0.5f);
            if (std::fabs(rotation - q) < quarterEps) rotation = q;
        }
        return true;
    }

    if (resizing != Handle::NONE) {
        moved = true;
        float c = std::cos(-rotation), s = std::sin(-rotation);
        auto unrotMouse = [&](float& outXl, float& outYl) {
            float dxW = (float)cX - anchorWorldX;
            float dyW = (float)cY - anchorWorldY;
            outXl = (float)anchorX + dxW * c - dyW * s;
            outYl = (float)anchorY + dxW * s + dyW * c;
        };
        float cXl, cYl;
        unrotMouse(cXl, cYl);

        int newX = currentBounds.x, newY = currentBounds.y;
        int newW = currentBounds.w, newH = currentBounds.h;

        bool affectsX = (resizing == Handle::W  || resizing == Handle::NW || resizing == Handle::SW ||
                         resizing == Handle::E  || resizing == Handle::NE || resizing == Handle::SE);
        if (affectsX) {
            bool dragFromRight = (resizing == Handle::W || resizing == Handle::NW || resizing == Handle::SW);
            float rawW = cXl - (float)anchorX;

            if (dragFromRight && rawW > 0.f) {
                flipX = !flipX;
                resizing = (resizing == Handle::W)  ? Handle::E  :
                           (resizing == Handle::NW) ? Handle::NE : Handle::SE;
                {
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    float ua = ccx2 + dxA * co2 - dyA * si2;
                    anchorX = (int)std::round(ua);
                }
                dragFromRight = false;
                unrotMouse(cXl, cYl);
                rawW = cXl - (float)anchorX;
            } else if (!dragFromRight && rawW < 0.f) {
                flipX = !flipX;
                resizing = (resizing == Handle::E)  ? Handle::W  :
                           (resizing == Handle::NE) ? Handle::NW : Handle::SW;
                {
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    float ua = ccx2 + dxA * co2 - dyA * si2;
                    anchorX = (int)std::round(ua);
                }
                dragFromRight = true;
                unrotMouse(cXl, cYl);
                rawW = cXl - (float)anchorX;
            }

            newW = std::max(1, (int)std::round(std::abs(rawW)));
            if (dragFromRight)
                newX = (int)std::round((float)anchorX - (float)newW);
            else
                newX = anchorX;
        }

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
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    float ua = ccy2 + dxA * si2 + dyA * co2;
                    anchorY = (int)std::round(ua);
                }
                dragFromBottom = false;
                unrotMouse(cXl, cYl);
                rawH = cYl - (float)anchorY;
            } else if (!dragFromBottom && rawH < 0.f) {
                flipY = !flipY;
                resizing = (resizing == Handle::S)  ? Handle::N  :
                           (resizing == Handle::SW) ? Handle::NW : Handle::NE;
                {
                    float co2 = std::cos(-rotation), si2 = std::sin(-rotation);
                    float ccx2 = currentBounds.x + currentBounds.w * 0.5f;
                    float ccy2 = currentBounds.y + currentBounds.h * 0.5f;
                    float dxA = anchorWorldX - ccx2, dyA = anchorWorldY - ccy2;
                    float ua = ccy2 + dxA * si2 + dyA * co2;
                    anchorY = (int)std::round(ua);
                }
                dragFromBottom = true;
                unrotMouse(cXl, cYl);
                rawH = cYl - (float)anchorY;
            }

            newH = std::max(1, (int)std::round(std::abs(rawH)));
            if (dragFromBottom)
                newY = (int)std::round((float)anchorY - (float)newH);
            else
                newY = anchorY;
        }

        if (aspectLock && dragAspect > 0.f &&
            (resizing == Handle::NW || resizing == Handle::NE ||
             resizing == Handle::SW || resizing == Handle::SE)) {
            int wFromH = std::max(1, (int)std::round(newH * dragAspect));
            int hFromW = std::max(1, (int)std::round(newW / dragAspect));
            if (wFromH <= newW) {
                newW = wFromH;
                if (rotation == 0.f) {
                    bool fromRight = (resizing == Handle::NW || resizing == Handle::SW);
                    if (fromRight) newX = (int)std::round(anchorWorldX) - newW;
                }
            } else {
                newH = hFromW;
                newW = std::max(1, (int)std::round(newH * dragAspect));
                if (rotation == 0.f) {
                    bool fromBottom = (resizing == Handle::NW || resizing == Handle::NE);
                    if (fromBottom) newY = (int)std::round(anchorWorldY) - newH;
                    bool fromRight  = (resizing == Handle::NW || resizing == Handle::SW);
                    if (fromRight)  newX = (int)std::round(anchorWorldX) - newW;
                }
            }
        }

        if (rotation != 0.f) {
            float hw = newW * 0.5f, hh = newH * 0.5f;

            float offX = 0.f, offY = 0.f;
            bool dragsLeft   = (resizing == Handle::W  || resizing == Handle::NW || resizing == Handle::SW);
            bool dragsRight  = (resizing == Handle::E  || resizing == Handle::NE || resizing == Handle::SE);
            bool dragsTop    = (resizing == Handle::N  || resizing == Handle::NW || resizing == Handle::NE);
            bool dragsBottom = (resizing == Handle::S  || resizing == Handle::SW || resizing == Handle::SE);

            if      (dragsLeft)   offX = +hw;
            else if (dragsRight)  offX = -hw;
            if      (dragsTop)    offY = +hh;
            else if (dragsBottom) offY = -hh;

            float co = std::cos(rotation), si = std::sin(rotation);
            float cx = anchorWorldX - (co * offX - si * offY);
            float cy = anchorWorldY - (si * offX + co * offY);
            newX = (int)std::round(cx - hw);
            newY = (int)std::round(cy - hh);
            drawCenterX = cx;
            drawCenterY = cy;
        }

        snapBounds(newX, newY, newW, newH);
        currentBounds = { newX, newY, newW, newH };
        if (rotation == 0.f) syncDrawCenterFromBounds();
        return true;
    }

    if (isMoving) {
        moved = true;
        currentBounds.x = cX - dragOffX;
        currentBounds.y = cY - dragOffY;
        syncDrawCenterFromBounds();
        return true;
    }
    return false;
}

void TransformTool::handleMouseUp() {
    if (isRotating && (SDL_GetModState() & KMOD_SHIFT)) {
        const float snap45 = (float)M_PI / 4.f;
        rotation = std::round(rotation / snap45) * snap45;
    }
    resizing   = Handle::NONE;
    isMoving   = false;
    isRotating = false;
}

void TransformTool::drawHandles(SDL_Renderer* winRenderer) const {
    float ccx = drawCenterX, ccy = drawCenterY;
    float rot = getRotation();
    float hw = currentBounds.w * 0.5f, hh = currentBounds.h * 0.5f;
    double rotDeg = std::fmod(rot * 180.0 / M_PI, 360.0);
    if (rotDeg < 0.0) rotDeg += 360.0;
    bool parityDiff = (currentBounds.w & 1) != (currentBounds.h & 1);
    bool is90or270 = (std::fabs(rotDeg - 90.0) < 1.0 || std::fabs(rotDeg - 270.0) < 1.0);
    float boxLeft, boxTop, pivX, pivY;
    if (is90or270 && parityDiff) {
        float rx = (float)std::round(ccx - hw), ry = (float)std::round(ccy - hh);
        boxLeft = rx; boxTop = ry;
        pivX = rx + (float)std::round(hw);
        pivY = ry + (float)std::round(hh);
    } else {
        boxLeft = ccx - hw; boxTop = ccy - hh;
        pivX = ccx; pivY = ccy;
    }
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
    {
        float corners[4][2] = {
            { boxLeft, boxTop },
            { boxLeft + currentBounds.w, boxTop },
            { boxLeft + currentBounds.w, boxTop + currentBounds.h },
            { boxLeft, boxTop + currentBounds.h },
        };
        SDL_Point wpts[4];
        for (int i = 0; i < 4; ++i) {
            float rx, ry;
            rotatePt(corners[i][0], corners[i][1], pivX, pivY, rot, rx, ry);
            mapper->getWindowCoords((int)std::round(rx), (int)std::round(ry), &wpts[i].x, &wpts[i].y);
        }
        drawDashed(wpts[0].x, wpts[0].y, wpts[1].x, wpts[1].y);
        drawDashed(wpts[1].x, wpts[1].y, wpts[2].x, wpts[2].y);
        drawDashed(wpts[2].x, wpts[2].y, wpts[3].x, wpts[3].y);
        drawDashed(wpts[3].x, wpts[3].y, wpts[0].x, wpts[0].y);
    }
    const int hs = 4;
    struct CPt { float x, y; };
    CPt pts[] = {
        { boxLeft, boxTop }, { pivX, boxTop }, { boxLeft + currentBounds.w, boxTop },
        { boxLeft, pivY },                     { boxLeft + currentBounds.w, pivY },
        { boxLeft, boxTop + currentBounds.h }, { pivX, boxTop + currentBounds.h }, { boxLeft + currentBounds.w, boxTop + currentBounds.h },
    };
    float sinR = std::sin(rot), cosR = std::cos(rot);
    float uWx =  cosR * hs, uWy =  sinR * hs;
    float vWx = -sinR * hs, vWy =  cosR * hs;

    auto fillQuad = [&](SDL_Point q[4]) {
        int yMin = q[0].y, yMax = q[0].y;
        for (int i = 1; i < 4; i++) { yMin = std::min(yMin, q[i].y); yMax = std::max(yMax, q[i].y); }
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
        rotatePt(p.x, p.y, pivX, pivY, rot, rx, ry);
        int wpx, wpy;
        mapper->getWindowCoords((int)std::round(rx), (int)std::round(ry), &wpx, &wpy);

        SDL_Point corners[4] = {
            { (int)std::round(wpx - uWx - vWx), (int)std::round(wpy - uWy - vWy) },
            { (int)std::round(wpx + uWx - vWx), (int)std::round(wpy + uWy - vWy) },
            { (int)std::round(wpx + uWx + vWx), (int)std::round(wpy + uWy + vWy) },
            { (int)std::round(wpx - uWx + vWx), (int)std::round(wpy - uWy + vWy) },
        };
        SDL_SetRenderDrawColor(winRenderer, 255, 255, 255, 255);
        fillQuad(corners);
        SDL_SetRenderDrawColor(winRenderer, 0, 0, 0, 255);
        for (int i = 0; i < 4; i++)
            SDL_RenderDrawLine(winRenderer, corners[i].x, corners[i].y,
                               corners[(i+1)%4].x, corners[(i+1)%4].y);
    }
    {
        float rnx, rny;
        rotatePt(ccx, ccy - hh, ccx, ccy, rot, rnx, rny);
        int nwx, nwy;
        mapper->getWindowCoords((int)std::round(rnx), (int)std::round(rny), &nwx, &nwy);

        int rhwx, rhwy;
        getRotateHandleWin(rhwx, rhwy);

        drawDashed(nwx, nwy, rhwx, rhwy);
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

bool TransformTool::isHit(int cX, int cY) const {
    return pointInRotatedBounds(cX, cY) || getHandle(cX, cY) != Handle::NONE;
}

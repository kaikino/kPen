#include "ViewController.h"
#include <cmath>
#include <algorithm>

SDL_Rect ViewController::getFitViewport(int winW, int winH, int canvasW, int canvasH) {
    int availW = winW - Toolbar::TB_W;
    int fitW = availW - GAP * 2;
    int fitH = winH - GAP * 2;
    if (fitW < 1) fitW = 1;
    if (fitH < 1) fitH = 1;
    float canvasAspect = (float)canvasW / canvasH;
    float windowAspect = (float)fitW / fitH;
    SDL_Rect v;
    if (windowAspect > canvasAspect) {
        v.h = fitH; v.w = (int)(fitH * canvasAspect);
        v.x = Toolbar::TB_W + GAP + (fitW - v.w) / 2; v.y = GAP;
    } else {
        v.w = fitW; v.h = (int)(fitW / canvasAspect);
        v.x = Toolbar::TB_W + GAP; v.y = GAP + (fitH - v.h) / 2;
    }
    return v;
}

void ViewController::getMaxPan(int winW, int winH, int canvasW, int canvasH,
                               float* maxPanX, float* maxPanY) const {
    SDL_Rect fit = getFitViewport(winW, winH, canvasW, canvasH);
    float zW = fit.w * zoom_;
    float zH = fit.h * zoom_;
    *maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
    *maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;
}

SDL_Rect ViewController::getViewport(int winW, int winH, int canvasW, int canvasH) const {
    SDL_FRect f = getViewportF(winW, winH, canvasW, canvasH);
    int x = (int)std::floor(f.x);
    int y = (int)std::floor(f.y);
    int x2 = (int)std::ceil(f.x + f.w);
    int y2 = (int)std::ceil(f.y + f.h);
    return { x, y, x2 - x, y2 - y };
}

SDL_FRect ViewController::getViewportF(int winW, int winH, int canvasW, int canvasH) const {
    SDL_Rect fit = getFitViewport(winW, winH, canvasW, canvasH);
    float zW = fit.w * zoom_;
    float zH = fit.h * zoom_;
    float x = fit.x + (fit.w - zW) / 2.f + panX_;
    float y = fit.y + (fit.h - zH) / 2.f + panY_;
    return { x, y, zW, zH };
}

void ViewController::zoomAround(float newZoom, int pivotWinX, int pivotWinY,
                                int winW, int winH, int canvasW, int canvasH) {
    SDL_Rect fit = getFitViewport(winW, winH, canvasW, canvasH);
    float dz = newZoom / zoom_;
    float pivotRelX = pivotWinX - (fit.x + fit.w / 2.f);
    float pivotRelY = pivotWinY - (fit.y + fit.h / 2.f);
    panX_ = pivotRelX + (panX_ - pivotRelX) * dz;
    panY_ = pivotRelY + (panY_ - pivotRelY) * dz;
    zoom_ = newZoom;
}

void ViewController::addPanDelta(float winDx, float winDy,
                                 int winW, int winH, int canvasW, int canvasH) {
    scrollFromMouseWheel_ = false;
    float maxPanX, maxPanY;
    getMaxPan(winW, winH, canvasW, canvasH, &maxPanX, &maxPanY);
    panX_ = std::max(-maxPanX, std::min(maxPanX, panX_ + winDx));
    panY_ = std::max(-maxPanY, std::min(maxPanY, panY_ + winDy));
}

void ViewController::onCanvasScroll(int winX, int winY, float dy, bool ctrl,
                                    int winW, int winH, int canvasW, int canvasH) {
    if (!ctrl) return;
    scrollFromMouseWheel_ = false;
    SDL_Rect fit = getFitViewport(winW, winH, canvasW, canvasH);

    if (!viewScrolling_) {
        viewScrollBaseZoom_ = zoom_;
        viewScrollRawZoom_  = 0.f;
        viewScrolling_      = true;
    } else if (viewScrollRawX_ != 0.f || viewScrollRawY_ != 0.f) {
        viewScrollBaseZoom_ = zoom_;
        viewScrollRawZoom_  = 0.f;
        viewScrollRawX_     = 0.f;
        viewScrollRawY_     = 0.f;
    }
    viewScrollRawZoom_ += dy * 0.1f;

    float rawZoom = viewScrollBaseZoom_ * expf(viewScrollRawZoom_);
    const float kz = 0.3f;
    if (rawZoom < MIN_ZOOM) {
        float rawOver = MIN_ZOOM / rawZoom - 1.f;
        float dispOver = rawOver * kz / (rawOver + kz);
        rawZoom = MIN_ZOOM / (1.f + dispOver);
    } else if (rawZoom > MAX_ZOOM) {
        float rawOver = rawZoom / MAX_ZOOM - 1.f;
        float dispOver = rawOver * kz / (rawOver + kz);
        rawZoom = MAX_ZOOM * (1.f + dispOver);
    }
    zoomTarget_ = std::max(MIN_ZOOM, std::min(MAX_ZOOM, rawZoom));
}

void ViewController::onWheelPan(float dx, float dy) {
    const float cap = 400.f;
    wheelAccumX_ += dx;
    wheelAccumY_ += dy;
    wheelAccumX_ = std::max(-cap, std::min(cap, wheelAccumX_));
    wheelAccumY_ = std::max(-cap, std::min(cap, wheelAccumY_));
    viewScrolling_ = true;
    scrollWheelWasVertical_ = std::abs(dy) >= std::abs(dx);
}

bool ViewController::tickView(int winW, int winH, int canvasW, int canvasH,
                              bool pinchActive, bool twoFingerPivotSet, float twoFingerPivotX, float twoFingerPivotY,
                              int mousePivotX, int mousePivotY) {
    bool animating = false;
    const float k = 0.18f;

    int pivotX = (pinchActive && twoFingerPivotSet) ? (int)twoFingerPivotX : mousePivotX;
    int pivotY = (pinchActive && twoFingerPivotSet) ? (int)twoFingerPivotY : mousePivotY;

    float clamped = std::max(MIN_ZOOM, std::min(MAX_ZOOM, zoomTarget_));
    float diff = clamped - zoom_;
    if (std::abs(diff) > 0.0002f) {
        zoomAround(zoom_ + diff * k, pivotX, pivotY, winW, winH, canvasW, canvasH);
        animating = true;
    } else if (zoom_ != clamped) {
        zoomAround(clamped, pivotX, pivotY, winW, winH, canvasW, canvasH);
    }

    SDL_Rect fit = getFitViewport(winW, winH, canvasW, canvasH);
    float zW = fit.w * zoom_;
    float zH = fit.h * zoom_;
    float maxPanX = std::max(0.f, (zW - fit.w) / 2.f) + PAN_SLACK;
    float maxPanY = std::max(0.f, (zH - fit.h) / 2.f) + PAN_SLACK;
    if (std::abs(wheelAccumX_) > 0.01f || std::abs(wheelAccumY_) > 0.01f) {
        const float applyFactor = 0.45f;
        const float decay = 0.88f;
        panX_ += wheelAccumX_ * applyFactor;
        panY_ += wheelAccumY_ * applyFactor;
        panX_ = std::max(-maxPanX, std::min(maxPanX, panX_));
        panY_ = std::max(-maxPanY, std::min(maxPanY, panY_));
        wheelAccumX_ *= decay;
        wheelAccumY_ *= decay;
        animating = true;
    }

    if (viewScrolling_) return animating;

    auto snapAxis = [&](float& pan, float maxPan) {
        float lo = -maxPan, hi = maxPan;
        if (pan < lo)      { pan += (lo - pan) * k; if (std::abs(pan-lo)<0.5f) pan=lo; else animating=true; }
        else if (pan > hi) { pan += (hi - pan) * k; if (std::abs(pan-hi)<0.5f) pan=hi; else animating=true; }
    };
    snapAxis(panX_, maxPanX);
    snapAxis(panY_, maxPanY);

    return animating;
}

bool ViewController::getScrollbarRects(int winW, int winH, int canvasW, int canvasH,
                                       SDL_Rect* trackV, SDL_Rect* thumbV, SDL_Rect* trackH, SDL_Rect* thumbH,
                                       bool* hasV, bool* hasH) const {
    const int SB_SZ = 8;
    const int THUMB_MIN = 24;
    SDL_Rect fit = getFitViewport(winW, winH, canvasW, canvasH);
    float zW = fit.w * zoom_;
    float zH = fit.h * zoom_;
    float maxPanX, maxPanY;
    getMaxPan(winW, winH, canvasW, canvasH, &maxPanX, &maxPanY);

    *hasV = (zH > (float)fit.h && maxPanY > 0.f);
    *hasH = (zW > (float)fit.w && maxPanX > 0.f);
    if (!*hasV && !*hasH) return false;

    if (*hasV) {
        float thumbRatio = (float)fit.h / zH;
        int thumbHpx = (int)(fit.h * thumbRatio);
        if (thumbHpx < THUMB_MIN) thumbHpx = THUMB_MIN;
        if (thumbHpx > fit.h) thumbHpx = fit.h;
        float range = (float)(fit.h - thumbHpx);
        float norm = (panY_ + maxPanY) / (2.f * maxPanY);
        if (norm < 0.f) norm = 0.f;
        if (norm > 1.f) norm = 1.f;
        int thumbY = fit.y + (int)(norm * range + 0.5f);
        trackV->x = winW - SB_SZ;
        trackV->y = fit.y;
        trackV->w = SB_SZ;
        trackV->h = fit.h;
        thumbV->x = winW - SB_SZ;
        thumbV->y = thumbY;
        thumbV->w = SB_SZ;
        thumbV->h = thumbHpx;
    }
    if (*hasH) {
        float thumbRatio = (float)fit.w / zW;
        int thumbW = (int)(fit.w * thumbRatio);
        if (thumbW < THUMB_MIN) thumbW = THUMB_MIN;
        if (thumbW > fit.w) thumbW = fit.w;
        float range = (float)(fit.w - thumbW);
        float norm = (panX_ + maxPanX) / (2.f * maxPanX);
        if (norm < 0.f) norm = 0.f;
        if (norm > 1.f) norm = 1.f;
        int thumbX = fit.x + (int)(norm * range + 0.5f);
        trackH->x = fit.x;
        trackH->y = winH - SB_SZ;
        trackH->w = fit.w;
        trackH->h = SB_SZ;
        thumbH->x = thumbX;
        thumbH->y = winH - SB_SZ;
        thumbH->w = thumbW;
        thumbH->h = SB_SZ;
    }
    return true;
}

void ViewController::getScrollbarHover(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                                       bool* hoverV, bool* hoverH, bool* hasV, bool* hasH) const {
    SDL_Rect trackV, thumbV, trackH, thumbH;
    *hoverV = false;
    *hoverH = false;
    *hasV = false;
    *hasH = false;
    if (!getScrollbarRects(winW, winH, canvasW, canvasH, &trackV, &thumbV, &trackH, &thumbH, hasV, hasH)) return;
    SDL_Point pt = { mx, my };
    if (*hasV) *hoverV = SDL_PointInRect(&pt, &trackV) || SDL_PointInRect(&pt, &thumbV);
    if (*hasH) *hoverH = SDL_PointInRect(&pt, &trackH) || SDL_PointInRect(&pt, &thumbH);
}

void ViewController::setPanFromScrollbarThumb(bool vertical, int thumbPos,
                                              int winW, int winH, int canvasW, int canvasH) {
    SDL_Rect trackV, thumbV, trackH, thumbH;
    bool hasV, hasH;
    if (!getScrollbarRects(winW, winH, canvasW, canvasH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH)) return;
    if (vertical && hasV) {
        int thumbY = thumbPos;
        if (thumbY < trackV.y) thumbY = trackV.y;
        if (thumbY > trackV.y + trackV.h - thumbV.h) thumbY = trackV.y + trackV.h - thumbV.h;
        float maxPanXUnused, maxPanY;
        getMaxPan(winW, winH, canvasW, canvasH, &maxPanXUnused, &maxPanY);
        float range = (float)(trackV.h - thumbV.h);
        if (range > 0.f)
            panY_ = (float)(thumbY - trackV.y) / range * 2.f * maxPanY - maxPanY;
    } else if (!vertical && hasH) {
        int thumbX = thumbPos;
        if (thumbX < trackH.x) thumbX = trackH.x;
        if (thumbX > trackH.x + trackH.w - thumbH.w) thumbX = trackH.x + trackH.w - thumbH.w;
        float maxPanX, maxPanYUnused;
        getMaxPan(winW, winH, canvasW, canvasH, &maxPanX, &maxPanYUnused);
        float range = (float)(trackH.w - thumbH.w);
        if (range > 0.f)
            panX_ = (float)(thumbX - trackH.x) / range * 2.f * maxPanX - maxPanX;
    }
}

bool ViewController::onScrollbarMouseDown(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                                         bool* consumed) {
    SDL_Rect trackV, thumbV, trackH, thumbH;
    bool hasV, hasH;
    if (!getScrollbarRects(winW, winH, canvasW, canvasH, &trackV, &thumbV, &trackH, &thumbH, &hasV, &hasH)) {
        *consumed = false;
        return false;
    }
    SDL_Point pt = { mx, my };
    if (hasV && SDL_PointInRect(&pt, &trackV)) {
        if (SDL_PointInRect(&pt, &thumbV)) {
            scrollbarDragOffsetY_ = my - thumbV.y;
            scrollbarDragV_ = true;
        } else {
            int thumbY = (my < thumbV.y) ? my : (my - thumbV.h);
            scrollbarDragOffsetY_ = (my < thumbV.y) ? 0 : thumbV.h;
            setPanFromScrollbarThumb(true, thumbY, winW, winH, canvasW, canvasH);
            scrollbarDragV_ = true;
        }
        *consumed = true;
        return true;
    }
    if (hasH && SDL_PointInRect(&pt, &trackH)) {
        if (SDL_PointInRect(&pt, &thumbH)) {
            scrollbarDragOffsetX_ = mx - thumbH.x;
            scrollbarDragH_ = true;
        } else {
            int thumbX = (mx < thumbH.x) ? mx : (mx - thumbH.w);
            scrollbarDragOffsetX_ = (mx < thumbH.x) ? 0 : thumbH.w;
            setPanFromScrollbarThumb(false, thumbX, winW, winH, canvasW, canvasH);
            scrollbarDragH_ = true;
        }
        *consumed = true;
        return true;
    }
    *consumed = false;
    return false;
}

void ViewController::onScrollbarMouseMotion(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                                            bool* needsRedraw) {
    if (scrollbarDragV_) {
        setPanFromScrollbarThumb(true, my - scrollbarDragOffsetY_, winW, winH, canvasW, canvasH);
        *needsRedraw = true;
    }
    if (scrollbarDragH_) {
        setPanFromScrollbarThumb(false, mx - scrollbarDragOffsetX_, winW, winH, canvasW, canvasH);
        *needsRedraw = true;
    }
}

void ViewController::tickScrollbarFade(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                                       bool handPanning, bool* needsRedraw) {
    bool hoverV, hoverH, hasV, hasH;
    getScrollbarHover(winW, winH, mx, my, canvasW, canvasH, &hoverV, &hoverH, &hasV, &hasH);
    bool wheelShowV = viewScrolling_ && (!scrollFromMouseWheel_ || scrollWheelWasVertical_);
    bool wheelShowH = viewScrolling_ && (!scrollFromMouseWheel_ || !scrollWheelWasVertical_);
    bool wantVisibleV = hasV && (scrollbarDragV_ || hoverV || wheelShowV || handPanning);
    bool wantVisibleH = hasH && (scrollbarDragH_ || hoverH || wheelShowH || handPanning);
    const float SB_FADE_IN = 0.22f, SB_FADE_OUT = 0.028f;
    if (wantVisibleV)
        scrollbarAlphaV_ = std::min(1.f, scrollbarAlphaV_ + SB_FADE_IN);
    else
        scrollbarAlphaV_ = std::max(0.f, scrollbarAlphaV_ - SB_FADE_OUT);
    if (wantVisibleH)
        scrollbarAlphaH_ = std::min(1.f, scrollbarAlphaH_ + SB_FADE_IN);
    else
        scrollbarAlphaH_ = std::max(0.f, scrollbarAlphaH_ - SB_FADE_OUT);
    if ((scrollbarAlphaV_ > 0.001f && scrollbarAlphaV_ < 0.999f) || (scrollbarAlphaH_ > 0.001f && scrollbarAlphaH_ < 0.999f))
        *needsRedraw = true;
}

void ViewController::reset() {
    zoom_ = 1.f;
    zoomTarget_ = 1.f;
    panX_ = 0.f;
    panY_ = 0.f;
    viewScrolling_ = false;
    scrollFromMouseWheel_ = false;
    viewScrollRawX_  = 0.f;
    viewScrollRawY_  = 0.f;
    viewScrollRawZoom_ = 0.f;
    wheelAccumX_ = 0.f;
    wheelAccumY_ = 0.f;
}

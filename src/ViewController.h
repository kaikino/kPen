#pragma once
#include <SDL2/SDL.h>
#include "Toolbar.h"

class ViewController {
public:
    static constexpr float MIN_ZOOM  = 0.1f;
    static constexpr float MAX_ZOOM  = 20.f;
    static constexpr float PAN_SLACK = 50.f;
    static constexpr int GAP  = 50;

    ViewController() = default;

    float getZoom() const { return zoom_; }
    float getPanX() const { return panX_; }
    float getPanY() const { return panY_; }
    void setZoom(float z) { zoom_ = z; }
    void setPan(float px, float py) { panX_ = px; panY_ = py; }
    void setZoomTarget(float z) { zoomTarget_ = z; }

    bool getViewScrolling() const { return viewScrolling_; }
    void endScrollGesture() { viewScrolling_ = false; }
    bool getScrollbarDragV() const { return scrollbarDragV_; }
    bool getScrollbarDragH() const { return scrollbarDragH_; }
    void endScrollbarDrag() { scrollbarDragV_ = false; scrollbarDragH_ = false; }
    float getScrollbarAlphaV() const { return scrollbarAlphaV_; }
    float getScrollbarAlphaH() const { return scrollbarAlphaH_; }
    bool getScrollWheelWasVertical() const { return scrollWheelWasVertical_; }
    void setScrollWheelWasVertical(bool v) { scrollWheelWasVertical_ = v; }

    static SDL_Rect getFitViewport(int winW, int winH, int canvasW, int canvasH);

    SDL_Rect getViewport(int winW, int winH, int canvasW, int canvasH) const;
    SDL_FRect getViewportF(int winW, int winH, int canvasW, int canvasH) const;

    void zoomAround(float newZoom, int pivotWinX, int pivotWinY, int winW, int winH, int canvasW, int canvasH);
    void addPanDelta(float winDx, float winDy, int winW, int winH, int canvasW, int canvasH);
    void onCanvasScroll(int winX, int winY, float dy, bool ctrl, int winW, int winH, int canvasW, int canvasH);
    void onWheelPan(float dx, float dy);

    bool tickView(int winW, int winH, int canvasW, int canvasH,
                  bool pinchActive, bool twoFingerPivotSet, float twoFingerPivotX, float twoFingerPivotY,
                  int mousePivotX, int mousePivotY);

    bool getScrollbarRects(int winW, int winH, int canvasW, int canvasH,
                           SDL_Rect* trackV, SDL_Rect* thumbV, SDL_Rect* trackH, SDL_Rect* thumbH,
                           bool* hasV, bool* hasH) const;
    void getScrollbarHover(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                           bool* hoverV, bool* hoverH, bool* hasV, bool* hasH) const;
    void setPanFromScrollbarThumb(bool vertical, int thumbPos, int winW, int winH, int canvasW, int canvasH);

    bool onScrollbarMouseDown(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                              bool* consumed);
    void onScrollbarMouseMotion(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                                bool* needsRedraw);
    void tickScrollbarFade(int winW, int winH, int mx, int my, int canvasW, int canvasH,
                           bool handPanning, bool* needsRedraw);

    void reset();

private:
    float zoom_ = 1.f;
    float panX_ = 0.f;
    float panY_ = 0.f;
    float zoomTarget_ = 1.f;

    bool  viewScrolling_      = false;
    float viewScrollBaseX_    = 0.f;
    float viewScrollBaseY_    = 0.f;
    float viewScrollRawX_     = 0.f;
    float viewScrollRawY_     = 0.f;
    float viewScrollBaseZoom_ = 1.f;
    float viewScrollRawZoom_  = 0.f;
    float wheelAccumX_ = 0.f;
    float wheelAccumY_ = 0.f;

    bool scrollbarDragV_ = false;
    bool scrollbarDragH_ = false;
    int  scrollbarDragOffsetX_ = 0;
    int  scrollbarDragOffsetY_ = 0;
    float scrollbarAlphaV_ = 0.f;
    float scrollbarAlphaH_ = 0.f;
    bool scrollWheelWasVertical_ = true;

    void getMaxPan(int winW, int winH, int canvasW, int canvasH,
                   float* maxPanX, float* maxPanY) const;
};

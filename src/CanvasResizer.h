#pragma once
#include <SDL2/SDL.h>

class ICoordinateMapper;

// Canvas-edge drag handles to propose new size; top/left move origin (crop/pad), bottom/right extend.
class CanvasResizer {
  public:
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW };

    CanvasResizer(ICoordinateMapper* m) : mapper(m) {}

    void draw(SDL_Renderer* r, int canvasW, int canvasH) const;

    bool onMouseDown(int winX, int winY, int canvasW, int canvasH);

    bool onMouseMove(int winX, int winY,
                     int& previewW, int& previewH,
                     int& originX,  int& originY,
                     bool aspectLock = false) const;

    bool onMouseUp(int winX, int winY, int canvasW, int canvasH,
                   int& newW, int& newH, int& originX, int& originY,
                   bool aspectLock = false);

    bool isDragging() const { return activeHandle != Handle::NONE; }

    Handle hitTest(int winX, int winY, int canvasW, int canvasH) const;

  private:
    static const int HS  = 3;
    static const int HIT = 10;

    ICoordinateMapper* mapper;
    Handle activeHandle  = Handle::NONE;
    int dragStartWinX    = 0, dragStartWinY = 0;
    int dragBaseW        = 0, dragBaseH     = 0;

    struct HandlePos { Handle h; int wx, wy; };
    void getHandlePositions(int canvasW, int canvasH, HandlePos out[8]) const;

    void compute(int winX, int winY,
                 int& newW, int& newH,
                 int& originX, int& originY,
                 bool aspectLock = false) const;
};

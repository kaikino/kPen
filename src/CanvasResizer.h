#pragma once
#include <SDL2/SDL.h>

class ICoordinateMapper;

// Draws 8 drag handles (4 corners + 4 edges) around the canvas boundary in
// window space, and lets the user drag them to propose a new canvas size.
//
// Top/left handles move the top-left origin — content is cropped or padded
// on those edges. Bottom/right handles extend the canvas rightward/downward.
class CanvasResizer {
  public:
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW };

    CanvasResizer(ICoordinateMapper* m) : mapper(m) {}

    void draw(SDL_Renderer* r, int canvasW, int canvasH) const;

    bool onMouseDown(int winX, int winY, int canvasW, int canvasH);

    // Updates live preview. originX/Y are the shift of the top-left corner in
    // canvas pixels (negative = canvas grew upward/leftward).
    bool onMouseMove(int winX, int winY,
                     int& previewW, int& previewH,
                     int& originX,  int& originY,
                     bool aspectLock = false) const;

    // Returns true if size changed. Fills new dims and origin shift.
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

    // Computes new size AND the origin shift caused by top/left handle movement.
    void compute(int winX, int winY,
                 int& newW, int& newH,
                 int& originX, int& originY,
                 bool aspectLock = false) const;
};

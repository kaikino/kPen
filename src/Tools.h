#pragma once

#include <SDL2/SDL.h>
#include <functional>
#include <vector>
#include "DrawingUtils.h"

enum class ToolType { BRUSH, ERASER, LINE, RECT, CIRCLE, SELECT, FILL, RESIZE };

class ICoordinateMapper {
  public:
    virtual void getCanvasCoords(int winX, int winY, int* canX, int* canY) = 0;
    virtual void getWindowCoords(int canX, int canY, int* winX, int* winY) = 0;
    virtual int  getWindowSize(int canSize) = 0;
    virtual void getCanvasSize(int* w, int* h) = 0;  // runtime canvas dimensions

    // Clamp (cx,cy) to the canvas edge along the ray from (sx,sy).
    void clampToCanvasEdge(int sx, int sy, int& cx, int& cy) {
        int cw, ch; getCanvasSize(&cw, &ch);
        if (cx >= 0 && cx < cw && cy >= 0 && cy < ch) return;
        float dx = (float)(cx - sx), dy = (float)(cy - sy);
        float t = 1.f;
        if (dx != 0.f) { float v = (dx > 0 ? cw - 1 - sx : -sx) / dx; if (v > 0.f && v < t) t = v; }
        if (dy != 0.f) { float v = (dy > 0 ? ch - 1 - sy : -sy) / dy; if (v > 0.f && v < t) t = v; }
        cx = std::max(0, std::min(cw - 1, sx + (int)(dx * t)));
        cy = std::max(0, std::min(ch - 1, sy + (int)(dy * t)));
    }
};

class AbstractTool {
  protected:
    ICoordinateMapper* mapper;
    bool isDrawing = false;
    int  startX = 0, startY = 0, lastX = 0, lastY = 0;
  public:
    AbstractTool(ICoordinateMapper* m) : mapper(m) {}
    virtual ~AbstractTool() {}
    bool isActive()  const { return isDrawing; }
    void getStart(int* sx, int* sy) const { *sx = startX; *sy = startY; }
    virtual void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual bool onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual void onOverlayRender(SDL_Renderer* r);
    virtual bool hasOverlayContent();
    virtual void deactivate(SDL_Renderer* r);
};

// ── TransformTool — shared handle/move logic for Select and Resize ────────────

class TransformTool : public AbstractTool {
  public:
    // Handle is public so CursorManager can switch on it directly.
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW, ROTATE };

  protected:
    static const int GRAB_WIN    = 4;   // hit radius in window pixels for square handles
    static const int ROT_OFFSET  = 28;  // distance above N handle in window pixels

    SDL_Rect currentBounds = {0, 0, 0, 0};
    float    rotation      = 0.f;  // radians, clockwise positive

    Handle resizing    = Handle::NONE;
    bool   isRotating  = false;
    bool   isMoving    = false;
    bool   moved       = false;
    int    anchorX     = 0, anchorY  = 0;
    int    dragOffX    = 0, dragOffY = 0;
    float  dragAspect  = 1.f;  // w/h of currentBounds captured at handleMouseDown
    bool   flipX       = false;
    bool   flipY       = false;
    float  rotPivotCX  = 0.f;  // canvas-space center at rotation drag start
    float  rotPivotCY  = 0.f;
    float  rotStartAngle = 0.f; // angle from pivot to mouse at drag start
    float  rotBaseAngle  = 0.f; // rotation value at drag start
    float  rotLastAngle  = 0.f; // raw atan2 angle from previous frame (for wraparound)
    float  anchorWorldX  = 0.f; // world-space position of resize anchor (set at mousedown)
    float  anchorWorldY  = 0.f;

    // Returns window-space position of the rotate handle circle center.
    void   getRotateHandleWin(int& wx, int& wy) const;
    Handle getHandle(int cX, int cY) const;
    void   drawHandles(SDL_Renderer* r) const;
    bool   handleMouseDown(int cX, int cY);
    bool   handleMouseMove(int cX, int cY, bool aspectLock = false);
    void   handleMouseUp();

    // Transform a canvas point through the current rotation about the bounds center.
    // Returns rotated canvas coordinates.
    void rotatePt(float inX, float inY, float pivX, float pivY,
                  float angle, float& outX, float& outY) const;
    // Test whether a canvas point lies inside the (possibly rotated) bounds.
    bool pointInRotatedBounds(int cX, int cY) const;

  public:
    using AbstractTool::AbstractTool;
    bool isHit              (int cX, int cY) const;
    bool hasMoved           () const { return moved; }
    bool isMutating         () const { return isMoving || isRotating || resizing != Handle::NONE; }
    SDL_Rect getFloatingBounds() const { return currentBounds; }
    float    getRotation()      const { return rotation; }
    // Used by CursorManager to pick the right resize-arrow cursor.
    Handle getHandleForCursor(int cX, int cY) const { return getHandle(cX, cY); }
};

// ── SelectTool ────────────────────────────────────────────────────────────────

class SelectTool : public TransformTool {
    SDL_Texture* selectionTexture = nullptr;
    bool         active           = false;
    bool         dirty            = false;  // true if canvas will change on commit (paste or moved)
  public:
    using TransformTool::TransformTool;
    ~SelectTool();
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    bool onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onOverlayRender(SDL_Renderer* r) override;
    void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void deactivate (SDL_Renderer* r) override;
    bool hasOverlayContent() override { return active; }
    bool isSelectionActive() const    { return active; }
    bool isDirty()           const    { return dirty || hasMoved() || rotation != 0.f; }
    bool isHit(int cX, int cY) const;
    void activateWithTexture(SDL_Texture* tex, SDL_Rect area);
    void setBounds(SDL_Rect area) { currentBounds = area; }
    std::vector<uint32_t> getFloatingPixels(SDL_Renderer* r) const;
  private:
    // Render the selection texture into renderer r with the current rotation,
    // flip, and bounds. dst is in the coordinate space of r (canvas or window).
    void renderWithTransform(SDL_Renderer* r, const SDL_Rect& dst) const;
};

// ── ResizeTool ────────────────────────────────────────────────────────────────

class ResizeTool : public TransformTool {
    ToolType         shapeType;
    SDL_Rect         origBounds;
    int              shapeStartX, shapeStartY, shapeEndX, shapeEndY;
    const SDL_Color* liveColor;   // points to toolbar.brushColor — always current

    void renderShape(SDL_Renderer* r, const SDL_Rect& bounds,
                     int bs, SDL_Color col, int clipW = 0, int clipH = 0) const;
    void renderShapeRotated(SDL_Renderer* r, SDL_Color col, int clipW, int clipH) const;
  public:
    int*      liveBrushSize;  // points to toolbar.brushSize — always current
    bool      shapeFilled;
    ResizeTool(ICoordinateMapper* m, ToolType shapeType, SDL_Rect bounds, SDL_Rect origBounds,
               int sx, int sy, int ex, int ey, int* liveBrushSize, const SDL_Color* liveColor, bool filled = false);
    ~ResizeTool();
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    bool onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onOverlayRender(SDL_Renderer* r) override;
    void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void deactivate (SDL_Renderer* r) override;
    bool hasOverlayContent() override { return true; }
    bool isHit(int cX, int cY) const  { return TransformTool::isHit(cX, cY); }
    SDL_Rect getBounds() const        { return currentBounds; }
    bool willRender() const;  // true if the shape will produce visible pixels at current bounds/brushSize
    std::vector<uint32_t> getFloatingPixels(SDL_Renderer* r) const;
};

// ── Other tools ───────────────────────────────────────────────────────────────

class BrushTool : public AbstractTool {
  public:
    bool squareBrush = false;
    BrushTool(ICoordinateMapper* m, bool square = false) : AbstractTool(m), squareBrush(square) {}
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

class EraserTool : public AbstractTool {
  public:
    bool squareBrush = false;
    EraserTool(ICoordinateMapper* m, bool square = false) : AbstractTool(m), squareBrush(square) {}
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

class ShapeTool : public AbstractTool {
    ToolType type;
    using ShapeReadyCallback = std::function<void(ToolType, SDL_Rect, SDL_Rect, int, int, int, int, int, SDL_Color, bool)>;
    ShapeReadyCallback onShapeReady;
    // Cached each frame so onOverlayRender can draw with the correct brush/color
    int       cachedBrushSize = 1;
    SDL_Color cachedColor     = {0, 0, 0, 255};
  public:
    bool filled = false;
    ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb, bool filled = false);
    bool onMouseUp       (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onPreviewRender (SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onOverlayRender (SDL_Renderer* r) override;
    bool hasOverlayContent() override { return isDrawing; }
};

class FillTool : public AbstractTool {
  public:
    using AbstractTool::AbstractTool;
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

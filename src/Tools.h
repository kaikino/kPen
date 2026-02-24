#pragma once

#include <SDL2/SDL.h>
#include <functional>
#include <vector>
#include "DrawingUtils.h"
#include "Constants.h"

enum class ToolType { BRUSH, LINE, RECT, CIRCLE, SELECT, FILL, RESIZE };

class ICoordinateMapper {
  public:
    virtual void getCanvasCoords(int winX, int winY, int* canX, int* canY) = 0;
    virtual void getWindowCoords(int canX, int canY, int* winX, int* winY) = 0;
    virtual int  getWindowSize(int canSize) = 0;

    // Clamp (cx,cy) to the canvas edge along the ray from (sx,sy).
    static void clampToCanvasEdge(int sx, int sy, int& cx, int& cy) {
        if (cx >= 0 && cx < CANVAS_WIDTH && cy >= 0 && cy < CANVAS_HEIGHT) return;
        float dx = (float)(cx - sx), dy = (float)(cy - sy);
        float t = 1.f;
        if (dx != 0.f) { float v = (dx > 0 ? CANVAS_WIDTH  - 1 - sx : -sx) / dx; if (v > 0.f && v < t) t = v; }
        if (dy != 0.f) { float v = (dy > 0 ? CANVAS_HEIGHT - 1 - sy : -sy) / dy; if (v > 0.f && v < t) t = v; }
        cx = std::max(0, std::min(CANVAS_WIDTH  - 1, sx + (int)(dx * t)));
        cy = std::max(0, std::min(CANVAS_HEIGHT - 1, sy + (int)(dy * t)));
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
  protected:
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW };
    static const int GRAB = 6;

    SDL_Rect currentBounds = {0, 0, 0, 0};

    Handle resizing = Handle::NONE;
    bool   isMoving = false;
    bool   moved    = false;
    int    anchorX  = 0, anchorY  = 0;
    int    dragOffX = 0, dragOffY = 0;
    bool   flipX    = false;   // toggled each time a horizontal handle crosses anchor
    bool   flipY    = false;   // toggled each time a vertical handle crosses anchor

    Handle getHandle(int cX, int cY) const;
    void   drawHandles(SDL_Renderer* r) const;
    bool   handleMouseDown(int cX, int cY);
    bool   handleMouseMove(int cX, int cY);
    void   handleMouseUp();

  public:
    using AbstractTool::AbstractTool;
    bool isHit          (int cX, int cY) const;
    bool hasMoved       () const { return moved; }
    bool isMutating     () const { return isMoving || resizing != Handle::NONE; }
    SDL_Rect getFloatingBounds() const { return currentBounds; }
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
    bool isDirty()           const    { return dirty || hasMoved(); }
    bool isHit(int cX, int cY) const;
    void activateWithTexture(SDL_Texture* tex, SDL_Rect area);
    void setBounds(SDL_Rect area) { currentBounds = area; }
    std::vector<uint32_t> getFloatingPixels(SDL_Renderer* r) const;
};

// ── ResizeTool ────────────────────────────────────────────────────────────────

class ResizeTool : public TransformTool {
    ToolType  shapeType;
    SDL_Rect  origBounds;
    int       shapeStartX, shapeStartY, shapeEndX, shapeEndY;
    int       shapeBrushSize;
    SDL_Color shapeColor;

    void renderShape(SDL_Renderer* r, const SDL_Rect& bounds,
                     int bs, SDL_Color col, int clipW = 0, int clipH = 0) const;
  public:
    ResizeTool(ICoordinateMapper* m, ToolType shapeType, SDL_Rect bounds,
               int sx, int sy, int ex, int ey, int brushSize, SDL_Color color);
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
    std::vector<uint32_t> getFloatingPixels(SDL_Renderer* r) const;
};

// ── Other tools ───────────────────────────────────────────────────────────────

class BrushTool : public AbstractTool {
  public:
    using AbstractTool::AbstractTool;
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

class ShapeTool : public AbstractTool {
    ToolType type;
    using ShapeReadyCallback = std::function<void(ToolType, SDL_Rect, int, int, int, int, int, SDL_Color)>;
    ShapeReadyCallback onShapeReady;
  public:
    ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb);
    bool onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

class FillTool : public AbstractTool {
  public:
    using AbstractTool::AbstractTool;
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

#pragma once

#include <SDL2/SDL.h>
#include <functional>
#include <vector>
#include "DrawingUtils.h"

enum class ToolType { BRUSH, ERASER, LINE, RECT, CIRCLE, SELECT, FILL, PICK, RESIZE, HAND };

class ICoordinateMapper {
  public:
    virtual void getCanvasCoords(int winX, int winY, int* canX, int* canY) = 0;
    virtual void getWindowCoords(int canX, int canY, int* winX, int* winY) = 0;
    virtual int  getWindowSize(int canSize) = 0;
    virtual void getCanvasSize(int* w, int* h) = 0;
};

inline bool isPointOnCanvas(ICoordinateMapper* m, int cX, int cY) {
    int cw, ch;
    m->getCanvasSize(&cw, &ch);
    return cX >= 0 && cX < cw && cY >= 0 && cY < ch;
}

class AbstractTool {
  protected:
    ICoordinateMapper* mapper;
    bool isDrawing = false;
    int  startX = 0, startY = 0, lastX = 0, lastY = 0;
  public:
    AbstractTool(ICoordinateMapper* m) : mapper(m) {}
    virtual ~AbstractTool() {}
    bool isActive()  const { return isDrawing; }
    virtual void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual bool onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual void onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color);
    virtual void onOverlayRender(SDL_Renderer* r);
    virtual bool hasOverlayContent();
    virtual void deactivate(SDL_Renderer* r);
};

// --- TransformTool: shared handle/move logic for Select and Resize ---

class TransformTool : public AbstractTool {
  public:
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW, ROTATE };

  protected:
    static const int GRAB_WIN    = 4;
    static const int ROT_OFFSET  = 28;

    SDL_Rect currentBounds = {0, 0, 0, 0};
    float    rotation      = 0.f;

    Handle resizing    = Handle::NONE;
    bool   isRotating  = false;
    bool   isMoving    = false;
    bool   moved       = false;
    int    anchorX     = 0, anchorY  = 0;
    int    dragOffX    = 0, dragOffY = 0;
    float  dragAspect  = 1.f;
    bool   flipX       = false;
    bool   flipY       = false;
    float  rotPivotCX  = 0.f;
    float  rotPivotCY  = 0.f;
    float  rotStartAngle = 0.f;
    float  rotBaseAngle  = 0.f;
    float  rotLastAngle  = 0.f;
    float  anchorWorldX  = 0.f;
    float  anchorWorldY  = 0.f;
    float  drawCenterX   = 0.f;
    float  drawCenterY   = 0.f;

    void   syncDrawCenterFromBounds();
    void   getRotateHandleWin(int& wx, int& wy) const;
    Handle getHandle(int cX, int cY) const;
    void   drawHandles(SDL_Renderer* r) const;
    bool   handleMouseDown(int cX, int cY);
    bool   handleMouseMove(int cX, int cY, bool aspectLock = false);
    void   handleMouseUp();

    void rotatePt(float inX, float inY, float pivX, float pivY,
                  float angle, float& outX, float& outY) const;
    bool pointInRotatedBounds(int cX, int cY) const;

  public:
    using AbstractTool::AbstractTool;
    bool isHit              (int cX, int cY) const;
    bool hasMoved           () const { return moved; }
    bool isMutating         () const { return isMoving || isRotating || resizing != Handle::NONE; }
    SDL_Rect getFloatingBounds() const { return currentBounds; }
    float    getRotation()      const;
    float    getDrawCenterX()   const { return drawCenterX; }
    float    getDrawCenterY()   const { return drawCenterY; }
    bool     getFlipX()         const { return flipX; }
    bool     getFlipY()         const { return flipY; }
    Handle getHandleForCursor(int cX, int cY) const { return getHandle(cX, cY); }
    Handle getResizingHandle() const { return resizing; }

  protected:
    virtual void snapBounds(int& /*newX*/, int& /*newY*/, int& /*newW*/, int& /*newH*/) {}
};

// --- SelectTool ---

class SelectTool : public TransformTool {
    SDL_Texture* selectionTexture = nullptr;
    bool         active           = false;
    bool         dirty            = false;
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
    void fillWithColor(SDL_Renderer* r, SDL_Color color);
  private:
    void renderWithTransform(SDL_Renderer* r, const SDL_Rect& dst) const;
};

// --- ResizeTool ---

class ResizeTool : public TransformTool {
    ToolType         shapeType;
    SDL_Rect         origBounds;
    int              shapeStartX, shapeStartY, shapeEndX, shapeEndY;
    const SDL_Color* liveColor;

    void renderShape(SDL_Renderer* r, const SDL_Rect& bounds,
                     int bs, SDL_Color col, int clipW = 0, int clipH = 0) const;
    void renderShapeAt(SDL_Renderer* r, float x, float y, int w, int h, float rotationRad,
                       SDL_Color col, int clipW, int clipH) const;
  public:
    int*      liveBrushSize;
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
    bool willRender() const;
    std::vector<uint32_t> getFloatingPixels(SDL_Renderer* r) const;
  protected:
    void snapBounds(int& newX, int& newY, int& newW, int& newH) override;
};

// --- Other tools ---

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

// --- PickTool ---

class PickTool : public AbstractTool {
    using ColorPickedCallback = std::function<void(SDL_Color)>;
    ColorPickedCallback onColorPicked;
  public:
    PickTool(ICoordinateMapper* m, ColorPickedCallback cb)
        : AbstractTool(m), onColorPicked(std::move(cb)) {}
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

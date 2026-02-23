#pragma once

#include <SDL2/SDL.h>
#include <functional>
#include "DrawingUtils.h"
#include "Constants.h"

enum class ToolType { BRUSH, LINE, RECT, CIRCLE, SELECT, FILL, RESIZE };

class ICoordinateMapper {
  public:
    virtual void getCanvasCoords(int winX, int winY, int* canX, int* canY) = 0;
    virtual void getWindowCoords(int canX, int canY, int* winX, int* winY) = 0;
    virtual int getWindowSize(int canSize) = 0;
};

class AbstractTool {
  protected:
    ICoordinateMapper* mapper;
    bool isDrawing = false;
    int startX, startY, lastX, lastY;
  public:
    AbstractTool(ICoordinateMapper* m) : mapper(m) {};
    virtual ~AbstractTool() {}
    virtual void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color);
    virtual void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color);
    virtual bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color);
    virtual void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color);
    virtual void onOverlayRender(SDL_Renderer* overlayRenderer);
    virtual bool hasOverlayContent();
    virtual void deactivate(SDL_Renderer* canvasRenderer);
};

class BrushTool : public AbstractTool {
  public:
    using AbstractTool::AbstractTool;
    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override;
    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) override;
};

class ShapeTool : public AbstractTool {
  private:
    ToolType type;
    // Callback now receives shape params so kPen can create a ResizeTool
    using ShapeReadyCallback = std::function<void(ToolType, SDL_Rect, int, int, int, int, int, SDL_Color)>;
    ShapeReadyCallback onShapeReady;
  public:
    ShapeTool(ICoordinateMapper* m, ToolType t, ShapeReadyCallback cb);
    bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override;
    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) override;
};

class SelectTool : public AbstractTool {
  private:
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW };
    struct SelectionState {
        bool active = false;
        SDL_Rect area = {0, 0, 0, 0};
        SDL_Texture* selectionTexture = nullptr;
        bool hasMoved = false;
        bool isMoving = false;
        int dragOffsetX = 0, dragOffsetY = 0;
        Handle resizing = Handle::NONE;
        // Anchor edge positions kept fixed during resize
        int anchorX = 0, anchorY = 0;
    } state;

    static const int GRAB = 6; // canvas-pixel grab margin for handles
    Handle getHandle(int cX, int cY) const;
  public:
    using AbstractTool::AbstractTool;
    ~SelectTool();
    void onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override;
    bool onMouseUp(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) override;
    void onOverlayRender(SDL_Renderer* overlayRenderer) override;
    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) override;
    void deactivate(SDL_Renderer* canvasRenderer) override;
    bool isSelectionActive() const;
    bool isHit(int cX, int cY) const;
    bool hasMoved() const { return state.hasMoved; }
    bool isMutating() const { return state.isMoving || state.resizing != Handle::NONE; }
    void activateWithTexture(SDL_Texture* tex, SDL_Rect area);
    bool hasOverlayContent() override;
};

class FillTool : public AbstractTool {
  public:
    using AbstractTool::AbstractTool;
    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
};

class ResizeTool : public AbstractTool {
  private:
    enum class Handle { NONE, N, S, E, W, NE, NW, SE, SW };
    static const int GRAB = 6;

    ToolType   shapeType;
    SDL_Rect   origBounds;      // bounding box when shape was first drawn
    SDL_Rect   currentBounds;   // bounding box as user drags handles
    int        shapeStartX, shapeStartY; // original draw start point (canvas coords)
    int        shapeEndX,   shapeEndY;   // original draw end point
    int        shapeBrushSize;
    SDL_Color  shapeColor;

    Handle resizing  = Handle::NONE;
    bool   isMoving  = false;
    bool   moved     = false;
    int    anchorX   = 0, anchorY = 0;
    int    dragOffX  = 0, dragOffY = 0;

    Handle getHandle(int cX, int cY) const;
    void   renderShape(SDL_Renderer* r, const SDL_Rect& bounds,
                       int brushSz, SDL_Color col,
                       int clipW = 0, int clipH = 0) const;
  public:
    ResizeTool(ICoordinateMapper* m,
               ToolType           shapeType,
               SDL_Rect           bounds,
               int startX, int startY,
               int endX,   int endY,
               int brushSize,
               SDL_Color color);
    ~ResizeTool();

    void onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    bool onMouseUp  (int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) override;
    void onOverlayRender(SDL_Renderer* overlayRenderer) override;
    void onPreviewRender(SDL_Renderer* winRenderer, int brushSize, SDL_Color color) override;
    void deactivate (SDL_Renderer* canvasRenderer) override;
    bool hasOverlayContent() override;

    bool isHit(int cX, int cY) const;
    bool hasMoved() const { return moved; }
    bool isMutating() const { return isMoving || resizing != Handle::NONE; }
    SDL_Rect getBounds() const { return currentBounds; }
};

#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include "Tools.h"
#include "Toolbar.h"
#include "CanvasResizer.h"

class kPen : public ICoordinateMapper {
  public:
    kPen();
    ~kPen();

    void run();

    // Called by Toolbar when a tool button is clicked
    void setTool(ToolType t);

    // ICoordinateMapper interface
    void getCanvasCoords(int winX, int winY, int* cX, int* cY) override;
    void getWindowCoords(int canX, int canY, int* wX, int* wY) override;
    int  getWindowSize(int canSize) override;
    void getCanvasSize(int* w, int* h) override { *w = canvasW; *h = canvasH; }

    // Resize the canvas; scaleContent=true stretches pixels, false crops/pads with white.
    // originX/Y: how many canvas pixels the top-left corner shifted
    // (negative = canvas grew upward/leftward, padding added there).
    void resizeCanvas(int newW, int newH, bool scaleContent, int originX = 0, int originY = 0);

  private:
    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  canvas;
    SDL_Texture*  overlay;

    // Runtime canvas dimensions — start at compile-time defaults, change via resizeCanvas().
    int canvasW = 1200;
    int canvasH = 800;

    std::unique_ptr<AbstractTool> currentTool;
    ToolType currentType  = ToolType::BRUSH;
    ToolType originalType = ToolType::BRUSH;

    Toolbar       toolbar;
    CanvasResizer canvasResizer;

    // Drag-handle resize preview (ghost outline while dragging)
    int  previewW = 0, previewH = 0;
    int  previewOriginX = 0, previewOriginY = 0;
    bool showResizePreview = false;

    struct CanvasState {
        int w = 0, h = 0;
        std::vector<uint32_t> pixels;
    };
    std::vector<CanvasState> undoStack;
    std::vector<CanvasState> redoStack;

    // ── Zoom / pan ────────────────────────────────────────────────────────────
    static constexpr float MIN_ZOOM  = 0.1f;
    static constexpr float MAX_ZOOM  = 20.f;
    static constexpr float PAN_SLACK = 50.f;  // px the canvas edge may drift outside the window

    float zoom = 1.f;   // 1.0 = canvas fits the window
    float panX = 0.f;   // window-pixel offset from the fit-centered position
    float panY = 0.f;

    // Position-based scroll gesture state (mirrors toolbar scroll approach)
    bool  viewScrolling      = false;
    float viewScrollBaseX    = 0.f;   // panX at gesture start
    float viewScrollBaseY    = 0.f;   // panY at gesture start
    float viewScrollRawX     = 0.f;   // accumulated raw pan input since gesture start
    float viewScrollRawY     = 0.f;
    float viewScrollBaseZoom = 1.f;   // zoom at gesture start (ctrl+scroll / pinch)
    float viewScrollRawZoom  = 0.f;   // accumulated raw zoom input since gesture start

    // Two-finger pan gesture tracking
    bool  multiGestureActive = false;
    float lastGestureCX      = 0.f;
    float lastGestureCY      = 0.f;
    int   activeFingers      = 0;    // live count via FINGERDOWN/FINGERUP

    // Smooth zoom lerp target (only used when not actively scrolling)
    float zoomTarget = 1.f;

    // Per-gesture pinch state — reset each time fingers lift so accumulation is clean
    bool  pinchActive   = false;
    float pinchBaseZoom = 1.f;
    float pinchRawDist  = 0.f;

    SDL_Rect  getFitViewport();   // canvas fitted to window, centered — no zoom/pan
    SDL_Rect  getViewport();      // getFitViewport() + zoom + pan applied (integer, for hit-testing)
    SDL_FRect getViewportF();     // same but float, for smooth sub-pixel rendering

    void zoomAround(float newZoom, int pivotWinX, int pivotWinY);
    void onCanvasScroll(int winX, int winY, float dx, float dy, bool ctrl);
    bool tickView();             // call every frame; springs zoom/pan back if out of bounds

    // ── Undo / redo ───────────────────────────────────────────────────────────
    template<typename F> void withCanvas(F f);
    void saveState(std::vector<CanvasState>& stack);
    void applyState(CanvasState& s);
    void stampForRedo(AbstractTool* tool);
    void undo();
    void redo();
    void activateResizeTool(ToolType shapeType, SDL_Rect bounds,
                            int sx, int sy, int ex, int ey,
                            int brushSize, SDL_Color color);

    // Clipboard / selection helpers
    void copySelectionToClipboard();
    void pasteFromClipboard();
    void deleteSelection();   // delete without stamping back; saves undo state
};

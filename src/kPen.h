#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include <string>
#include "Tools.h"
#include "Toolbar.h"
#include "CanvasResizer.h"
#include "CursorManager.h"
#include "menu/MacMenu.h"

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
    CursorManager cursorManager;

    // Drag-handle resize preview (ghost outline while dragging)
    int  previewW = 0, previewH = 0;
    int  previewOriginX = 0, previewOriginY = 0;
    bool showResizePreview = false;
    bool shiftHeld = false;  // true while either shift key is physically down

    struct CanvasState {
        int w = 0, h = 0;
        std::vector<uint32_t> pixels;
        int serial = 0;  // unique ID assigned at push time
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

    void addPanDelta(float winDx, float winDy);  // apply pan delta and clamp (used by hand tool)

    void commitActiveTool();       // deactivate current tool and set to originalType
    void resetViewAndGestureState(); // zoom/pan to 1,0,0 and clear scroll/gesture flags

    // Hand pan: handActive = Space held or H toggled; does not deactivate other tools (e.g. selection stays).
    bool spaceHeld      = false;
    bool handToggledOn  = false;   // H toggles this; handActive = spaceHeld || handToggledOn
    bool handPanning    = false;
    int  handPanStartWinX = 0, handPanStartWinY = 0;
    float handPanStartPanX = 0.f, handPanStartPanY = 0.f;

    // Position-based scroll gesture state (mirrors toolbar scroll approach)
    bool  viewScrolling      = false;
    float viewScrollBaseX    = 0.f;   // panX at gesture start
    float viewScrollBaseY    = 0.f;   // panY at gesture start
    float viewScrollRawX     = 0.f;   // accumulated raw pan input since gesture start
    float viewScrollRawY     = 0.f;
    float viewScrollBaseZoom = 1.f;   // zoom at gesture start (ctrl+scroll / pinch)
    float viewScrollRawZoom  = 0.f;   // accumulated raw zoom input since gesture start
    float wheelAccumX = 0.f;          // mouse wheel velocity accumulation (applied + decayed each frame)
    float wheelAccumY = 0.f;

    // Two-finger pan gesture tracking
    bool  multiGestureActive   = false;
    float lastGestureCX        = 0.f;
    float lastGestureCY        = 0.f;
    int   activeFingers        = 0;    // live count via FINGERDOWN/FINGERUP
    bool  gestureNeedsRecenter = false; // skip pan delta on next MULTIGESTURE, just recapture centroid
    int   zoomPriorityEvents   = 0;    // after 1→2 fingers: skip pan for this many MULTIGESTUREs so pinch can arm

    // Second-finger tap detection — synthesizes a click when a quick tap lands
    // while one finger is already moving (macOS delays/drops the MOUSEBUTTONDOWN).
    SDL_FingerID tapFingerId   = 0;
    float        tapDownX      = 0.f; // window-pixel position at FINGERDOWN
    float        tapDownY      = 0.f;
    Uint32       tapDownTime   = 0;
    bool         tapPending    = false;
    bool         tapSawGesture = false; // MULTIGESTURE fired while this tap was pending (confirms two-finger scenario)
    bool         tapConsumed   = false; // suppress the delayed real MOUSEBUTTON events after a synthesized tap
    bool         threeFingerPanMode = false; // true when 3rd finger landed while 2-finger pan/zoom was active

    // Smooth zoom lerp target (only used when not actively scrolling)
    float zoomTarget = 1.f;

    // Per-gesture pinch state — reset each time fingers lift so accumulation is clean
    bool  pinchActive     = false;
    float pinchBaseZoom   = 1.f;
    float pinchRawDist    = 0.f;
    // Pivot when 2nd finger lands: midpoint of both fingers in window coords; used for zoom so pan→lift→zoom works.
    float twoFingerPivotX   = 0.f;
    float twoFingerPivotY   = 0.f;
    bool  twoFingerPivotSet = false;

    SDL_Rect  getFitViewport();   // canvas fitted to window, centered — no zoom/pan
    SDL_Rect  getViewport();      // getFitViewport() + zoom + pan applied (integer, for hit-testing)
    SDL_FRect getViewportF();     // same but float, for smooth sub-pixel rendering

    void zoomAround(float newZoom, int pivotWinX, int pivotWinY);
    void onCanvasScroll(int winX, int winY, float dx, float dy, bool ctrl);
    bool tickView();             // call every frame; springs zoom/pan back if out of bounds

    // Canvas scrollbars at window edge (right/bottom). Fills rects when scrollable; returns true if any.
    bool getScrollbarRects(int winW, int winH, SDL_Rect* trackV, SDL_Rect* thumbV, SDL_Rect* trackH, SDL_Rect* thumbH, bool* hasV, bool* hasH);
    bool scrollbarDragV = false;
    bool scrollbarDragH = false;
    int  scrollbarDragOffsetX = 0;  // mouse offset into thumb at drag start (for smooth drag)
    int  scrollbarDragOffsetY = 0;
    float scrollbarAlphaV = 0.f;     // vertical (right) bar fade
    float scrollbarAlphaH = 0.f;     // horizontal (bottom) bar fade
    bool scrollWheelWasVertical = true;  // last wheel scroll axis (for showing only relevant scrollbar)

    // ── Undo / redo ───────────────────────────────────────────────────────────
    template<typename F> void withCanvas(F f);
    void saveState(std::vector<CanvasState>& stack);
    void applyState(CanvasState& s);
    void stampForRedo(AbstractTool* tool);
    void undo();
    void redo();
    void activateResizeTool(ToolType shapeType, SDL_Rect bounds, SDL_Rect origBounds,
                            int sx, int sy, int ex, int ey,
                            int brushSize, SDL_Color color, bool filled = false);

    // Clipboard / selection helpers
    void copySelectionToClipboard();
    void pasteFromClipboard();
    void deleteSelection();   // delete without stamping back; saves undo state

    // File I/O
    std::string currentFilePath;
    int         nextStateSerial = 1;  // increments each time a new state is pushed
    int         savedStateId    = 0;  // serial of the CanvasState that matches the saved file
    bool hasUnsavedChanges() const {
        return undoStack.empty() || undoStack.back().serial != savedStateId;
    }
    void updateWindowTitle();
    bool promptSaveIfNeeded();  // returns false if user cancelled
    void doSave(bool forceSaveAs);
    void doOpen();

    // Single dispatcher for all menu-driven actions (MacMenu::Code values).
    // Called from SDL_USEREVENT (macOS native menu) and synthesised from
    // SDL_KEYDOWN on Windows/Linux.
    void dispatchCommand(int code, bool& running, bool& needsRedraw, bool& overlayDirty);

    // Event loop: one dispatcher and per-type handlers (run() calls processEvent for each polled event).
    void processEvent(SDL_Event& e, bool& running, bool& needsRedraw, bool& overlayDirty);
    void handleQuit(bool& running);
    void handleUserEvent(SDL_Event& e, bool& running, bool& needsRedraw, bool& overlayDirty);
    void handleTextInput(SDL_Event& e, bool& needsRedraw);
    void handleKeyDown(SDL_Event& e, bool& running, bool& needsRedraw, bool& overlayDirty);
    void handleKeyUp(SDL_Event& e, bool& needsRedraw);
    void handleWindowEvent(SDL_Event& e, bool& needsRedraw);
    void handleMouseWheel(SDL_Event& e, bool& needsRedraw, bool& overlayDirty);
    void handleFingerDown(SDL_Event& e);
    void handleFingerUp(SDL_Event& e, bool& needsRedraw, bool& overlayDirty);
    void handleFingerMotion(SDL_Event& e);
    void handleMultiGesture(SDL_Event& e, bool& needsRedraw);
    void handleMouseButtonDown(SDL_Event& e, bool& needsRedraw, bool& overlayDirty);
    void handleMouseButtonUp(SDL_Event& e, bool& needsRedraw, bool& overlayDirty);
    void handleMouseMotion(SDL_Event& e, bool& needsRedraw, bool& overlayDirty);

    // Per-frame updates (called from run() after polling events).
    void tickScrollbarFade(bool& needsRedraw);
    void updateCursor(bool& needsRedraw, bool& overlayDirty);

    // Full render pass: overlay, composite, scrollbars, toolbar, present.
    void renderFrame(bool& overlayDirty);
};

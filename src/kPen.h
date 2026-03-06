#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include <string>
#include "Tools.h"
#include "Toolbar.h"
#include "CanvasResizer.h"
#include "CursorManager.h"
#include "UndoManager.h"
#include "ViewController.h"
#include "menu/MacMenu.h"

class kPen : public ICoordinateMapper {
  public:
    kPen();
    ~kPen();

    void run();

    void setTool(ToolType t);
    void getCanvasCoords(int winX, int winY, int* cX, int* cY) override;
    void getWindowCoords(int canX, int canY, int* wX, int* wY) override;
    int  getWindowSize(int canSize) override;
    void getCanvasSize(int* w, int* h) override { *w = canvasW; *h = canvasH; }

    // Resize canvas; scaleContent=true stretches pixels, false crops/pads. originX/Y = top-left shift in canvas px (negative = grew up/left).
    // Returns false if new texture creation failed (canvas/overlay unchanged).
    bool resizeCanvas(int newW, int newH, bool scaleContent, int originX = 0, int originY = 0);

  private:
    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  canvas;
    SDL_Texture*  overlay;

    int canvasW = 1200;
    int canvasH = 800;
    int winW_   = 0;
    int winH_   = 0;

    std::unique_ptr<AbstractTool> currentTool;
    ToolType originalType = ToolType::BRUSH;  // tool to restore when leaving SELECT/RESIZE

    Toolbar       toolbar;
    CanvasResizer canvasResizer;
    CursorManager cursorManager;

    int  previewW = 0, previewH = 0;
    int  previewOriginX = 0, previewOriginY = 0;
    bool showResizePreview = false;
    bool shiftHeld = false;

    UndoManager undoManager;
    ViewController view_;

    void commitActiveTool();
    void resetViewAndGestureState();
    void resetGestureState();

    bool spaceHeld      = false;
    bool handToggledOn  = false;
    bool handPanning    = false;
    int  handPanStartWinX = 0, handPanStartWinY = 0;

    bool  multiGestureActive   = false;
    float lastGestureCX        = 0.f;
    float lastGestureCY        = 0.f;
    int   activeFingers        = 0;
    bool  gestureNeedsRecenter = false;
    int   zoomPriorityEvents   = 0;

    SDL_FingerID tapFingerId   = 0;
    float        tapDownX      = 0.f;
    float        tapDownY      = 0.f;
    Uint32       tapDownTime   = 0;
    bool         tapPending    = false;
    bool         tapSawGesture = false;
    bool         tapConsumed   = false;
    bool         threeFingerPanMode = false;

    int   lastMotionCX    = -1;
    int   lastMotionCY    = -1;
    int   lastPickCX      = -1;
    int   lastPickCY      = -1;
    SDL_Color lastPickHoverColor = { 0, 0, 0, 0 };

    bool  pinchActive     = false;
    float pinchBaseZoom   = 1.f;
    float pinchRawDist    = 0.f;
    float twoFingerPivotX   = 0.f;
    float twoFingerPivotY   = 0.f;
    bool  twoFingerPivotSet = false;

    Uint32 lastGestureTicks = 0;

    SDL_Rect  getViewport();
    SDL_FRect getViewportF();
    bool tickView();

    // --- Undo / redo ---
    template<typename F> void withCanvas(F f);
    void saveState();
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
    void deleteSelection();

    // --- File I/O ---
    std::string currentFilePath;
    int         savedStateId = 0;
    bool hasUnsavedChanges() const {
        return undoManager.getUndoSize() == 0 || undoManager.currentSerial() != savedStateId;
    }
    void updateWindowTitle();
    bool promptSaveIfNeeded();
    void doSave(bool forceSaveAs);
    void doOpen();

    // Menu/shortcut dispatch (MacMenu::Code); SDL_USEREVENT on macOS, SDL_KEYDOWN elsewhere.
    void dispatchCommand(int code, bool& running, bool& needsRedraw, bool& overlayDirty);
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

    void tickScrollbarFade(bool& needsRedraw);
    void updateCursor(bool& needsRedraw, bool& overlayDirty);
    void renderFrame(bool& overlayDirty);
};

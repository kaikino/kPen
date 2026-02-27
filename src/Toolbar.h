#pragma once
#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <cmath>
#include <algorithm>
#include "Tools.h"

// Forward declaration — Toolbar needs to call setTool() on kPen
class kPen;

class Toolbar {
  public:
    static constexpr int TB_W      = 84;
    static constexpr int TB_PAD    = 6;
    static constexpr int ICON_SIZE = 24;
    static constexpr int ICON_GAP  = 3;
    static constexpr int NUM_CUSTOM = 9;

    static constexpr int TRANSPARENT_PRESET_IDX = 0;
    static constexpr SDL_Color PRESETS[27] = {
        {0,0,0,0},        {0,0,0,255},       {255,255,255,255},
        {64,64,64,255},{128,128,128,255},{220,220,220,255},
        {101,55,0,255},   {160,100,40,255}, {210,170,110,255},
        {139,0,0,255},    {240,40,50,255},  {255,120,100,255},
        {230,100,0,255},  {255,165,60,255}, {255,230,0,255},
        {200,0,140,255},  {255,0,180,255},  {255,170,230,255},
        {55,0,130,255},   {128,0,200,255},  {210,150,255,255},
        {0,0,160,255},    {30,100,220,255}, {140,190,255,255},
        {0,100,0,255},    {34,160,34,255},  {140,220,140,255},
    };

    // Color state (owned here, read by kPen for drawing)
    float       hue = 0.f, sat = 0.f, val = 0.f;
    SDL_Color   brushColor  = {0, 0, 0, 255};
    int         brushSize   = 2;
    ToolType    currentType  = ToolType::BRUSH;
    bool        fillRect     = false;  // toggled by clicking RECT while already active
    bool        fillCircle   = false;  // toggled by clicking CIRCLE while already active
    bool        squareBrush  = false;  // toggled by clicking BRUSH while already active
    bool        squareEraser = false;  // toggled by clicking ERASER while already active

    SDL_Color   customColors[NUM_CUSTOM] = {
        {220,220,220,255},{180,180,180,255},{120,120,120,255},
        {255,100,100,255},{100,200,100,255},{100,150,255,255},
        {255,200,80,255}, {200,100,255,255},{80,220,200,255},
    };
    int selectedCustomSlot = -1;
    int selectedPresetSlot = -1;

    Toolbar(SDL_Renderer* renderer, kPen* app);

    // Draw the entire toolbar panel
    void draw();

    // Event handling — return true if the event was consumed by the toolbar
    bool onMouseDown(int x, int y);
    bool onMouseMotion(int x, int y);
    void onMouseUp(int x, int y);

    bool isDragging() const;
    bool onMouseWheel(int x, int y, float dy);
    void stopScrolling() { userScrolling = false; scrollRawOffset = 0.f; scrollBaseY = scrollY; }
    bool tickScroll();
    bool inToolbar(int x, int y) const { return x < TB_W; }

    // ── Canvas resize panel ───────────────────────────────────────────────────
    struct CanvasResizeRequest {
        bool pending = false;
        int  w = 0, h = 0;
        bool scale = false;
    };
    bool getResizeScaleMode() const { return resizeScaleMode; }
    bool getResizeLockAspect() const { return resizeLockAspect; }
    // Shift-key temporary override: when true, aspect is locked regardless of button state.
    // Returns to previous state when cleared.
    void setShiftLockAspect(bool on) { shiftLockAspect = on; }
    bool getEffectiveLockAspect() const { return resizeLockAspect || shiftLockAspect; }
    bool onTextInput(const char* text);
    bool onResizeKey(SDL_Keycode sym);
    CanvasResizeRequest getResizeRequest();
    void syncCanvasSize(int w, int h);
    void syncBrushSize();  // update brushSizeBuf to match current brushSize

    // Call this when a mouse-down lands outside the toolbar while a resize
    // field is focused — reverts the text fields to the actual canvas size.
    void notifyClickOutside();

    // HSV <-> RGB helpers (used externally by kPen for init)
    static SDL_Color hsvToRgb(float h, float s, float v);
    static void      rgbToHsv(SDL_Color c, float& h, float& s, float& v);

  private:
    SDL_Renderer* renderer;
    kPen*         app;      // for setTool() callbacks

    // Drag state
    bool draggingWheel      = false;
    bool draggingBrightness = false;
    bool draggingSlider     = false;
    bool draggingSwatch     = false;
    int  draggingSwatchIdx  = -1;
    int  scrollY            = 0;
    int   maxScrollCache    = 0;
    bool  userScrolling     = false;
    // Position-based scroll: track raw accumulated input and scrollY at gesture start
    float scrollRawOffset   = 0.f;  // accumulated raw wheel ticks (in pixels)
    int   scrollBaseY       = 0;    // scrollY when gesture started

    // Brush size text input
    bool brushSizeFocused   = false;
    char brushSizeBuf[3]    = {'2', 0, 0};  // up to 2 digits (1–20)
    int  brushSizeLen       = 1;
    mutable SDL_Rect brushSizeFieldRect = {0, 0, 0, 0};  // cached in screen space by draw()

    // Cached during draw for O(1) hit-testing
    int colorWheelCX = 0, colorWheelCY = 0, colorWheelR = 0;
    SDL_Rect brightnessRect = {0, 0, 0, 0};
    mutable int customGridY = 0;
    mutable int presetGridY = 0;

    // ── Resize panel private state ────────────────────────────────────────────
    // Text fields for width and height; no TTF — we use a baked pixel font.
    char   resizeWBuf[7] = {'1','2','0','0',0,0,0};  // null-terminated digit string (up to 6 digits, clamped to 16384)
    char   resizeHBuf[7] = {'8','0','0',0,0,0,0};
    int    resizeWLen    = 4;
    int    resizeHLen    = 3;
    enum class ResizeFocus { NONE, W, H } resizeFocus = ResizeFocus::NONE;
    bool   resizeScaleMode = false;
    bool   resizeLockAspect = false;
    bool   shiftLockAspect = false;  // true while shift is held during canvas drag
    int    resizeLockW = 1200;  // canvas W at time of last syncCanvasSize
    int    resizeLockH = 800;   // canvas H at time of last syncCanvasSize
    CanvasResizeRequest pendingResize;
    mutable int resizePanelY = 0;  // cached top of panel in scrolled coords (set by draw)
    void drawResizePanel(int panelY);
    bool hitResizePanel(int x, int sy, bool isDown);  // sy = scroll-adjusted y
    void drawDigitString(int x, int y, const char* s, int len) const;
    void applyAspectLock(bool srcIsW);  // enforce aspect ratio if resizeLockAspect is set
    void clampResizeInput(bool srcIsW); // cap entered value (and linked dim if locked) to 16384
    void defocusResize(bool commit);    // commit or revert resize fields and clear focus
    void commitResize();

    // Layout helpers
    int toolStartY()     const { return TB_PAD; }
    int sliderSectionY() const { return toolStartY() + 3*(ICON_SIZE+ICON_GAP) + 2 + 20 + 4; }  // row2: after row1(20px) + gap(4px)
    int sliderSectionH() const { return 14; }
    int swatchCellSize()   const { return (TB_W - TB_PAD*2 - 4) / 3; }
    int swatchCellStride() const { return swatchCellSize() + 2; }

    // O(1) hit-test helpers
    int hitCustomSwatch(int x, int y) const;
    int hitPresetSwatch(int x, int y) const;

    // Draw helpers
    void drawIcon(int cx, int cy, ToolType t, bool active);

    // Mouse update helpers
    void updateSliderFromMouse(int x);
    void updateWheelFromMouse(int x, int y);
    void updateBrightnessFromMouse(int x);
};

#pragma once
#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "Tools.h"

class kPen;

class Toolbar {
  public:
    static constexpr int TB_W       = 84;
    static constexpr int TB_PAD     = 6;
    static constexpr int ICON_SIZE  = 24;
    static constexpr int ICON_GAP   = 3;
    static constexpr int NUM_CUSTOM = 9;
    static constexpr int BS_FIELD_W = 26;
    static constexpr int BS_GAP     = 4;
    static constexpr int BS_ROW1_H  = 20;
    static constexpr int BS_ROW2_H   = 14;
    static constexpr int BS_ROW_GAP  = 4;

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

    float       hue = 0.f, sat = 0.f, val = 0.f;
    SDL_Color   brushColor  = {0, 0, 0, 255};
    int         brushSize   = 8;
    ToolType    currentType  = ToolType::BRUSH;
    bool        fillRect     = false;
    bool        fillCircle   = false;
    bool        squareBrush  = false;
    bool        squareEraser = false;
    bool        lassoSelect  = false;

    SDL_Color   customColors[NUM_CUSTOM] = {
        {220,220,220,255},{180,180,180,255},{120,120,120,255},
        {255,100,100,255},{100,200,100,255},{100,150,255,255},
        {255,200,80,255}, {200,100,255,255},{80,220,200,255},
    };
    int selectedCustomSlot = -1;
    int selectedPresetSlot = -1;

    std::function<void(SDL_Color)> onColorChanged;
    /** Called when the user drops a swatch onto the canvas (fill canvas with that color). */
    std::function<void(SDL_Color)> onColorDroppedOnCanvas;

    Toolbar(SDL_Renderer* renderer, kPen* app);
    void draw(bool handActive, int winW, int winH);

    bool onMouseDown(int x, int y);
    bool onMouseMotion(int x, int y);
    void onMouseUp(int x, int y);

    bool isDragging() const;
    /** When dragging a swatch, returns true and sets *out to the dragged color. */
    bool getDraggingSwatchColor(SDL_Color* out) const;
    bool onMouseWheel(int x, int y, float dy);
    bool tickScroll();
    bool inToolbar(int x, int y) const { return x < TB_W; }
    /** Draw mouse coords at bottom-right of window (winW, winH). */
    void drawCoordDisplay(int winW, int winH, int mx, int my) const;
    bool isInteractive(int x, int y) const;

    // --- Canvas resize panel ---
    struct CanvasResizeRequest {
        bool pending = false;
        int  w = 0, h = 0;
        bool scale = false;
    };
    bool getResizeScaleMode() const { return resizeScaleMode; }
    bool getResizeLockAspect() const { return resizeLockAspect; }
    void setShiftLockAspect(bool on) { shiftLockAspect = on; }
    bool getEffectiveLockAspect() const { return resizeLockAspect || shiftLockAspect; }
    bool onTextInput(const char* text);
    bool onResizeKey(SDL_Keycode sym);
    bool onArrowKey(int dx, int dy);
    CanvasResizeRequest getResizeRequest();
    void syncCanvasSize(int w, int h);
    void syncBrushSize();
    void notifyClickOutside();
    static SDL_Color hsvToRgb(float h, float s, float v);
    static void      rgbToHsv(SDL_Color c, float& h, float& s, float& v);

  private:
    SDL_Renderer* renderer;
    kPen*         app;
    bool draggingWheel      = false;
    bool draggingBrightness = false;
    bool draggingSlider     = false;
    bool draggingSwatch     = false;
    int  draggingSwatchIdx  = -1;
    int  scrollY            = 0;
    int   maxScrollCache    = 0;
    bool  userScrolling     = false;
    float scrollRawOffset   = 0.f;
    int   scrollBaseY       = 0;

    bool brushSizeFocused   = false;
    char brushSizeBuf[3]    = {'8', 0, 0};
    mutable SDL_Rect brushSizeFieldRect = {0, 0, 0, 0};
    int brushSizeBufLen() const { return (int)std::strlen(brushSizeBuf); }

    int colorWheelCX = 0, colorWheelCY = 0, colorWheelR = 0;
    SDL_Rect brightnessRect = {0, 0, 0, 0};
    mutable int customGridY = 0;
    mutable int presetGridY = 0;

    char   resizeWBuf[7] = {'1','2','0','0',0,0,0};
    char   resizeHBuf[7] = {'8','0','0',0,0,0,0};
    int resizeWBufLen() const { return (int)std::strlen(resizeWBuf); }
    int resizeHBufLen() const { return (int)std::strlen(resizeHBuf); }
    enum class ResizeFocus { NONE, W, H } resizeFocus = ResizeFocus::NONE;
    bool   resizeScaleMode = false;
    bool   resizeLockAspect = false;
    bool   shiftLockAspect = false;
    int    resizeLockW = 1200;
    int    resizeLockH = 800;
    CanvasResizeRequest pendingResize;
    mutable int resizePanelY = 0;
    void drawResizePanel(int panelY);
    bool hitResizePanel(int x, int sy, bool isDown);
    void drawDigitString(int x, int y, const char* s, int len) const;
    void applyAspectLock(bool srcIsW);
    void applyAspectFromFocusedField();
    void clampResizeInput(bool srcIsW);
    void defocusResize(bool commit);
    void commitResize();

    int toolStartY()      const { return TB_PAD; }
    static constexpr int toolCellW()    { return (TB_W - TB_PAD) / 3; }
    static constexpr int contentWidth() { return TB_W - TB_PAD*2; }
    int sliderSectionY()  const { return toolStartY() + 3*(ICON_SIZE+ICON_GAP) + 2 + BS_ROW1_H + BS_ROW_GAP; }
    int sliderSectionH()  const { return BS_ROW2_H; }
    int swatchCellSize()  const { return (contentWidth() - 4) / 3; }
    int swatchCellStride() const { return swatchCellSize() + 2; }

    int hitCustomSwatch(int x, int y) const;
    int hitPresetSwatch(int x, int y) const;
    void drawIcon(int cx, int cy, ToolType t, bool active);
    void updateSliderFromMouse(int x);
    void updateWheelFromMouse(int x, int y);
    void updateBrightnessFromMouse(int x);
};

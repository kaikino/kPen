#pragma once
#define _USE_MATH_DEFINES
#include <SDL2/SDL.h>
#include <cmath>
#include <algorithm>
#include "Constants.h"
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

    static constexpr SDL_Color PRESETS[27] = {
        {255,255,255,255},{0,0,0,255},      {64,64,64,255},
        {128,128,128,255},{180,180,180,255},{220,220,220,255},
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
    ToolType    currentType = ToolType::BRUSH;

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
    bool inToolbar(int x, int y) const { return x < TB_W; }

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

    // Cached during draw for O(1) hit-testing
    int colorWheelCX = 0, colorWheelCY = 0, colorWheelR = 0;
    SDL_Rect brightnessRect = {0, 0, 0, 0};
    mutable int customGridY = 0;
    mutable int presetGridY = 0;

    // Layout helpers
    int toolStartY()     const { return TB_PAD; }
    int sliderSectionY() const { return toolStartY() + 3*(ICON_SIZE+ICON_GAP) + 2 + 20 + 2; }
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

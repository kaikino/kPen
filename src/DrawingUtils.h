#pragma once

#include <SDL2/SDL.h>

#include "Constants.h"

// Shared Drawing Helpers
namespace DrawingUtils {
    void drawFillCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius);
    void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int size, int w = CANVAS_WIDTH, int h = CANVAS_HEIGHT);
    void drawRect(SDL_Renderer* renderer, const SDL_Rect* rect, int size, int w = CANVAS_WIDTH, int h = CANVAS_HEIGHT);
    void drawOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int size, int w = CANVAS_WIDTH, int h = CANVAS_HEIGHT);

    // Alternating black/white dashes around the full perimeter (marching ants style).
    // Always visible regardless of what color is underneath.
    void drawMarchingRect(SDL_Renderer* renderer, const SDL_Rect* rect);
}

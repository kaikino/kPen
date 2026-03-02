#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <cstdint>

namespace DrawingUtils {
    void drawFillCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius);
    void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int size, int w, int h);
    void drawSquareStamp(SDL_Renderer* r, int cx, int cy, int brushSize, int cw, int ch, SDL_Color color);
    void drawSquareLine (SDL_Renderer* r, int x0, int y0, int x1, int y1, int brushSize, int cw, int ch, SDL_Color color);
    void drawRect    (SDL_Renderer* renderer, const SDL_Rect* rect, int size, int w, int h);
    void drawFilledRect(SDL_Renderer* renderer, const SDL_Rect* rect, int w, int h);
    void drawOval      (SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int size, int w, int h);
    void drawFilledOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int w, int h);
    SDL_Rect getOvalCenterBounds(int x0, int y0, int x1, int y1);
    void drawMarchingRect(SDL_Renderer* renderer, const SDL_Rect* rect);
    void drawMarchingPolyline(SDL_Renderer* renderer, const SDL_Point* points, int count, bool closed, bool whiteOnly = false);

    std::vector<uint8_t> encodeJPEG(const uint32_t* argbPixels, int w, int h, int quality = 92);
    std::vector<uint8_t> encodePNG (const uint32_t* argbPixels, int w, int h);
    std::vector<uint32_t> decodeImage(const uint8_t* data, int dataLen, int& outW, int& outH);
    bool setClipboardImage(const uint32_t* argbPixels, int w, int h);
    bool getClipboardImage(std::vector<uint32_t>& outPixels, int& outW, int& outH);
}

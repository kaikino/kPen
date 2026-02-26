#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <cstdint>

// Shared Drawing Helpers
namespace DrawingUtils {
    void drawFillCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius);
    void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int size, int w, int h);
    void drawRect(SDL_Renderer* renderer, const SDL_Rect* rect, int size, int w, int h);
    void drawOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int size, int w, int h);
    // Returns the tight bounding box of brush center pixels that drawOval will plot
    // given the same cx0/cy0/cx1/cy1 arguments. Add ±brushSize/2 to get pixel extent.
    SDL_Rect getOvalCenterBounds(int x0, int y0, int x1, int y1);

    // Alternating black/white dashes around the full perimeter (marching ants style).
    // Always visible regardless of what color is underneath.
    void drawMarchingRect(SDL_Renderer* renderer, const SDL_Rect* rect);

    // ── Image clipboard ───────────────────────────────────────────────────────
    //
    // encodeJPEG / encodePNG: compress ARGB8888 pixels to JPEG/PNG bytes.
    // decodeImage:            decompress any stb_image-supported format to ARGB8888.
    //
    // setClipboardImage: write ARGB8888 pixels to the OS image clipboard
    //                    (PNG on macOS, DIB on Windows, no-op elsewhere).
    // getClipboardImage: read ARGB8888 pixels from the OS image clipboard;
    //                    returns false if no image data is available.

    std::vector<uint8_t> encodeJPEG(const uint32_t* argbPixels, int w, int h, int quality = 92);
    std::vector<uint8_t> encodePNG (const uint32_t* argbPixels, int w, int h);

    // Decode any image format (PNG, JPEG, BMP, …) from raw bytes → ARGB8888.
    // outW/outH are set on success; returns empty vector on failure.
    std::vector<uint32_t> decodeImage(const uint8_t* data, int dataLen, int& outW, int& outH);

    bool setClipboardImage(const uint32_t* argbPixels, int w, int h);
    bool getClipboardImage(std::vector<uint32_t>& outPixels, int& outW, int& outH);
}

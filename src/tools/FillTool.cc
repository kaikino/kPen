#include "Tools.h"
#include <queue>
#include <vector>

void FillTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    if (cX < 0 || cX >= CANVAS_WIDTH || cY < 0 || cY >= CANVAS_HEIGHT) return;
    // Read all canvas pixels into a buffer
    std::vector<uint32_t> pixels(CANVAS_WIDTH * CANVAS_HEIGHT);
    SDL_RenderReadPixels(canvasRenderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                         pixels.data(), CANVAS_WIDTH * 4);

    // Target color = whatever is at the clicked pixel
    uint32_t target = pixels[cY * CANVAS_WIDTH + cX];
    uint32_t fill   = ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16)
                    | ((uint32_t)color.g <<  8) |  (uint32_t)color.b;

    if (target == fill) return; // already that color, nothing to do

    // BFS flood fill
    std::queue<int> q;
    q.push(cY * CANVAS_WIDTH + cX);
    pixels[cY * CANVAS_WIDTH + cX] = fill;

    while (!q.empty()) {
        int idx = q.front(); q.pop();
        int x = idx % CANVAS_WIDTH;
        int y = idx / CANVAS_WIDTH;

        // Check 4 neighbors
        auto tryPush = [&](int nx, int ny) {
            if (nx < 0 || nx >= CANVAS_WIDTH || ny < 0 || ny >= CANVAS_HEIGHT) return;
            int ni = ny * CANVAS_WIDTH + nx;
            if (pixels[ni] == target) {
                pixels[ni] = fill;
                q.push(ni);
            }
        };
        tryPush(x-1, y);
        tryPush(x+1, y);
        tryPush(x,   y-1);
        tryPush(x,   y+1);
    }

    // Write the modified pixels back to the canvas texture
    SDL_UpdateTexture(SDL_GetRenderTarget(canvasRenderer), nullptr,
                      pixels.data(), CANVAS_WIDTH * 4);
}

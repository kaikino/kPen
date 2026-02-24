#include "Tools.h"
#include <queue>
#include <vector>

void FillTool::onMouseDown(int cX, int cY, SDL_Renderer* canvasRenderer, int brushSize, SDL_Color color) {
    int canvasW, canvasH; mapper->getCanvasSize(&canvasW, &canvasH);
    if (cX < 0 || cX >= canvasW || cY < 0 || cY >= canvasH) return;
    std::vector<uint32_t> pixels(canvasW * canvasH);
    SDL_RenderReadPixels(canvasRenderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                         pixels.data(), canvasW * 4);

    uint32_t target = pixels[cY * canvasW + cX];
    uint32_t fill   = ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16)
                    | ((uint32_t)color.g <<  8) |  (uint32_t)color.b;

    if (target == fill) return; // already that color, nothing to do

    // BFS flood fill
    std::queue<int> q;
    q.push(cY * canvasW + cX);
    pixels[cY * canvasW + cX] = fill;

    while (!q.empty()) {
        int idx = q.front(); q.pop();
        int x = idx % canvasW;
        int y = idx / canvasW;

        auto tryPush = [&](int nx, int ny) {
            if (nx < 0 || nx >= canvasW || ny < 0 || ny >= canvasH) return;
            int ni = ny * canvasW + nx;
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
                      pixels.data(), canvasW * 4);
}

#include <SDL2/SDL.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>

enum class Tool { BRUSH, LINE, RECT, CIRCLE };

// canvas resolution
const int CANVAS_WIDTH = 1000;
const int CANVAS_HEIGHT = 700;

class kPen {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvas;
    
    Tool currentTool = Tool::BRUSH;
    bool isDrawing = false;
    int startX, startY, lastX, lastY;
    int brushSize = 2; // Shared brush size for all tools
    
    std::vector<std::vector<uint32_t>> undoStack;
    std::vector<std::vector<uint32_t>> redoStack;

    void saveState(std::vector<std::vector<uint32_t>>& stack) {
        std::vector<uint32_t> pixels(CANVAS_WIDTH * CANVAS_HEIGHT);
        SDL_SetRenderTarget(renderer, canvas);
        SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels.data(), CANVAS_WIDTH * 4);
        stack.push_back(pixels);
        SDL_SetRenderTarget(renderer, NULL);
    }

    void applyState(std::vector<uint32_t>& pixels) {
        SDL_UpdateTexture(canvas, NULL, pixels.data(), CANVAS_WIDTH * 4);
    }

    void undo() {
        if (undoStack.size() > 1) {
            saveState(redoStack);
            undoStack.pop_back();
            applyState(undoStack.back());
        }
    }

    void redo() {
        if (!redoStack.empty()) {
            applyState(redoStack.back());
            undoStack.push_back(redoStack.back());
            redoStack.pop_back();
        }
    }

    void getCanvasCoords(int winX, int winY, int* canvasX, int* canvasY) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        *canvasX = (int)(winX * ((float)CANVAS_WIDTH / w));
        *canvasY = (int)(winY * ((float)CANVAS_HEIGHT / h));
    }

    void drawFillCircle(int centerX, int centerY, int radius) {
        if (radius < 1) {
            SDL_RenderDrawPoint(renderer, centerX, centerY);
            return;
        }
        for (int w = 0; w < radius * 2; w++) {
            for (int h = 0; h < radius * 2; h++) {
                int dx = radius - w;
                int dy = radius - h;
                if ((dx * dx + dy * dy) <= (radius * radius)) {
                    SDL_RenderDrawPoint(renderer, centerX + dx, centerY + dy);
                }
            }
        }
    }

    // Standard line drawing with brush stamping for solid strokes
    void drawLine(int x1, int y1, int x2, int y2, int size) {
        if (size <= 1) {
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
            return;
        }

        int radius = size / 2;
        float dx = (float)(x2 - x1);
        float dy = (float)(y2 - y1);
        float distance = sqrt(dx * dx + dy * dy);
        
        if (distance == 0) {
            drawFillCircle(x1, y1, radius);
            return;
        }

        float steps = distance; 
        float xStep = dx / steps;
        float yStep = dy / steps;

        float currX = (float)x1;
        float currY = (float)y1;

        for (int i = 0; i <= (int)steps; ++i) {
            drawFillCircle((int)currX, (int)currY, radius);
            currX += xStep;
            currY += yStep;
        }
    }

    void drawRect(const SDL_Rect* rect, int size) {
        drawLine(rect->x, rect->y, rect->x + rect->w, rect->y, size); 
        drawLine(rect->x + rect->w, rect->y, rect->x + rect->w, rect->y + rect->h, size); 
        drawLine(rect->x + rect->w, rect->y + rect->h, rect->x, rect->y + rect->h, size); 
        drawLine(rect->x, rect->y + rect->h, rect->x, rect->y, size); 
    }

    void drawCircle(int x0, int y0, int x1, int y1, int size) {
        int r = (int)sqrt(pow(x1 - x0, 2) + pow(y1 - y0, 2));
        int radius = size / 2;
        
        float circumference = 2 * M_PI * r;
        int steps = (int)std::max(circumference, 1.0f);

        for (int i = 0; i < steps; i++) {
            float angle = 2.0f * M_PI * i / steps;
            int x = x0 + (int)(r * cos(angle));
            int y = y0 + (int)(r * sin(angle));
            drawFillCircle(x, y, radius);
        }
    }

public:
    kPen() {
        SDL_Init(SDL_INIT_VIDEO);
        window = SDL_CreateWindow("kPen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                                  CANVAS_WIDTH, CANVAS_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
        canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, CANVAS_WIDTH, CANVAS_HEIGHT);
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderTarget(renderer, NULL);
        saveState(undoStack);
    }

    ~kPen() {
        SDL_DestroyTexture(canvas);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void run() {
        bool running = true;
        SDL_Event event;
        while (running) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_KEYDOWN) {
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_b: currentTool = Tool::BRUSH; break;
                        case SDLK_l: currentTool = Tool::LINE; break;
                        case SDLK_r: currentTool = Tool::RECT; break;
                        case SDLK_o: currentTool = Tool::CIRCLE; break;
                        case SDLK_UP: brushSize = std::min(20, brushSize + 1); break;
                        case SDLK_DOWN: brushSize = std::max(1, brushSize - 1); break;
                        case SDLK_z: if (event.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) undo(); break;
                        case SDLK_y: if (event.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) redo(); break;
                    }
                }
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        isDrawing = true;
                        getCanvasCoords(event.button.x, event.button.y, &startX, &startY);
                        lastX = startX; lastY = startY;
                        redoStack.clear();
                    }
                }
                if (event.type == SDL_MOUSEBUTTONUP) {
                    if (isDrawing) {
                        isDrawing = false;
                        int endX, endY;
                        getCanvasCoords(event.button.x, event.button.y, &endX, &endY);
                        SDL_SetRenderTarget(renderer, canvas);
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        if (currentTool == Tool::LINE) drawLine(startX, startY, endX, endY, brushSize);
                        else if (currentTool == Tool::RECT) {
                            SDL_Rect r = { std::min(startX, endX), std::min(startY, endY), std::abs(endX - startX), std::abs(endY - startY) };
                            drawRect(&r, brushSize);
                        }
                        else if (currentTool == Tool::CIRCLE) drawCircle(startX, startY, endX, endY, brushSize);
                        SDL_SetRenderTarget(renderer, NULL);
                        saveState(undoStack);
                    }
                }
                if (event.type == SDL_MOUSEMOTION && isDrawing) {
                    if (currentTool == Tool::BRUSH) {
                        int currX, currY;
                        getCanvasCoords(event.motion.x, event.motion.y, &currX, &currY);
                        SDL_SetRenderTarget(renderer, canvas);
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        drawLine(lastX, lastY, currX, currY, brushSize);
                        SDL_SetRenderTarget(renderer, NULL);
                        lastX = currX; lastY = currY;
                    }
                }
            }
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255); 
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, canvas, NULL, NULL);
            if (isDrawing && (currentTool == Tool::LINE || currentTool == Tool::RECT || currentTool == Tool::CIRCLE)) {
                int winMouseX, winMouseY, curX, curY;
                SDL_GetMouseState(&winMouseX, &winMouseY);
                getCanvasCoords(winMouseX, winMouseY, &curX, &curY);
                SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255); 
                int w, h;
                SDL_GetWindowSize(window, &w, &h);
                int winStartX = (int)(startX * ((float)w / CANVAS_WIDTH));
                int winStartY = (int)(startY * ((float)h / CANVAS_HEIGHT));
                if (currentTool == Tool::LINE) drawLine(winStartX, winStartY, winMouseX, winMouseY, brushSize);
                else if (currentTool == Tool::RECT) {
                    SDL_Rect r = { std::min(winStartX, winMouseX), std::min(winStartY, winMouseY), std::abs(winMouseX - winStartX), std::abs(winMouseY - winStartY) };
                    drawRect(&r, brushSize);
                }
                else if (currentTool == Tool::CIRCLE) drawCircle(winStartX, winStartY, winMouseX, winMouseY, brushSize);
            }
            SDL_RenderPresent(renderer);
        }
    }
};

int main(int argc, char* argv[]) {
    kPen app;
    app.run();
    return 0;
}

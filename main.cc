#include <SDL2/SDL.h>
#include <vector>
#include <algorithm>
#include <iostream>

enum class Tool { BRUSH, LINE, RECT };

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

    // Helper to map window coordinates back to the canvas coordinates
    void getCanvasCoords(int winX, int winY, int* canvasX, int* canvasY) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        *canvasX = (int)(winX * ((float)CANVAS_WIDTH / w));
        *canvasY = (int)(winY * ((float)CANVAS_HEIGHT / h));
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
                        case SDLK_z: 
                            if (event.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) undo(); 
                            break;
                        case SDLK_y: 
                            if (event.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) redo(); 
                            break;
                    }
                }

                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        isDrawing = true;
                        getCanvasCoords(event.button.x, event.button.y, &startX, &startY);
                        lastX = startX;
                        lastY = startY;
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
                        
                        if (currentTool == Tool::LINE) {
                            SDL_RenderDrawLine(renderer, startX, startY, endX, endY);
                        } else if (currentTool == Tool::RECT) {
                            SDL_Rect r = { std::min(startX, endX), std::min(startY, endY), 
                                           std::abs(endX - startX), std::abs(endY - startY) };
                            SDL_RenderDrawRect(renderer, &r);
                        }
                        
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
                        SDL_RenderDrawLine(renderer, lastX, lastY, currX, currY);
                        SDL_SetRenderTarget(renderer, NULL);
                        lastX = currX;
                        lastY = currY;
                    }
                }
            }

            // Rendering
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255); 
            SDL_RenderClear(renderer);

            // Draw canvas scaled to window size
            SDL_RenderCopy(renderer, canvas, NULL, NULL);

            if (isDrawing && (currentTool == Tool::LINE || currentTool == Tool::RECT)) {
                int winMouseX, winMouseY, curX, curY;
                SDL_GetMouseState(&winMouseX, &winMouseY);
                getCanvasCoords(winMouseX, winMouseY, &curX, &curY);
                
                SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255); 
                
                int w, h;
                SDL_GetWindowSize(window, &w, &h);
                int winStartX = (int)(startX * ((float)w / CANVAS_WIDTH));
                int winStartY = (int)(startY * ((float)h / CANVAS_HEIGHT));

                if (currentTool == Tool::LINE) {
                    SDL_RenderDrawLine(renderer, winStartX, winStartY, winMouseX, winMouseY);
                } else {
                    SDL_Rect r = { std::min(winStartX, winMouseX), std::min(winStartY, winMouseY), 
                                   std::abs(winMouseX - winStartX), std::abs(winMouseY - winStartY) };
                    SDL_RenderDrawRect(renderer, &r);
                }
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

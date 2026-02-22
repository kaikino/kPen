#include <SDL2/SDL.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>

enum class Tool { BRUSH, LINE, RECT, CIRCLE, SELECT };

// canvas resolution
const int CANVAS_WIDTH = 1000;
const int CANVAS_HEIGHT = 700;

struct Selection {
    bool active = false;
    SDL_Rect area = {0, 0, 0, 0};
    std::vector<uint32_t> pixels; 
    bool isDragging = false;
    int offsetX = 0;
    int offsetY = 0;
};

class kPen {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvas;
    
    Tool currentTool = Tool::BRUSH;
    Tool previousTool = Tool::BRUSH;
    bool isDrawing = false;
    int startX, startY, lastX, lastY;
    int brushSize = 2; // Shared brush size for all tools
    
    Selection currentSelection;
    
    std::vector<std::vector<uint32_t>> undoStack;
    std::vector<std::vector<uint32_t>> redoStack;

    // RENDERER

    SDL_Rect getViewport() {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        float canvasAspect = (float)CANVAS_WIDTH / CANVAS_HEIGHT;
        float windowAspect = (float)winW / winH;
        
        SDL_Rect viewport;
        if (windowAspect > canvasAspect) {
            viewport.h = winH;
            viewport.w = (int)(winH * canvasAspect);
            viewport.x = (winW - viewport.w) / 2;
            viewport.y = 0;
        } else {
            viewport.w = winW;
            viewport.h = (int)(winW / canvasAspect);
            viewport.x = 0;
            viewport.y = (winH - viewport.h) / 2;
        }
        return viewport;
    }

    void getCanvasCoords(int winX, int winY, int* canvasX, int* canvasY) {
        SDL_Rect view = getViewport();
        *canvasX = (int)std::floor((winX - view.x) * ((float)CANVAS_WIDTH / view.w));
        *canvasY = (int)std::floor((winY - view.y) * ((float)CANVAS_HEIGHT / view.h));
    }

    void getWindowCoords(int canX, int canY, int* winX, int* winY) {
        SDL_Rect view = getViewport();
        // Use floor to match getCanvasCoords logic for pixel-perfect alignment
        *winX = view.x + (int)std::floor(canX * ((float)view.w / CANVAS_WIDTH));
        *winY = view.y + (int)std::floor(canY * ((float)view.h / CANVAS_HEIGHT));
    }

    int getWindowSize(int canSize) {
        SDL_Rect view = getViewport();
        return (int)std::round(canSize * ((float)view.w / CANVAS_WIDTH));
    }

    void drawDottedRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
        int dashLen = 4;
        // Top and Bottom
        for (int i = 0; i < rect->w; i += dashLen * 2) {
            SDL_RenderDrawLine(renderer, rect->x + i, rect->y, rect->x + std::min(i + dashLen, rect->w), rect->y);
            SDL_RenderDrawLine(renderer, rect->x + i, rect->y + rect->h, rect->x + std::min(i + dashLen, rect->w), rect->y + rect->h);
        }
        // Left and Right
        for (int i = 0; i < rect->h; i += dashLen * 2) {
            SDL_RenderDrawLine(renderer, rect->x, rect->y + i, rect->x, rect->y + std::min(i + dashLen, rect->h));
            SDL_RenderDrawLine(renderer, rect->x + rect->w, rect->y + i, rect->x + rect->w, rect->y + std::min(i + dashLen, rect->h));
        }
    }

    // DRAWER

    void drawFillCircle(int centerX, int centerY, int radius) {
        if (radius <= 0) {
            SDL_RenderDrawPoint(renderer, centerX, centerY);
            return;
        }
        for (int w = -radius; w <= radius; w++) {
            for (int h = -radius; h <= radius; h++) {
                if ((w * w + h * h) <= (radius * radius)) {
                    SDL_RenderDrawPoint(renderer, centerX + w, centerY + h);
                }
            }
        }
    }

    void drawLine(int x1, int y1, int x2, int y2, int size) {
        if (size <= 1) {
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
            return;
        }
        int radius = size / 2;
        int dx = std::abs(x2 - x1);
        int dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            drawFillCircle(x1, y1, radius);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx) { err += dx; y1 += sy; }
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

    // STATE

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

    void finalizeSelection() {
        if (!currentSelection.active) return;
        
        // Render the moved pixels onto the actual canvas
        SDL_SetRenderTarget(renderer, canvas);
        SDL_Texture* tempTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, currentSelection.area.w, currentSelection.area.h);
        SDL_SetTextureBlendMode(tempTex, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(tempTex, NULL, currentSelection.pixels.data(), currentSelection.area.w * 4);
        SDL_RenderCopy(renderer, tempTex, NULL, &currentSelection.area);
        SDL_DestroyTexture(tempTex);
        SDL_SetRenderTarget(renderer, NULL);
        
        currentSelection.active = false;
        saveState(undoStack);
        
        if (currentTool == Tool::SELECT && previousTool != Tool::SELECT) {
            currentTool = previousTool;
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
                        case SDLK_b: currentTool = Tool::BRUSH; previousTool = Tool::BRUSH; break;
                        case SDLK_l: currentTool = Tool::LINE; previousTool = Tool::LINE; break;
                        case SDLK_r: currentTool = Tool::RECT; previousTool = Tool::RECT; break;
                        case SDLK_o: currentTool = Tool::CIRCLE; previousTool = Tool::CIRCLE; break;
                        case SDLK_s: currentTool = Tool::SELECT; previousTool = Tool::SELECT; break;
                        case SDLK_UP: brushSize = std::min(20, brushSize + 1); break;
                        case SDLK_DOWN: brushSize = std::max(1, brushSize - 1); break;
                        case SDLK_z: if (event.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) undo(); break;
                        case SDLK_y: if (event.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) redo(); break;
                    }
                }

                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        int cX, cY;
                        getCanvasCoords(event.button.x, event.button.y, &cX, &cY);
                        
                        if (currentSelection.active) {
                            SDL_Point pt = {cX, cY};
                            if (SDL_PointInRect(&pt, &currentSelection.area)) {
                                currentSelection.isDragging = true;
                                currentSelection.offsetX = cX - currentSelection.area.x;
                                currentSelection.offsetY = cY - currentSelection.area.y;
                                if (currentTool != Tool::SELECT) {
                                    previousTool = currentTool;
                                    currentTool = Tool::SELECT;
                                }
                                isDrawing = false;
                                continue;
                            } else {
                                finalizeSelection();
                            }
                        }
                        
                        isDrawing = true;
                        startX = cX; startY = cY;
                        lastX = startX; lastY = startY;
                        redoStack.clear();
                    }
                }

                if (event.type == SDL_MOUSEBUTTONUP) {
                    if (currentSelection.isDragging) {
                        currentSelection.isDragging = false;
                    } else if (isDrawing) {
                        isDrawing = false;
                        int endX, endY;
                        getCanvasCoords(event.button.x, event.button.y, &endX, &endY);

                        if (currentTool == Tool::BRUSH) {
                            saveState(undoStack);
                        } else if (currentTool == Tool::SELECT) {
                            currentSelection.area = { std::min(startX, endX), std::min(startY, endY), 
                                                      std::max(1, std::abs(endX - startX)), std::max(1, std::abs(endY - startY)) };
                            
                            currentSelection.pixels.resize(currentSelection.area.w * currentSelection.area.h);
                            SDL_SetRenderTarget(renderer, canvas);
                            SDL_RenderReadPixels(renderer, &currentSelection.area, SDL_PIXELFORMAT_ARGB8888, currentSelection.pixels.data(), currentSelection.area.w * 4);
                            
                            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                            SDL_RenderFillRect(renderer, &currentSelection.area);
                            SDL_SetRenderTarget(renderer, NULL);
                            
                            currentSelection.active = true;
                        } else {
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
                }

                if (event.type == SDL_MOUSEMOTION) {
                    int cX, cY;
                    getCanvasCoords(event.motion.x, event.motion.y, &cX, &cY);
                    
                    if (currentSelection.active && currentSelection.isDragging) {
                        currentSelection.area.x = cX - currentSelection.offsetX;
                        currentSelection.area.y = cY - currentSelection.offsetY;
                    } else if (isDrawing && currentTool == Tool::BRUSH) {
                        SDL_SetRenderTarget(renderer, canvas);
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        drawLine(lastX, lastY, cX, cY, brushSize);
                        SDL_SetRenderTarget(renderer, NULL);
                        lastX = cX; lastY = cY;
                    }
                }
            }

            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255); 
            SDL_RenderClear(renderer);
            
            SDL_Rect view = getViewport();
            SDL_RenderCopy(renderer, canvas, NULL, &view);

            if (isDrawing && currentTool != Tool::BRUSH) {
                int mouseWinX, mouseWinY, curX, curY;
                SDL_GetMouseState(&mouseWinX, &mouseWinY);
                getCanvasCoords(mouseWinX, mouseWinY, &curX, &curY);
                
                int winStartX, winStartY, winCurX, winCurY;
                getWindowCoords(startX, startY, &winStartX, &winStartY);
                getWindowCoords(curX, curY, &winCurX, &winCurY);
                
                int scaledBrushSize = getWindowSize(brushSize);
                
                if (currentTool == Tool::SELECT) {
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Change to black
                    SDL_Rect r = { std::min(winStartX, winCurX), std::min(winStartY, winCurY), std::abs(winCurX - winStartX), std::abs(winCurY - winStartY) };
                    drawDottedRect(renderer, &r);
                } else {
                    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255); 
                    if (currentTool == Tool::LINE) drawLine(winStartX, winStartY, winCurX, winCurY, scaledBrushSize);
                    else if (currentTool == Tool::RECT) {
                        SDL_Rect r = { std::min(winStartX, winCurX), std::min(winStartY, winCurY), std::abs(winCurX - winStartX), std::abs(winCurY - winStartY) };
                        drawRect(&r, scaledBrushSize);
                    }
                    else if (currentTool == Tool::CIRCLE) drawCircle(winStartX, winStartY, winCurX, winCurY, scaledBrushSize);
                }
            }

            if (currentSelection.active) {
                int wX, wY, wX2, wY2;
                getWindowCoords(currentSelection.area.x, currentSelection.area.y, &wX, &wY);
                getWindowCoords(currentSelection.area.x + currentSelection.area.w, currentSelection.area.y + currentSelection.area.h, &wX2, &wY2);
                SDL_Rect scaledRect = { wX, wY, wX2 - wX, wY2 - wY };
                
                SDL_Texture* selectionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, currentSelection.area.w, currentSelection.area.h);
                SDL_SetTextureBlendMode(selectionTexture, SDL_BLENDMODE_BLEND);
                SDL_UpdateTexture(selectionTexture, NULL, currentSelection.pixels.data(), currentSelection.area.w * 4);
                
                SDL_RenderCopy(renderer, selectionTexture, NULL, &scaledRect);
                
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Change to black
                drawDottedRect(renderer, &scaledRect);
                
                SDL_DestroyTexture(selectionTexture);
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

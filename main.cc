#include <SDL2/SDL.h>

class MacPaint {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* canvas;
    
    bool isDrawing = false;
    int lastX, lastY;
    const int WIDTH = 800;
    const int HEIGHT = 600;

public:
    MacPaint() {
        SDL_Init(SDL_INIT_VIDEO);
        // Create the window and a renderer that allows drawing to textures
        window = SDL_CreateWindow("kPen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
        
        canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, WIDTH, HEIGHT);
        
        // Initialize the canvas
        SDL_SetRenderTarget(renderer, canvas);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderTarget(renderer, NULL);
    }

    ~MacPaint() {
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

                // Mouse Down: Start the stroke
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        isDrawing = true;
                        lastX = event.button.x;
                        lastY = event.button.y;
                    }
                }

                // Mouse Up: End the stroke
                if (event.type == SDL_MOUSEBUTTONUP) {
                    if (event.button.button == SDL_BUTTON_LEFT) isDrawing = false;
                }

                // Draw on mouse movement
                if (event.type == SDL_MOUSEMOTION && isDrawing) {
                    SDL_SetRenderTarget(renderer, canvas); // Switch focus to the canvas texture
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black ink
                    SDL_RenderDrawLine(renderer, lastX, lastY, event.motion.x, event.motion.y);
                    SDL_SetRenderTarget(renderer, NULL); // Switch focus back to the window
                    
                    lastX = event.motion.x;
                    lastY = event.motion.y;
                }
            }

            // Rendering: Show the canvas on the screen
            SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // Dark grey background
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, canvas, NULL, NULL);
            SDL_RenderPresent(renderer);
        }
    }
};

int main(int argc, char* argv[]) {
    MacPaint app;
    app.run();
    return 0;
}

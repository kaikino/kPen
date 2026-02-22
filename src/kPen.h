#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include "Constants.h"
#include "Tools.h"
#include "Toolbar.h"

class kPen : public ICoordinateMapper {
public:
    kPen();
    ~kPen();

    void run();

    // Called by Toolbar when a tool button is clicked
    void setTool(ToolType t);

    // ICoordinateMapper interface
    void getCanvasCoords(int winX, int winY, int* cX, int* cY) override;
    void getWindowCoords(int canX, int canY, int* wX, int* wY) override;
    int  getWindowSize(int canSize) override;

private:
    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  canvas;
    SDL_Texture*  overlay;

    std::unique_ptr<AbstractTool> currentTool;
    ToolType currentType  = ToolType::BRUSH;
    ToolType originalType = ToolType::BRUSH;

    Toolbar toolbar;

    std::vector<std::vector<uint32_t>> undoStack;

    SDL_Rect getViewport();
    void     saveState(std::vector<std::vector<uint32_t>>& stack);
    void     applyState(std::vector<uint32_t>& pixels);
    void     activateShapeSelection(SDL_Texture* tex, SDL_Rect bounds);
};

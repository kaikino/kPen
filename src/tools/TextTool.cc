#include "Tools.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>

// 5×7 bitmap font (matches toolbar tooltips). Index 0=space, 1–26=A–Z, 27–36=0–9, 37=comma.
static const uint8_t FONT_5X7[38][7] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11 },
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
    { 0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0F },
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E },
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 },
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x11, 0x1E },
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    { 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x04 },
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
    { 0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F },
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F },
    { 0x1F, 0x01, 0x01, 0x1F, 0x10, 0x10, 0x1F },
    { 0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F },
    { 0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01 },
    { 0x1F, 0x10, 0x10, 0x1F, 0x01, 0x01, 0x1F },
    { 0x1F, 0x10, 0x10, 0x1F, 0x11, 0x11, 0x1F },
    { 0x1F, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 },
    { 0x1F, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x1F },
    { 0x1F, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x1F },
    { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 },
};

static int glyphIndex(char c) {
    if (c == ' ') return 0;
    if (c == '.' || c == ',') return 37;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    return -1;
}

static int textScaleForBrush(int brushSize) {
    return std::max(1, std::min(6, brushSize / 2));
}

static int glyphAdvance(int scale) { return 5 * scale + scale; }

static int stringWidthPx(const std::string& s, int scale) {
    int adv = glyphAdvance(scale);
    int w = 0;
    for (char c : s) {
        if (glyphIndex(c) >= 0) w += adv;
    }
    return w;
}

static void drawGlyphPixels(SDL_Renderer* r, int ox, int oy, int gi,
                            int scale, SDL_Color col) {
    if (gi < 0 || gi > 37) return;
    if (col.a == 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    } else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    }
    for (int row = 0; row < 7; row++) {
        uint8_t bits = FONT_5X7[gi][row];
        for (int gx = 0; gx < 5; gx++) {
            if (bits & (1 << (4 - gx))) {
                SDL_Rect rect = { ox + gx * scale, oy + row * scale, scale, scale };
                SDL_RenderFillRect(r, &rect);
            }
        }
    }
    if (col.a == 0) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

TextTool::TextTool(ICoordinateMapper* m, std::function<void()> onAfterStamp)
    : AbstractTool(m), onAfterStamp(std::move(onAfterStamp)) {}

void TextTool::stopEditing() {
    if (!editing_) return;
    editing_ = false;
    SDL_StopTextInput();
}

void TextTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (!isPointOnCanvas(mapper, cX, cY)) return;
    cachedBrushSize = brushSize;
    cachedColor = color;
    if (editing_) commitEdit(r);
    anchorX = cX;
    anchorY = cY;
    buffer.clear();
    editing_ = true;
    SDL_StartTextInput();
}

bool TextTool::onMouseUp(int, int, SDL_Renderer*, int, SDL_Color) {
    return false;
}

void TextTool::onPreviewRender(SDL_Renderer*, int brushSize, SDL_Color color) {
    cachedBrushSize = brushSize;
    cachedColor = color;
}

void TextTool::onOverlayRender(SDL_Renderer* r) {
    if (!editing_) return;
    int scale = textScaleForBrush(cachedBrushSize);
    int x = anchorX;
    SDL_Color col = cachedColor;
    for (char ch : buffer) {
        int gi = glyphIndex(ch);
        if (gi < 0) continue;
        drawGlyphPixels(r, x, anchorY, gi, scale, col);
        x += glyphAdvance(scale);
    }
    bool blinkOn = ((SDL_GetTicks() / 530) % 2) == 0;
    if (blinkOn) {
        int caretx = anchorX + stringWidthPx(buffer, scale);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        if (col.a == 0)
            SDL_SetRenderDrawColor(r, 100, 149, 237, 200);
        else
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        int ch = 7 * scale;
        SDL_Rect caret = { caretx, anchorY, std::max(1, scale), ch };
        SDL_RenderFillRect(r, &caret);
    }
}

bool TextTool::stampToCanvas(SDL_Renderer* r) {
    if (buffer.empty()) return false;
    int scale = textScaleForBrush(cachedBrushSize);
    int x = anchorX;
    for (char ch : buffer) {
        int gi = glyphIndex(ch);
        if (gi < 0) continue;
        drawGlyphPixels(r, x, anchorY, gi, scale, cachedColor);
        x += glyphAdvance(scale);
    }
    return true;
}

void TextTool::commitEdit(SDL_Renderer* r) {
    if (!editing_) return;
    bool stamped = stampToCanvas(r);
    stopEditing();
    if (stamped && onAfterStamp) onAfterStamp();
}

void TextTool::discardEdit() {
    if (!editing_) return;
    buffer.clear();
    stopEditing();
}

void TextTool::deactivate(SDL_Renderer* r) {
    commitEdit(r);
}

bool TextTool::onTextInput(const char* text) {
    if (!editing_ || !text) return false;
    bool any = false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        unsigned char c = *p;
        if (c < 32 || c > 126) continue;
        char ch = static_cast<char>(c);
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (glyphIndex(ch) < 0) continue;
        if ((int)buffer.size() >= kMaxChars) break;
        buffer.push_back(ch);
        any = true;
    }
    return any;
}

bool TextTool::onKeyDown(SDL_Keycode key) {
    if (!editing_) return false;
    if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        if (!buffer.empty()) buffer.pop_back();
        return true;
    }
    return false;
}

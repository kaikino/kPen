#include "Tools.h"
#include "kPen.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

TextTool::TextTool(kPen* pen, std::function<void()> onAfterStamp)
    : AbstractTool(pen), pen_(pen), onAfterStamp(std::move(onAfterStamp)) {}

static void utf8PopBack(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    s.resize(i);
}

static SDL_BlendMode eraseGlyphBlendMode() {
    return SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}

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

void TextTool::drawUtf8Line(SDL_Renderer* r, bool forOverlay) {
    if (!pen_ || buffer.empty()) return;

    std::string path = pen_->toolbar.currentTextFontPath();
    int pt = pen_->toolbar.textFontPt;
    int style = TTF_STYLE_NORMAL;
    if (pen_->toolbar.textBold) style |= TTF_STYLE_BOLD;
    if (pen_->toolbar.textItalic) style |= TTF_STYLE_ITALIC;

    TTF_Font* font = pen_->fontCache_.get(path, pt, style);
    if (!font) return;

    SDL_Color fg = cachedColor;
    if (forOverlay && fg.a == 0)
        fg = { 100, 149, 237, 255 };
    else if (!forOverlay && fg.a != 0)
        fg.a = 255;

    SDL_Surface* surf = nullptr;
    if (forOverlay || cachedColor.a != 0)
        surf = TTF_RenderUTF8_Blended(font, buffer.c_str(), fg);
    else
        surf = TTF_RenderUTF8_Blended(font, buffer.c_str(), SDL_Color{ 255, 255, 255, 255 });

    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;

    if (!forOverlay && cachedColor.a == 0) {
        SDL_BlendMode eraseBm = eraseGlyphBlendMode();
        if (SDL_SetTextureBlendMode(tex, eraseBm) != 0)
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    } else {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }

    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    SDL_Rect dst{ anchorX, anchorY, tw, th };
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

void TextTool::onOverlayRender(SDL_Renderer* r) {
    if (!editing_ || !pen_) return;
    drawUtf8Line(r, true);

    std::string path = pen_->toolbar.currentTextFontPath();
    int pt = pen_->toolbar.textFontPt;
    int style = TTF_STYLE_NORMAL;
    if (pen_->toolbar.textBold) style |= TTF_STYLE_BOLD;
    if (pen_->toolbar.textItalic) style |= TTF_STYLE_ITALIC;
    TTF_Font* font = pen_->fontCache_.get(path, pt, style);

    int caretX = anchorX;
    int caretH = font ? TTF_FontHeight(font) : 14;
    if (font && !buffer.empty()) {
        int w = 0;
        if (TTF_SizeUTF8(font, buffer.c_str(), &w, nullptr) == 0)
            caretX += w;
    }

    bool blinkOn = ((SDL_GetTicks() / 530) % 2) == 0;
    if (blinkOn) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_Color col = cachedColor;
        if (col.a == 0)
            SDL_SetRenderDrawColor(r, 100, 149, 237, 200);
        else
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        int cw = std::max(1, pt / 12);
        SDL_Rect caret{ caretX, anchorY, cw, std::max(1, caretH) };
        SDL_RenderFillRect(r, &caret);
    }
}

bool TextTool::stampToCanvas(SDL_Renderer* r) {
    if (buffer.empty()) return false;
    drawUtf8Line(r, false);
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
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p;) {
        unsigned char c = *p;
        if (c < 32 && c != '\t') {
            ++p;
            continue;
        }
        size_t runeLen = 1;
        if (c >= 0xF0)
            runeLen = 4;
        else if (c >= 0xE0)
            runeLen = 3;
        else if (c >= 0xC0)
            runeLen = 2;

        if ((int)buffer.size() + (int)runeLen > kMaxBytes) break;
        for (size_t i = 0; i < runeLen && p[i]; i++)
            buffer.push_back(static_cast<char>(p[i]));
        p += runeLen;
        any = true;
    }
    return any;
}

bool TextTool::onKeyDown(SDL_Keycode key) {
    if (!editing_) return false;
    if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        utf8PopBack(buffer);
        return true;
    }
    return false;
}

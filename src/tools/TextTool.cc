#include "Tools.h"
#include "kPen.h"
#include "DrawingUtils.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

TextTool::TextTool(kPen* pen, std::function<void()> onAfterStamp)
    : TransformTool(pen), pen_(pen), onAfterStamp(std::move(onAfterStamp)) {}

static void utf8PopBack(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    s.resize(i);
}

static int utf8CharBytes(const char* p) {
    auto c = static_cast<unsigned char>(*p);
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

static SDL_BlendMode eraseGlyphBlendMode() {
    return SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}

/** When dragging a new text box with Shift held, keep width and height equal (same rule as unfilled rectangle). */
void TextTool::applyShiftSquare(int startX, int startY, int& curX, int& curY) {
    int dx = curX - startX, dy = curY - startY;
    if (dx == 0 && dy == 0) return;
    int adx = std::abs(dx), ady = std::abs(dy);
    int d = std::min(adx, ady);
    curX = startX + (dx >= 0 ? d : -d);
    curY = startY + (dy >= 0 ? d : -d);
}

void TextTool::clampBoundsToCanvas() {
    int cw, ch;
    mapper->getCanvasSize(&cw, &ch);
    if (currentBounds.x < 0) {
        currentBounds.w += currentBounds.x;
        currentBounds.x = 0;
    }
    if (currentBounds.y < 0) {
        currentBounds.h += currentBounds.y;
        currentBounds.y = 0;
    }
    currentBounds.w = std::max(kMinBoxW, currentBounds.w);
    currentBounds.h = std::max(kMinBoxH, currentBounds.h);
    if (currentBounds.x + currentBounds.w > cw)
        currentBounds.w = std::max(kMinBoxW, cw - currentBounds.x);
    if (currentBounds.y + currentBounds.h > ch)
        currentBounds.h = std::max(kMinBoxH, ch - currentBounds.y);
    if (currentBounds.x + currentBounds.w > cw)
        currentBounds.x = std::max(0, cw - currentBounds.w);
    if (currentBounds.y + currentBounds.h > ch)
        currentBounds.y = std::max(0, ch - currentBounds.h);
    syncDrawCenterFromBounds();
}

void TextTool::beginEditingWithRect(SDL_Rect box) {
    currentBounds = box;
    clampBoundsToCanvas();
    rotation = 0.f;
    moved = false;
    resizing = Handle::NONE;
    isMoving = false;
    isRotating = false;
    flipX = false;
    flipY = false;
    editing_ = true;
    buffer.clear();
    caretByte = 0;
    SDL_StartTextInput();
}

void TextTool::stopEditing() {
    if (!editing_) return;
    editing_ = false;
    handleMouseUp();
    SDL_StopTextInput();
}

/**
 * Fills `lines` with half-open byte ranges [first, second) for each visual row, matching greedy word-wrap
 * used with TTF_RenderUTF8_Blended_Wrapped at the same wrap width.
 */
bool TextTool::buildLayoutLines(TTF_Font* font, int wrapPx, std::vector<std::pair<int, int>>& lines) const {
    lines.clear();
    wrapPx = std::max(8, wrapPx);
    const std::string& buf = buffer;
    const int n = (int)buf.size();

    auto wrapParagraph = [&](int a, int b) {
        int p = a;
        while (p < b) {
            const int lineStart = p;
            int lastSpaceStart = -1;
            int lastSpaceEnd = -1;
            int q = p;
            while (q < b) {
                const int cl = utf8CharBytes(buf.c_str() + q);
                std::string chunk(buf.data() + lineStart, (size_t)(q + cl - lineStart));
                int tw = 0;
                if (TTF_SizeUTF8(font, chunk.c_str(), &tw, nullptr) != 0)
                    break;
                if (tw > wrapPx) {
                    if (lastSpaceStart >= lineStart) {
                        lines.emplace_back(lineStart, lastSpaceEnd);
                        p = lastSpaceEnd;
                        goto next_line;
                    }
                    if (q == lineStart) {
                        lines.emplace_back(lineStart, q + cl);
                        p = q + cl;
                    } else {
                        lines.emplace_back(lineStart, q);
                        p = q;
                    }
                    goto next_line;
                }
                if (cl == 1 && buf[(size_t)q] == ' ') {
                    lastSpaceStart = q;
                    lastSpaceEnd = q + cl;
                }
                q += cl;
            }
            lines.emplace_back(lineStart, b);
            p = b;
        next_line:;
        }
    };

    int paraStart = 0;
    while (paraStart <= n) {
        int paraEnd = n;
        for (int i = paraStart; i < n; i++) {
            if (buf[(size_t)i] == '\n') {
                paraEnd = i;
                break;
            }
        }
        if (paraEnd > paraStart)
            wrapParagraph(paraStart, paraEnd);
        else if (paraEnd < n && buf[(size_t)paraEnd] == '\n')
            lines.emplace_back(paraEnd, paraEnd + 1);
        if (paraEnd >= n)
            break;
        paraStart = paraEnd + 1;
    }
    if (lines.empty())
        lines.emplace_back(0, 0);
    return true;
}

void TextTool::placeCaretFromCanvas(int cX, int cY) {
    std::string path = pen_->toolbar.currentTextFontPath();
    int pt = pen_->toolbar.textFontPt;
    int style = TTF_STYLE_NORMAL;
    if (pen_->toolbar.textBold) style |= TTF_STYLE_BOLD;
    if (pen_->toolbar.textItalic) style |= TTF_STYLE_ITALIC;
    TTF_Font* font = pen_->fontCache_.get(path, pt, style);
    if (!font) return;

    const float hw = currentBounds.w * 0.5f, hh = currentBounds.h * 0.5f;
    float lx, ly;
    rotatePt((float)cX, (float)cY, drawCenterX, drawCenterY, -getRotation(), lx, ly);
    const float localX = lx - (drawCenterX - hw);
    const float localY = ly - (drawCenterY - hh);

    const int wrapPx = std::max(8, currentBounds.w - 2 * kPad);
    std::vector<std::pair<int, int>> lines;
    buildLayoutLines(font, wrapPx, lines);
    const int lineSkip = TTF_FontLineSkip(font);
    int li = (int)std::floor((localY - kPad) / (float)lineSkip);
    if (li < 0) li = 0;
    if (li >= (int)lines.size()) li = (int)lines.size() - 1;

    const int b0 = lines[(size_t)li].first;
    const int b1 = lines[(size_t)li].second;
    const float targetX = localX - kPad;
    if (targetX <= 0) {
        caretByte = b0;
        return;
    }
    if (b1 <= b0) {
        caretByte = b0;
        return;
    }

    int cp = b0;
    while (cp < b1) {
        const int cl = utf8CharBytes(buffer.c_str() + cp);
        int w = 0;
        std::string prefix(buffer.data() + b0, (size_t)(cp + cl - b0));
        if (TTF_SizeUTF8(font, prefix.c_str(), &w, nullptr) != 0)
            break;
        if (w > targetX + 2)
            break;
        cp += cl;
    }
    caretByte = cp;
}

/**
 * Renders wrapped UTF-8 into an axis-aligned w×h texture, then composites onto the active render target
 * at the text box position with the current rotation (same idea as ResizeTool::renderShapeAt).
 */
void TextTool::renderTextOntoCanvas(SDL_Renderer* r, SDL_Color fg, bool forOverlay) const {
    if (!pen_ || currentBounds.w <= 0 || currentBounds.h <= 0) return;

    std::string path = pen_->toolbar.currentTextFontPath();
    int pt = pen_->toolbar.textFontPt;
    int style = TTF_STYLE_NORMAL;
    if (pen_->toolbar.textBold) style |= TTF_STYLE_BOLD;
    if (pen_->toolbar.textItalic) style |= TTF_STYLE_ITALIC;
    TTF_Font* font = pen_->fontCache_.get(path, pt, style);
    if (!font) return;

    if (forOverlay && fg.a == 0)
        fg = {100, 149, 237, 255};
    else if (!forOverlay && fg.a != 0)
        fg.a = 255;

    const int w = currentBounds.w, h = currentBounds.h;
    const int wrapPx = std::max(8, w - 2 * kPad);

    SDL_Surface* textSurf = nullptr;
    if (buffer.empty()) {
        textSurf = nullptr;
    } else if (forOverlay || cachedColor.a != 0)
        textSurf = TTF_RenderUTF8_Blended_Wrapped(font, buffer.c_str(), fg, wrapPx);
    else
        textSurf = TTF_RenderUTF8_Blended_Wrapped(font, buffer.c_str(), SDL_Color{255, 255, 255, 255}, wrapPx);

    SDL_Texture* tmp = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tmp) {
        if (textSurf) SDL_FreeSurface(textSurf);
        return;
    }
    SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tmp);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    if (textSurf) {
        SDL_Texture* lineTex = SDL_CreateTextureFromSurface(r, textSurf);
        SDL_FreeSurface(textSurf);
        if (lineTex) {
            if (!forOverlay && cachedColor.a == 0) {
                SDL_BlendMode eraseBm = eraseGlyphBlendMode();
                if (SDL_SetTextureBlendMode(lineTex, eraseBm) != 0)
                    SDL_SetTextureBlendMode(lineTex, SDL_BLENDMODE_BLEND);
            } else
                SDL_SetTextureBlendMode(lineTex, SDL_BLENDMODE_BLEND);
            int tw = 0, th = 0;
            SDL_QueryTexture(lineTex, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{kPad, kPad, tw, std::min(th, h - 2 * kPad)};
            SDL_RenderCopy(r, lineTex, nullptr, &dst);
            SDL_DestroyTexture(lineTex);
        }
    }

    if (forOverlay) {
        std::vector<std::pair<int, int>> lines;
        buildLayoutLines(font, wrapPx, lines);
        const int lineSkip = TTF_FontLineSkip(font);
        int li = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            const int a = lines[i].first, b = lines[i].second;
            if (caretByte >= a && caretByte <= b) {
                li = (int)i;
                break;
            }
            if ((int)i + 1 < (int)lines.size() && caretByte > b && caretByte < lines[i + 1].first)
                li = (int)i + 1;
            else if (caretByte > b)
                li = (int)i;
        }
        const int b0 = lines[(size_t)li].first;
        int caretW = std::max(1, pt / 12);
        int cx = kPad;
        if (caretByte > b0 && caretByte <= buffer.size()) {
            std::string prefix(buffer.data() + b0, (size_t)(caretByte - b0));
            int cw = 0;
            if (TTF_SizeUTF8(font, prefix.c_str(), &cw, nullptr) == 0)
                cx += cw;
        }
        const int cy = kPad + li * lineSkip;
        const int caretH = std::max(1, lineSkip - 2);
        bool blinkOn = ((SDL_GetTicks() / 530) % 2) == 0;
        if (blinkOn) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, fg.r, fg.g, fg.b, 255);
            SDL_Rect caret{cx, cy, caretW, caretH};
            SDL_RenderFillRect(r, &caret);
        }
    }

    SDL_SetRenderTarget(r, prev);

    const float rot = getRotation();
    double rotDeg = std::fmod(rot * 180.0 / M_PI, 360.0);
    if (rotDeg < 0.0) rotDeg += 360.0;
    const bool parityDiff = (w & 1) != (h & 1);
    const bool is90or270 = (std::fabs(rotDeg - 90.0) < 1.0 || std::fabs(rotDeg - 270.0) < 1.0);
    float drawX = getDrawCenterX() - w * 0.5f, drawY = getDrawCenterY() - h * 0.5f;
    if (is90or270 && parityDiff) {
        drawX = (float)std::round(drawX);
        drawY = (float)std::round(drawY);
    }

    if (rot == 0.f) {
        SDL_FRect dstF{drawX, drawY, (float)w, (float)h};
        SDL_RenderCopyF(r, tmp, nullptr, &dstF);
        SDL_DestroyTexture(tmp);
        return;
    }

    float hw = w * 0.5f, hh = h * 0.5f;
    float pivotX = hw, pivotY = hh;
    if (is90or270 && parityDiff) {
        pivotX = (float)std::round(hw);
        pivotY = (float)std::round(hh);
    }
    SDL_FRect dstF = {drawX, drawY, (float)w, (float)h};
    SDL_FPoint centerF = {pivotX, pivotY};
    SDL_RenderCopyExF(r, tmp, nullptr, &dstF, rotDeg, &centerF, SDL_FLIP_NONE);
    SDL_DestroyTexture(tmp);
}

bool TextTool::stampToCanvas(SDL_Renderer* r) {
    if (buffer.empty()) return false;
    renderTextOntoCanvas(r, cachedColor, false);
    return true;
}

void TextTool::commitEdit(SDL_Renderer* r) {
    if (!editing_) return;
    bool stamped = stampToCanvas(r);
    stopEditing();
    rotation = 0.f;
    currentBounds = {0, 0, 0, 0};
    if (stamped && onAfterStamp) onAfterStamp();
}

void TextTool::discardEdit() {
    if (!editing_) return;
    buffer.clear();
    caretByte = 0;
    stopEditing();
    rotation = 0.f;
    currentBounds = {0, 0, 0, 0};
}

void TextTool::deactivate(SDL_Renderer* r) {
    commitEdit(r);
}

void TextTool::onMouseDown(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    cachedBrushSize = brushSize;
    cachedColor = color;
    if (!isPointOnCanvas(mapper, cX, cY)) return;

    if (editing_) {
        Handle h = getHandle(cX, cY);
        if (h != Handle::NONE) {
            handleMouseDown(cX, cY);
            return;
        }
        if ((SDL_GetModState() & KMOD_ALT) && pointInRotatedBounds(cX, cY)) {
            handleMouseDown(cX, cY);
            return;
        }
        if (pointInRotatedBounds(cX, cY)) {
            placeCaretFromCanvas(cX, cY);
            return;
        }
        commitEdit(r);
    }

    AbstractTool::onMouseDown(cX, cY, r, brushSize, color);
}

void TextTool::onMouseMove(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    cachedBrushSize = brushSize;
    cachedColor = color;
    if (isDrawing) {
        AbstractTool::onMouseMove(cX, cY, r, brushSize, color);
        return;
    }
    if (editing_ && isMutating())
        handleMouseMove(cX, cY);
}

bool TextTool::onMouseUp(int cX, int cY, SDL_Renderer* r, int brushSize, SDL_Color color) {
    if (isDrawing) {
        int curX = cX, curY = cY;
        if (SDL_GetModState() & KMOD_SHIFT)
            applyShiftSquare(startX, startY, curX, curY);

        const bool clickOnly = (curX == startX && curY == startY);
        SDL_Rect box{};
        int cw, ch;
        mapper->getCanvasSize(&cw, &ch);

        if (clickOnly) {
            int w = std::min(kDefaultBoxW, cw - startX);
            int h = std::min(kDefaultBoxH, ch - startY);
            int sx = startX, sy = startY;
            if (w < kMinBoxW) {
                sx = std::max(0, std::min(sx, cw - kMinBoxW));
                w = std::min(kMinBoxW, cw - sx);
            }
            if (h < kMinBoxH) {
                sy = std::max(0, std::min(sy, ch - kMinBoxH));
                h = std::min(kMinBoxH, ch - sy);
            }
            box = {sx, sy, w, h};
        } else {
            int minX = std::min(startX, curX), minY = std::min(startY, curY);
            int dw = std::abs(curX - startX), dh = std::abs(curY - startY);
            dw = std::max(kMinBoxW, dw);
            dh = std::max(kMinBoxH, dh);
            box = {minX, minY, dw, dh};
        }
        isDrawing = false;
        beginEditingWithRect(box);
        return false;
    }
    if (editing_)
        handleMouseUp();
    return false;
}

void TextTool::onPreviewRender(SDL_Renderer* r, int brushSize, SDL_Color color) {
    cachedBrushSize = brushSize;
    cachedColor = color;
    if (isDrawing) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        int curX, curY;
        mapper->getCanvasCoords(mouseX, mouseY, &curX, &curY);
        if (curX == startX && curY == startY) return;
        if (SDL_GetModState() & KMOD_SHIFT)
            applyShiftSquare(startX, startY, curX, curY);
        int minX = std::min(startX, curX), minY = std::min(startY, curY);
        int dw = std::abs(curX - startX), dh = std::abs(curY - startY);
        int wx0, wy0, wx1, wy1;
        mapper->getWindowCoords(minX, minY, &wx0, &wy0);
        mapper->getWindowCoords(minX + dw, minY + dh, &wx1, &wy1);
        SDL_Rect outline = {wx0, wy0, wx1 - wx0, wy1 - wy0};
        DrawingUtils::drawMarchingRect(r, &outline);
        return;
    }
    if (editing_)
        drawHandles(r);
}

void TextTool::onOverlayRender(SDL_Renderer* r) {
    if (!pen_) return;
    if (isDrawing) return;
    if (!editing_) return;
    SDL_Color fg = cachedColor;
    renderTextOntoCanvas(r, fg, true);
}

bool TextTool::onTextInput(const char* text) {
    if (!editing_ || !text) return false;
    bool any = false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p;) {
        unsigned char c = *p;
        if (c == '\r') {
            p++;
            if ((int)buffer.size() + 1 > kMaxBytes) break;
            const int ins = std::max(0, std::min(caretByte, (int)buffer.size()));
            buffer.insert((size_t)ins, "\n");
            caretByte = ins + 1;
            any = true;
            continue;
        }
        if (c < 32 && c != '\t' && c != '\n') {
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
        std::string chunk;
        for (size_t i = 0; i < runeLen && p[i]; i++)
            chunk.push_back(static_cast<char>(p[i]));
        p += runeLen;
        const int ins = std::max(0, std::min(caretByte, (int)buffer.size()));
        buffer.insert((size_t)ins, chunk);
        caretByte = ins + (int)chunk.size();
        any = true;
    }
    return any;
}

bool TextTool::onKeyDown(SDL_Keycode key) {
    if (!editing_) return false;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if ((int)buffer.size() >= kMaxBytes) return true;
        buffer.push_back('\n');
        caretByte = (int)buffer.size();
        return true;
    }
    if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        if (key == SDLK_BACKSPACE) {
            if (caretByte <= 0) return true;
            const int end = caretByte;
            int i = end - 1;
            while (i > 0 && ((unsigned char)buffer[(size_t)i] & 0xC0) == 0x80) --i;
            buffer.erase((size_t)i, (size_t)(end - i));
            caretByte = i;
        } else {
            if (caretByte >= (int)buffer.size()) return true;
            size_t i = (size_t)caretByte;
            size_t j = i + (size_t)utf8CharBytes(buffer.c_str() + i);
            buffer.erase(i, j - i);
        }
        return true;
    }
    return false;
}

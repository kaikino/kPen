#define _USE_MATH_DEFINES
#include "Toolbar.h"
#include "kPen.h"
#include <cmath>
#include <algorithm>

// ── ToolTypes ────────────────────────────────────────────────────────────────
constexpr int toolGrid[3][3] = {{0,1,-1},{2,3,-1},{4,5,-1}};
constexpr ToolType toolTypes[] = {
    ToolType::BRUSH, ToolType::LINE, ToolType::RECT,
    ToolType::CIRCLE, ToolType::SELECT, ToolType::FILL
};

// ─────────────────────────────────────────────────────────────────────────────

Toolbar::Toolbar(SDL_Renderer* renderer, kPen* app)
    : renderer(renderer), app(app)
{
    rgbToHsv(brushColor, hue, sat, val);
}

// ── HSV helpers ───────────────────────────────────────────────────────────────

SDL_Color Toolbar::hsvToRgb(float h, float s, float v) {
    h = fmod(h, 1.f) * 6.f;
    int   i = (int)h;
    float f = h - i, p = v*(1-s), q = v*(1-s*f), t = v*(1-s*(1-f));
    float r, g, b;
    switch (i % 6) {
        case 0: r=v;g=t;b=p; break; case 1: r=q;g=v;b=p; break;
        case 2: r=p;g=v;b=t; break; case 3: r=p;g=q;b=v; break;
        case 4: r=t;g=p;b=v; break; default: r=v;g=p;b=q; break;
    }
    return {(Uint8)(r*255), (Uint8)(g*255), (Uint8)(b*255), 255};
}

void Toolbar::rgbToHsv(SDL_Color c, float& h, float& s, float& v) {
    float r=c.r/255.f, g=c.g/255.f, b=c.b/255.f;
    float mx=std::max({r,g,b}), mn=std::min({r,g,b}), d=mx-mn;
    v = mx; s = mx < 1e-6f ? 0 : d/mx;
    if (d < 1e-6f) { h = 0; return; }
    if      (mx == r) h = fmod((g-b)/d, 6.f) / 6.f;
    else if (mx == g) h = ((b-r)/d + 2) / 6.f;
    else              h = ((r-g)/d + 4) / 6.f;
    if (h < 0) h += 1.f;
}

// ── O(1) swatch hit-tests ─────────────────────────────────────────────────────

int Toolbar::hitCustomSwatch(int x, int y) const {
    int sz = swatchCellSize(), stride = swatchCellStride();
    int lx = x - TB_PAD, ly = y - customGridY;
    if (lx < 0 || ly < 0) return -1;
    int col = lx / stride, row = ly / stride;
    if (col >= 3 || row >= 3) return -1;
    if (lx % stride >= sz || ly % stride >= sz) return -1;
    return row * 3 + col;
}

int Toolbar::hitPresetSwatch(int x, int y) const {
    int sz = swatchCellSize(), stride = swatchCellStride();
    int lx = x - TB_PAD, ly = y - presetGridY;
    if (lx < 0 || ly < 0) return -1;
    int col = lx / stride, row = ly / stride;
    if (col >= 3 || row >= 9) return -1;
    if (lx % stride >= sz || ly % stride >= sz) return -1;
    return row * 3 + col;
}

// ── Icon drawing ──────────────────────────────────────────────────────────────

void Toolbar::drawIcon(int cx, int cy, ToolType t, bool active) {
    SDL_Color fg = active ? SDL_Color{255,255,255,255} : SDL_Color{160,160,170,255};
    SDL_SetRenderDrawColor(renderer, fg.r, fg.g, fg.b, 255);
    int s = ICON_SIZE/2 - 3;
    switch (t) {
        case ToolType::BRUSH: {
            for (int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i,   cy+i);
            for (int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i+1, cy+i);
            SDL_Rect tip = {cx+s-1, cy+s-1, 3, 3};
            SDL_RenderFillRect(renderer, &tip);
            break;
        }
        case ToolType::LINE: {
            for (int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i,   cy-i);
            for (int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i+1, cy-i);
            break;
        }
        case ToolType::RECT: {
            SDL_Rect r = {cx-s, cy-s, s*2, s*2};
            SDL_RenderDrawRect(renderer, &r);
            break;
        }
        case ToolType::CIRCLE: {
            for (int deg=0; deg<360; deg+=5) {
                float a = deg * M_PI / 180.f;
                SDL_RenderDrawPoint(renderer, cx+(int)(s*cos(a)), cy+(int)(s*sin(a)));
            }
            break;
        }
        case ToolType::SELECT: {
            int d = 3;
            for (int i=0; i<s*2; i+=d*2) {
                SDL_RenderDrawLine(renderer, cx-s+i, cy-s, cx-s+std::min(i+d,s*2), cy-s);
                SDL_RenderDrawLine(renderer, cx-s+i, cy+s, cx-s+std::min(i+d,s*2), cy+s);
            }
            for (int i=0; i<s*2; i+=d*2) {
                SDL_RenderDrawLine(renderer, cx-s, cy-s+i, cx-s, cy-s+std::min(i+d,s*2));
                SDL_RenderDrawLine(renderer, cx+s, cy-s+i, cx+s, cy-s+std::min(i+d,s*2));
            }
            break;
        }
        case ToolType::FILL: {
            int ox = cx - 2;
            int oy = cy + 2;
            s -= 2;

            // Diamond outline
            SDL_RenderDrawLine(renderer, ox,    oy-s, ox+s, oy  ); // top-right
            SDL_RenderDrawLine(renderer, ox+s,  oy,   ox,   oy+s); // bottom-right
            SDL_RenderDrawLine(renderer, ox,    oy+s, ox-s, oy  ); // bottom-left
            SDL_RenderDrawLine(renderer, ox-s,  oy,   ox,   oy-s); // top-left

            // Fill bottom half: triangle with wide base at middle, narrows to point at bottom
            for (int row = 2; row <= s; row++) {
                int halfW = s - row;
                SDL_RenderDrawLine(renderer, ox - halfW, oy + row, ox + halfW, oy + row);
            }

            // Handle: angled same direction as top-right edge (slope +1, going up-right from top corner)
            int hLen = 3; // stay within icon box
            SDL_RenderDrawLine(renderer, ox,      oy-s,      ox-hLen,   oy-s-hLen);
            SDL_RenderDrawLine(renderer, ox-1,    oy-s,      ox-hLen-1, oy-s-hLen);

            // Drip below bottom-right
            int dx = ox + s + 2, dy = cy + 2;
            SDL_RenderDrawPoint(renderer, dx,   dy  );
            SDL_RenderDrawLine(renderer, dx-1, dy+1, dx+1, dy+1);
            SDL_RenderDrawLine(renderer, dx-2, dy+2, dx+2, dy+2);
            SDL_RenderDrawLine(renderer, dx-2, dy+3, dx+2, dy+3);
            SDL_RenderDrawLine(renderer, dx-1, dy+4, dx+1, dy+4);
            break;
        }
    }
}

// ── Full draw ─────────────────────────────────────────────────────────────────

void Toolbar::draw() {
    int winW, winH;
    SDL_GetWindowSize(SDL_RenderGetWindow(renderer), &winW, &winH);

    // Background panel + right border
    SDL_Rect panel = {0, 0, TB_W, winH};
    SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, TB_W-1, 0, TB_W-1, winH);

    // Clamp scroll so content doesn't scroll past top or leave gap at bottom
    // We'll clamp after computing total content height at the end of draw.
    // For now just apply current scrollY.
    const int S = scrollY; // alias for brevity

    // ── Tool buttons (3 per row) ──
    int cellW = (TB_W - TB_PAD) / 3;
    int ty = toolStartY() - S;
    for (int row=0; row<3; row++) {
        for (int col=0; col<3; col++) {
            int idx = toolGrid[row][col];
            int bx = TB_PAD/2 + col*cellW;
            int by = ty + row*(ICON_SIZE+ICON_GAP);
            SDL_Rect btn = {bx, by, cellW-2, ICON_SIZE};
            if (idx < 0) {
                SDL_SetRenderDrawColor(renderer, 35, 35, 40, 255);
                SDL_RenderFillRect(renderer, &btn);
                SDL_SetRenderDrawColor(renderer, 55, 55, 62, 255);
                SDL_RenderDrawRect(renderer, &btn);
                continue;
            }
            bool active = (currentType == toolTypes[idx]);
            SDL_SetRenderDrawColor(renderer, active ? 70 : 45, active ? 130 : 45, active ? 220 : 52, 255);
            SDL_RenderFillRect(renderer, &btn);
            SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
            SDL_RenderDrawRect(renderer, &btn);
            drawIcon(bx + (cellW-2)/2, by + ICON_SIZE/2, toolTypes[idx], active);
        }
    }

    // ── Brush size preview circle ──
    int previewAreaH = 20;
    int labelY   = ty + 3*(ICON_SIZE+ICON_GAP) + 2;
    int previewCX = TB_W / 2;
    int previewCY = labelY + previewAreaH / 2;
    int maxR  = previewAreaH / 2 - 1;
    int dotR  = std::max(1, (int)((brushSize / 20.f) * maxR + 0.5f));
    SDL_SetRenderDrawColor(renderer, brushColor.r, brushColor.g, brushColor.b, 255);
    for (int py=-dotR; py<=dotR; py++)
        for (int px=-dotR; px<=dotR; px++)
            if (px*px + py*py <= dotR*dotR)
                SDL_RenderDrawPoint(renderer, previewCX+px, previewCY+py);

    // ── Thickness slider (horizontal) ──
    int sliderY = labelY + previewAreaH + 2;
    int sX = TB_PAD, sW = TB_W - TB_PAD*2, sH = 14;
    int trackY = sliderY + sH/2;
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, sX, trackY,   sX+sW, trackY);
    SDL_RenderDrawLine(renderer, sX, trackY+1, sX+sW, trackY+1);
    int thumbX = sX + (int)((brushSize-1)/19.f * sW);
    SDL_Rect thumb = {thumbX-5, sliderY, 10, sH};
    SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
    SDL_RenderFillRect(renderer, &thumb);
    SDL_SetRenderDrawColor(renderer, 120, 120, 130, 255);
    SDL_RenderDrawRect(renderer, &thumb);

    // ── Color wheel ──
    int wTop   = sliderY + sH + 8;
    int availH = winH - wTop - TB_PAD;
    int wheelDiam = std::min(TB_W - TB_PAD*2, availH - 20);
    if (wheelDiam < 10) return;
    int wcx = TB_W/2, wcy = wTop + wheelDiam/2, wr = wheelDiam/2;
    colorWheelCX = wcx; colorWheelCY = wcy; colorWheelR = wr;

    for (int py=wcy-wr; py<=wcy+wr; py++) {
        for (int px=wcx-wr; px<=wcx+wr; px++) {
            float dx=px-wcx, dy=py-wcy, dist=sqrt(dx*dx+dy*dy);
            if (dist > wr) continue;
            float h = fmod(atan2(dy,dx)/(2*M_PI)+1.f, 1.f);
            float s = dist / wr;
            SDL_Color c = hsvToRgb(h, s, val);
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
            SDL_RenderDrawPoint(renderer, px, py);
        }
    }
    for (int deg=0; deg<360; deg++) {
        float a = deg * M_PI / 180.f;
        SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
        SDL_RenderDrawPoint(renderer, wcx+(int)(wr*cos(a)), wcy+(int)(wr*sin(a)));
    }
    float cursorAngle = hue * 2.f * M_PI;
    int cursorX = wcx + (int)(sat * wr * cos(cursorAngle));
    int cursorY = wcy + (int)(sat * wr * sin(cursorAngle));
    SDL_Rect cur  = {cursorX-4, cursorY-4, 8, 8};
    SDL_Rect cur2 = {cursorX-3, cursorY-3, 6, 6};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); SDL_RenderDrawRect(renderer, &cur);
    SDL_SetRenderDrawColor(renderer, 0,   0,   0,   255); SDL_RenderDrawRect(renderer, &cur2);

    // ── Brightness bar ──
    int bTop = wTop + wheelDiam + 6, bH = 12;
    int bX = TB_PAD, bW = TB_W - TB_PAD*2;
    brightnessRect = {bX, bTop, bW, bH};
    for (int px=bX; px<bX+bW; px++) {
        float t = (float)(px-bX) / bW;
        SDL_Color c = hsvToRgb(hue, sat, t);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderDrawLine(renderer, px, bTop, px, bTop+bH);
    }
    SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
    SDL_RenderDrawRect(renderer, &brightnessRect);
    int bCurX = bX + (int)(val * bW);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer, bCurX,   bTop-2, bCurX,   bTop+bH+2);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawLine(renderer, bCurX+1, bTop-2, bCurX+1, bTop+bH+2);

    // ── Custom color slots (3x3) ──
    int csy = bTop + bH + 7;
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, TB_PAD, csy, TB_W-TB_PAD, csy);
    csy += 4;
    customGridY = csy;
    int sz = swatchCellSize(), stride = swatchCellStride();
    for (int i=0; i<NUM_CUSTOM; i++) {
        int col=i%3, row=i/3;
        int sx=TB_PAD+col*stride, sy=csy+row*stride;
        SDL_Rect r = {sx, sy, sz, sz};
        SDL_SetRenderDrawColor(renderer, customColors[i].r, customColors[i].g, customColors[i].b, 255);
        SDL_RenderFillRect(renderer, &r);
        if (i == selectedCustomSlot) {
            SDL_Rect outer={sx-2,sy-2,sz+4,sz+4}; SDL_SetRenderDrawColor(renderer,255,255,255,255); SDL_RenderDrawRect(renderer,&outer);
            SDL_Rect inner={sx-1,sy-1,sz+2,sz+2}; SDL_SetRenderDrawColor(renderer,0,0,0,255);       SDL_RenderDrawRect(renderer,&inner);
        } else {
            SDL_SetRenderDrawColor(renderer, 70, 70, 80, 255);
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    // ── Preset colors (27, 3 per row = 9 rows) ──
    int psy = csy + 3*stride + 7;
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, TB_PAD, psy, TB_W-TB_PAD, psy);
    psy += 4;
    presetGridY = psy;
    for (int i=0; i<27; i++) {
        int col=i%3, row=i/3;
        int sx=TB_PAD+col*stride, sy=psy+row*stride;
        SDL_Rect r = {sx, sy, sz, sz};
        SDL_SetRenderDrawColor(renderer, PRESETS[i].r, PRESETS[i].g, PRESETS[i].b, 255);
        SDL_RenderFillRect(renderer, &r);
        if (i == selectedPresetSlot) {
            SDL_Rect outer={sx-2,sy-2,sz+4,sz+4}; SDL_SetRenderDrawColor(renderer,255,255,255,255); SDL_RenderDrawRect(renderer,&outer);
            SDL_Rect inner={sx-1,sy-1,sz+2,sz+2}; SDL_SetRenderDrawColor(renderer,0,0,0,255);       SDL_RenderDrawRect(renderer,&inner);
        } else {
            SDL_SetRenderDrawColor(renderer, 70, 70, 80, 255);
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    // ── Clamp scroll and draw subtle scrollbar ──
    int totalContentH = psy + 9*stride + TB_PAD; // bottom of last preset row
    maxScrollCache = std::max(0, totalContentH - winH);
    // Don't clamp here — overscroll is allowed; tickScroll() handles snap-back.

    if (maxScrollCache > 0) {
        // Thin scrollbar on right edge of toolbar
        int sbW = 3, sbX = TB_W - sbW - 1;
        float ratio = (float)winH / totalContentH;
        int sbH   = std::max(20, (int)(winH * ratio));
        int sbTop = (int)((float)scrollY / totalContentH * winH);
        SDL_Rect sbTrack = {sbX, 0, sbW, winH};
        SDL_SetRenderDrawColor(renderer, 50, 50, 58, 255);
        SDL_RenderFillRect(renderer, &sbTrack);
        SDL_Rect sbThumb = {sbX, sbTop, sbW, sbH};
        SDL_SetRenderDrawColor(renderer, 100, 100, 115, 255);
        SDL_RenderFillRect(renderer, &sbThumb);
    }
}

// ── Mouse update helpers ──────────────────────────────────────────────────────

void Toolbar::updateSliderFromMouse(int x) {
    int sX = TB_PAD, sW = TB_W - TB_PAD*2;
    int clamped = std::max(sX, std::min(sX+sW, x));
    brushSize = 1 + (int)((float)(clamped-sX) / sW * 19.f + 0.5f);
    brushSize = std::max(1, std::min(20, brushSize));
}

void Toolbar::updateWheelFromMouse(int x, int y) {
    float dx=x-colorWheelCX, dy=y-colorWheelCY;
    float dist = sqrt(dx*dx + dy*dy);
    hue = fmod(atan2(dy,dx)/(2*M_PI)+1.f, 1.f);
    sat = std::min(1.f, dist / colorWheelR);
    brushColor = hsvToRgb(hue, sat, val);
    selectedPresetSlot = -1;
    if (selectedCustomSlot >= 0) customColors[selectedCustomSlot] = brushColor;
}

void Toolbar::updateBrightnessFromMouse(int x) {
    float t = (float)(x - brightnessRect.x) / brightnessRect.w;
    val = std::max(0.f, std::min(1.f, t));
    brushColor = hsvToRgb(hue, sat, val);
    selectedPresetSlot = -1;
    if (selectedCustomSlot >= 0) customColors[selectedCustomSlot] = brushColor;
}

// ── Event handling ────────────────────────────────────────────────────────────

bool Toolbar::onMouseDown(int x, int y) {
    if (!inToolbar(x, y)) return false;
    userScrolling = false;

    // Adjust y for scroll offset before hit-testing
    int sy = y + scrollY;

    // Tool buttons
    int cellW = (TB_W - TB_PAD) / 3;
    for (int row=0; row<3; row++) {
        for (int col=0; col<3; col++) {
            int idx = toolGrid[row][col];
            if (idx < 0) continue;
            int bx = TB_PAD/2 + col*cellW;
            int by = toolStartY() + row*(ICON_SIZE+ICON_GAP);
            SDL_Rect btn = {bx, by, cellW-2, ICON_SIZE};
            SDL_Point pt = {x, sy};
            if (SDL_PointInRect(&pt, &btn)) {
                app->setTool(toolTypes[idx]);
                currentType = toolTypes[idx];
                return true;
            }
        }
    }

    // Slider
    int sTop = sliderSectionY(), sH = sliderSectionH();
    SDL_Rect sliderArea = {TB_PAD/2, sTop-6, TB_W-TB_PAD, sH+12};
    SDL_Point pt = {x, sy};
    if (SDL_PointInRect(&pt, &sliderArea)) {
        draggingSlider = true;
        updateSliderFromMouse(x);
        return true;
    }

    // Color wheel (uses cached colorWheelCY which already has scroll applied from draw())
    if (colorWheelR > 0) {
        float dx=x-colorWheelCX, dy=y-colorWheelCY;
        if (sqrt(dx*dx+dy*dy) <= colorWheelR+4) {
            draggingWheel = true;
            updateWheelFromMouse(x, y);
            return true;
        }
    }

    // Brightness bar (brightnessRect already has scroll applied from draw())
    SDL_Rect bExp = {brightnessRect.x-2, brightnessRect.y-4, brightnessRect.w+4, brightnessRect.h+8};
    SDL_Point bpt = {x, y};
    if (SDL_PointInRect(&bpt, &bExp)) {
        draggingBrightness = true;
        updateBrightnessFromMouse(x);
        return true;
    }

    // Custom swatches (customGridY already has scroll applied from draw())
    {
        int i = hitCustomSwatch(x, y);
        if (i >= 0) {
            if (selectedCustomSlot == i) {
                selectedCustomSlot = -1;
            } else {
                selectedCustomSlot = i;
                selectedPresetSlot = -1;
                brushColor = customColors[i];
                rgbToHsv(brushColor, hue, sat, val);
            }
            draggingSwatch    = true;
            draggingSwatchIdx = i;
            return true;
        }
    }

    // Preset swatches (presetGridY already has scroll applied from draw())
    {
        int i = hitPresetSwatch(x, y);
        if (i >= 0 && i < 27) {
            if (selectedPresetSlot == i) {
                selectedPresetSlot = -1;
            } else {
                selectedPresetSlot = i;
                selectedCustomSlot = -1;
                brushColor = PRESETS[i];
                rgbToHsv(brushColor, hue, sat, val);
            }
            draggingSwatch    = true;
            draggingSwatchIdx = i + NUM_CUSTOM;
            return true;
        }
    }

    return false; // click was in toolbar area but didn't hit any control
}

bool Toolbar::onMouseMotion(int x, int y) {
    userScrolling = false;
    if (draggingSlider)     { updateSliderFromMouse(x);    return true; }
    if (draggingWheel)      { updateWheelFromMouse(x, y);  return true; }
    if (draggingBrightness) { updateBrightnessFromMouse(x); return true; }
    if (draggingSwatch)     { return true; }
    return false;
}

void Toolbar::onMouseUp(int x, int y) {
    if (draggingSwatch && draggingSwatchIdx >= 0) {
        int i = hitCustomSwatch(x, y);
        if (i >= 0 && i != draggingSwatchIdx) {
            if (draggingSwatchIdx < NUM_CUSTOM)
                customColors[i] = customColors[draggingSwatchIdx];
            else
                customColors[i] = PRESETS[draggingSwatchIdx - NUM_CUSTOM];
            selectedCustomSlot = i;
            selectedPresetSlot = -1;
            brushColor = customColors[i];
            rgbToHsv(brushColor, hue, sat, val);
        }
    }
    draggingSwatch     = false;
    draggingSwatchIdx  = -1;
    draggingSlider     = false;
    draggingWheel      = false;
    draggingBrightness = false;
}

bool Toolbar::isDragging() const {
    return draggingWheel || draggingBrightness || draggingSlider || draggingSwatch;
}

bool Toolbar::onMouseWheel(int x, int y, float dy) {
    if (!inToolbar(x, y)) return false;

    if (!userScrolling) {
        // Gesture start — snapshot current position
        scrollBaseY    = scrollY;
        scrollRawOffset = 0.f;
        userScrolling  = true;
    }

    scrollRawOffset -= dy * 18.f;

    // Compute where scrollY should be based on raw offset from base
    float target = scrollBaseY + scrollRawOffset;

    // macOS-style resistance: displayOver = rawOver * k / (rawOver + k)
    // This approaches asymptote of k pixels no matter how much you scroll
    const float k = 60.f;
    if (target < 0) {
        float rawOver = -target;
        float displayOver = rawOver * k / (rawOver + k);
        target = -displayOver;
    } else if (target > maxScrollCache) {
        float rawOver = target - maxScrollCache;
        float displayOver = rawOver * k / (rawOver + k);
        target = maxScrollCache + displayOver;
    }

    scrollY = (int)target;
    return true;
}

bool Toolbar::tickScroll() {
    if (userScrolling) return false; // nothing to animate while user is scrolling

    // Snap back to valid range
    if (scrollY < 0) {
        scrollY += std::max(1, (int)(-scrollY * 0.18f));
        if (scrollY >= 0) scrollY = 0;
        else return true;
    } else if (scrollY > maxScrollCache) {
        scrollY += std::min(-1, (int)((maxScrollCache - scrollY) * 0.18f));
        if (scrollY <= maxScrollCache) scrollY = maxScrollCache;
        else return true;
    }
    return false;
}

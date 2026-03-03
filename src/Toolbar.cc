#define _USE_MATH_DEFINES
#include "Toolbar.h"
#include "kPen.h"
#include <cmath>
#include <algorithm>

constexpr int toolGrid[3][3] = {{0,1,2},{3,-1,4},{5,6,7}};
constexpr ToolType toolTypes[] = {
    ToolType::BRUSH, ToolType::LINE, ToolType::ERASER,
    ToolType::RECT, ToolType::CIRCLE, ToolType::SELECT,
    ToolType::FILL, ToolType::PICK
};

Toolbar::Toolbar(SDL_Renderer* renderer, kPen* app)
    : renderer(renderer), app(app)
{
    rgbToHsv(brushColor, hue, sat, val);
}

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

void Toolbar::drawIcon(int cx, int cy, ToolType t, bool active) {
    SDL_Color fg = active ? SDL_Color{255,255,255,255} : SDL_Color{160,160,170,255};
    SDL_SetRenderDrawColor(renderer, fg.r, fg.g, fg.b, 255);
    int s = ICON_SIZE/2 - 3;
    switch (t) {
        case ToolType::BRUSH: {
            if (squareBrush) {
                const int r = 4;
                SDL_Rect sq = { cx - r, cy - r, r * 2 + 1, r * 2 + 1 };
                SDL_RenderFillRect(renderer, &sq);
            } else {
                const int r = 4;
                for (int dy = -r; dy <= r; dy++) {
                    int dx = (int)std::sqrt((float)(r * r - dy * dy) + 0.5f);
                    SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
                }
            }
            break;
        }
        case ToolType::ERASER: {
            if (squareEraser) {
                const int r = 4;
                const int d = 2;
                int x0 = cx - r, y0 = cy - r, x1 = cx + r, y1 = cy + r;
                SDL_RenderDrawLine(renderer, x0,     y0, x0 + d, y0);
                SDL_RenderDrawLine(renderer, x0,     y0, x0,     y0 + d);
                SDL_RenderDrawLine(renderer, x1 - d, y0, x1,     y0);
                SDL_RenderDrawLine(renderer, x1,     y0, x1,     y0 + d);
                SDL_RenderDrawLine(renderer, x0,     y1, x0 + d, y1);
                SDL_RenderDrawLine(renderer, x0,     y1 - d, x0, y1);
                SDL_RenderDrawLine(renderer, x1 - d, y1, x1,     y1);
                SDL_RenderDrawLine(renderer, x1,     y1 - d, x1,  y1);
            } else {
                const int r = 4;
                for (int deg = 0; deg < 360; deg++) {
                    if ((deg / 45) % 2 == 0) {
                        float a = (22.5 + deg) * (float)M_PI / 180.f;
                        SDL_RenderDrawPoint(renderer,
                            cx + (int)std::round(r * std::cos(a)),
                            cy + (int)std::round(r * std::sin(a)));
                    }
                }
            }
            break;
        }
        case ToolType::LINE: {
            for (int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i,   cy-i);
            for (int i=-s; i<=s; i++) SDL_RenderDrawPoint(renderer, cx+i+1, cy-i);
            break;
        }
        case ToolType::RECT: {
            SDL_Rect r = {cx-s, cy-s, s*2, s*2};
            if (fillRect) SDL_RenderFillRect(renderer, &r);
            else          SDL_RenderDrawRect(renderer, &r);
            break;
        }
        case ToolType::CIRCLE: {
            if (fillCircle) {
                for (int h = -s; h <= s; h++) {
                    int half = (int)std::sqrt((float)(s*s - h*h));
                    SDL_RenderDrawLine(renderer, cx-half, cy+h, cx+half, cy+h);
                }
            } else {
                for (int deg=0; deg<360; deg+=5) {
                    float a = deg * M_PI / 180.f;
                    SDL_RenderDrawPoint(renderer, cx+(int)(s*cos(a)), cy+(int)(s*sin(a)));
                }
            }
            break;
        }
        case ToolType::SELECT: {
            if (lassoSelect) {
                // Lasso icon: same rect as rect select but with flattened (chamfered) top-left corner
                const int d = 3;
                const int ch = s;  // chamfer size for flattened top-left (full side = more flat)
                SDL_Point lasso[] = {
                    {cx - s + ch, cy - s},   // top edge start (after chamfer)
                    {cx + s,     cy - s},   // top-right
                    {cx + s,     cy + s},   // bottom-right
                    {cx - s,     cy + s},   // bottom-left
                    {cx - s,     cy - s + ch},   // left edge end (before chamfer)
                    {cx - s + ch, cy - s},   // chamfer back to top
                };
                const int n = (int)(sizeof(lasso) / sizeof(lasso[0]));
                for (int i = 0; i < n; i++) {
                    int x0 = lasso[i].x, y0 = lasso[i].y;
                    int x1 = lasso[(i + 1) % n].x, y1 = lasso[(i + 1) % n].y;
                    double dx = (double)(x1 - x0), dy = (double)(y1 - y0);
                    double len = std::sqrt(dx * dx + dy * dy);
                    if (len < 0.5) continue;
                    for (int k = 0; (k * 2 + 1) * d <= len + 0.5; k++) {
                        double t0 = (k * 2 * d) / len;
                        double t1 = std::min((double)((k * 2 + 1) * d) / len, 1.0);
                        int px0 = (int)(x0 + dx * t0 + 0.5);
                        int py0 = (int)(y0 + dy * t0 + 0.5);
                        int px1 = (int)(x0 + dx * t1 + 0.5);
                        int py1 = (int)(y0 + dy * t1 + 0.5);
                        SDL_RenderDrawLine(renderer, px0, py0, px1, py1);
                    }
                }
            } else {
                int d = 3;
                for (int i=0; i<s*2; i+=d*2) {
                    SDL_RenderDrawLine(renderer, cx-s+i, cy-s, cx-s+std::min(i+d,s*2), cy-s);
                    SDL_RenderDrawLine(renderer, cx-s+i, cy+s, cx-s+std::min(i+d,s*2), cy+s);
                }
                for (int i=0; i<s*2; i+=d*2) {
                    SDL_RenderDrawLine(renderer, cx-s, cy-s+i, cx-s, cy-s+std::min(i+d,s*2));
                    SDL_RenderDrawLine(renderer, cx+s, cy-s+i, cx+s, cy-s+std::min(i+d,s*2));
                }
            }
            break;
        }
        case ToolType::FILL: {
            int ox = cx + 2;   // shift diamond center slightly right (mirror of old cx-2)
            int oy = cy + 2;
            s -= 2;

            SDL_RenderDrawLine(renderer, ox,    oy-s, ox+s, oy  );
            SDL_RenderDrawLine(renderer, ox+s,  oy,   ox,   oy+s);
            SDL_RenderDrawLine(renderer, ox,    oy+s, ox-s, oy  );
            SDL_RenderDrawLine(renderer, ox-s,  oy,   ox,   oy-s);

            for (int row = 2; row <= s; row++) {
                int halfW = s - row;
                SDL_RenderDrawLine(renderer, ox - halfW, oy + row, ox + halfW, oy + row);
            }

            int hLen = 3;
            SDL_RenderDrawLine(renderer, ox,      oy-s,      ox+hLen,   oy-s-hLen);
            SDL_RenderDrawLine(renderer, ox+1,    oy-s,      ox+hLen+1, oy-s-hLen);

            int dx = ox - s - 2, dy = cy + 2;
            SDL_RenderDrawPoint(renderer, dx,   dy  );
            SDL_RenderDrawLine(renderer, dx-1, dy+1, dx+1, dy+1);
            SDL_RenderDrawLine(renderer, dx-2, dy+2, dx+2, dy+2);
            SDL_RenderDrawLine(renderer, dx-2, dy+3, dx+2, dy+3);
            SDL_RenderDrawLine(renderer, dx-1, dy+4, dx+1, dy+4);
            break;
        }
        case ToolType::PICK: {
            int capX = cx + 1, capY = cy - 8;
            SDL_Rect cap = { capX, capY, 5, 3 };
            SDL_RenderFillRect(renderer, &cap);

            int ax = cx + 3, ay = cy - 5;
            for (int i = 0; i < 9; i++) {
                SDL_RenderDrawPoint(renderer, ax - i,     ay + i);
                SDL_RenderDrawPoint(renderer, ax - i - 1, ay + i);
            }

            int nx = ax - 9, ny = ay + 9;
            SDL_RenderDrawPoint(renderer, nx,     ny);
            SDL_RenderDrawPoint(renderer, nx - 1, ny);
            SDL_RenderDrawPoint(renderer, nx - 1, ny + 1);
            SDL_RenderDrawPoint(renderer, nx - 2, ny + 2);
            break;
        }
        case ToolType::RESIZE:
        case ToolType::HAND:
            break;  // no toolbar icon (resize is transient; hand is space/H only)
        }
}

void Toolbar::draw(bool handActive, int winW, int winH) {
    SDL_Rect panel = {0, 0, TB_W, winH};
    SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, TB_W-1, 0, TB_W-1, winH);

    const int S = scrollY;
    const int cellW = toolCellW();
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
            bool active = !handActive && ((currentType == toolTypes[idx]) ||
                          (currentType == ToolType::RESIZE && toolTypes[idx] == ToolType::SELECT));
            SDL_SetRenderDrawColor(renderer, active ? 70 : 45, active ? 130 : 45, active ? 220 : 52, 255);
            SDL_RenderFillRect(renderer, &btn);
            SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
            SDL_RenderDrawRect(renderer, &btn);
            drawIcon(bx + (cellW-2)/2, by + ICON_SIZE/2, toolTypes[idx], active);
        }
    }
    int brushRowY = ty + 3*(ICON_SIZE+ICON_GAP) + 2;

    SDL_Rect bsField = { TB_PAD, brushRowY, BS_FIELD_W, BS_ROW1_H };
    brushSizeFieldRect = bsField;
    bool bsFocused = brushSizeFocused;
    SDL_SetRenderDrawColor(renderer, bsFocused ? 45 : 38, bsFocused ? 45 : 38, bsFocused ? 55 : 45, 255);
    SDL_RenderFillRect(renderer, &bsField);
    SDL_SetRenderDrawColor(renderer, bsFocused ? 70 : 55, bsFocused ? 130 : 55, bsFocused ? 220 : 62, 255);
    SDL_RenderDrawRect(renderer, &bsField);
    SDL_SetRenderDrawColor(renderer, 220, 220, 230, 255);
    int bsLen = brushSizeBufLen();
    int textW = bsLen * 8 - 2;
    int textX = bsField.x + (bsField.w - textW) / 2;
    int textY = bsField.y + (BS_ROW1_H - 10) / 2;
    drawDigitString(textX, textY, brushSizeBuf, bsLen);
    if (bsFocused) {
        int curX = textX + bsLen * 8;
        SDL_SetRenderDrawColor(renderer, 200, 200, 220, 255);
        SDL_RenderDrawLine(renderer, curX, bsField.y + 2, curX, bsField.y + BS_ROW1_H - 3);
    }

    int previewAreaX = TB_PAD + BS_FIELD_W + BS_GAP;
    int previewAreaW = contentWidth() - BS_FIELD_W - BS_GAP;
    int previewCX = previewAreaX + previewAreaW / 2;
    int previewCY = brushRowY + BS_ROW1_H / 2;
    int maxR = BS_ROW1_H / 2 - 1;
    int dotR = std::max(1, (int)((std::min(brushSize, 25) / 25.f) * maxR + 0.5f));
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    bool previewSquare = (currentType == ToolType::BRUSH   && squareBrush) ||
                         (currentType == ToolType::ERASER  && squareEraser);
    if (previewSquare) {
        SDL_Rect sq = { previewCX - dotR, previewCY - dotR, dotR * 2 + 1, dotR * 2 + 1 };
        SDL_RenderFillRect(renderer, &sq);
    } else {
        for (int py = -dotR; py <= dotR; py++)
            for (int px = -dotR; px <= dotR; px++)
                if (px*px + py*py <= dotR*dotR)
                    SDL_RenderDrawPoint(renderer, previewCX+px, previewCY+py);
    }

    // Row 2: full-width slider
    int sliderY = brushRowY + BS_ROW1_H + BS_ROW_GAP;
    int sX = TB_PAD, sW = contentWidth(), sH = BS_ROW2_H;
    int trackY = sliderY + sH/2;
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, sX, trackY,   sX+sW, trackY);
    SDL_RenderDrawLine(renderer, sX, trackY+1, sX+sW, trackY+1);
    int thumbX = sX + (int)((std::min(brushSize, 25)-1)/24.f * sW);
    SDL_Rect thumb = {thumbX-5, sliderY, 10, sH};
    SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
    SDL_RenderFillRect(renderer, &thumb);
    SDL_SetRenderDrawColor(renderer, 120, 120, 130, 255);
    SDL_RenderDrawRect(renderer, &thumb);

    int wTop = brushRowY + BS_ROW1_H + BS_ROW_GAP + BS_ROW2_H + 8;
    int availH = winH - wTop - TB_PAD;
    int wheelDiam = std::min(contentWidth(), availH - 20);
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

    int bTop = wTop + wheelDiam + 6, bH = 12;
    int bX = TB_PAD, bW = contentWidth();
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

    int csy = bTop + bH + 7;
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, TB_PAD, csy, TB_PAD + contentWidth(), csy);
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

    int psy = csy + 3*stride + 7;
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, TB_PAD, psy, TB_PAD + contentWidth(), psy);
    psy += 4;
    presetGridY = psy;
    for (int i=0; i<27; i++) {
        int col=i%3, row=i/3;
        int sx=TB_PAD+col*stride, sy=psy+row*stride;
        SDL_Rect r = {sx, sy, sz, sz};
        if (i == TRANSPARENT_PRESET_IDX) {
            // White background with a diagonal red line — "no color / transparent"
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderFillRect(renderer, &r);
            SDL_SetRenderDrawColor(renderer, 200, 30, 30, 255);
            SDL_RenderDrawLine(renderer, sx, sy+sz-2, sx+sz-2, sy);
            SDL_RenderDrawLine(renderer, sx, sy+sz-1, sx+sz-1, sy);
            SDL_RenderDrawLine(renderer, sx+1, sy+sz-1, sx+sz-1, sy+1);
        } else {
            SDL_SetRenderDrawColor(renderer, PRESETS[i].r, PRESETS[i].g, PRESETS[i].b, 255);
            SDL_RenderFillRect(renderer, &r);
        }
        if (i == selectedPresetSlot) {
            SDL_Rect outer={sx-2,sy-2,sz+4,sz+4}; SDL_SetRenderDrawColor(renderer,255,255,255,255); SDL_RenderDrawRect(renderer,&outer);
            SDL_Rect inner={sx-1,sy-1,sz+2,sz+2}; SDL_SetRenderDrawColor(renderer,0,0,0,255);       SDL_RenderDrawRect(renderer,&inner);
        } else {
            SDL_SetRenderDrawColor(renderer, 70, 70, 80, 255);
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    int rpTop = psy + 9*stride + 8;
    drawResizePanel(rpTop);       // rpTop is already in screen space (psy has -S applied)
    resizePanelY = rpTop;         // store screen-space Y for hit-testing (matches swatch pattern)

    int totalContentH = (rpTop + S) + 90;
    maxScrollCache = std::max(0, totalContentH - winH);

    if (maxScrollCache > 0) {
        int sbW = 3, sbX = TB_W - sbW - 1;  // scrollbar within panel
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

// --- Mouse update helpers ---

void Toolbar::updateSliderFromMouse(int x) {
    int sX = TB_PAD, sW = contentWidth();
    int clamped = std::max(sX, std::min(sX+sW, x));
    brushSize = 1 + (int)((float)(clamped-sX) / sW * 24.f + 0.5f);
    brushSize = std::max(1, std::min(25, brushSize));
    snprintf(brushSizeBuf, sizeof(brushSizeBuf), "%d", brushSize);
}

void Toolbar::updateWheelFromMouse(int x, int y) {
    float dx=x-colorWheelCX, dy=y-colorWheelCY;
    float dist = sqrt(dx*dx + dy*dy);
    hue = fmod(atan2(dy,dx)/(2*M_PI)+1.f, 1.f);
    sat = std::min(1.f, dist / colorWheelR);
    brushColor = hsvToRgb(hue, sat, val);
    selectedPresetSlot = -1;
    if (selectedCustomSlot >= 0) customColors[selectedCustomSlot] = brushColor;
    if (onColorChanged) onColorChanged(brushColor);
}

void Toolbar::updateBrightnessFromMouse(int x) {
    float t = (float)(x - brightnessRect.x) / brightnessRect.w;
    val = std::max(0.f, std::min(1.f, t));
    brushColor = hsvToRgb(hue, sat, val);
    selectedPresetSlot = -1;
    if (selectedCustomSlot >= 0) customColors[selectedCustomSlot] = brushColor;
    if (onColorChanged) onColorChanged(brushColor);
}

// --- Event handling ---

void Toolbar::defocusResize(bool commit) {
    if (resizeFocus == ResizeFocus::NONE) return;
    resizeFocus = ResizeFocus::NONE;
    SDL_StopTextInput();
    if (commit) {
        int w = 0; for (int i = 0; i < resizeWBufLen(); i++) w = w * 10 + (resizeWBuf[i] - '0');
        int h = 0; for (int i = 0; i < resizeHBufLen(); i++) h = h * 10 + (resizeHBuf[i] - '0');
        if (w > 0 && h > 0 && (w != resizeLockW || h != resizeLockH))
            commitResize();
        else {
            snprintf(resizeWBuf, sizeof(resizeWBuf), "%d", resizeLockW);
            snprintf(resizeHBuf, sizeof(resizeHBuf), "%d", resizeLockH);
        }
    } else {
        snprintf(resizeWBuf, sizeof(resizeWBuf), "%d", resizeLockW);
        snprintf(resizeHBuf, sizeof(resizeHBuf), "%d", resizeLockH);
    }
}

void Toolbar::notifyClickOutside() {
    defocusResize(false);  // clicking outside the toolbar always cancels/reverts
    // Also dismiss brush size input
    if (brushSizeFocused) {
        int v = 0;
        for (int i = 0; i < brushSizeBufLen(); i++) v = v * 10 + (brushSizeBuf[i] - '0');
        brushSize = std::max(1, std::min(99, v > 0 ? v : brushSize));
        snprintf(brushSizeBuf, sizeof(brushSizeBuf), "%d", brushSize);
        brushSizeFocused = false;
        SDL_StopTextInput();
    }
}

bool Toolbar::isInteractive(int x, int y) const {
    if (!inToolbar(x, y)) return false;
    int sy = y + scrollY;

    int cellW = toolCellW();
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int bx = TB_PAD/2 + col*cellW;
            int by = toolStartY() + row*(ICON_SIZE+ICON_GAP);
            SDL_Rect btn = {bx, by, cellW-2, ICON_SIZE};
            SDL_Point pt = {x, sy};
            if (SDL_PointInRect(&pt, &btn)) return true;
        }
    }

    SDL_Rect bsExp = { brushSizeFieldRect.x - 2, brushSizeFieldRect.y - 4,
                       brushSizeFieldRect.w + 4, brushSizeFieldRect.h + 8 };
    SDL_Point bsPt = {x, y};
    if (SDL_PointInRect(&bsPt, &bsExp)) return true;

    int sTop = sliderSectionY();
    SDL_Rect sliderArea = { 0, sTop - 4, TB_W, sliderSectionH() + 8 };
    SDL_Point sPt = {x, sy};
    if (SDL_PointInRect(&sPt, &sliderArea)) return true;

    // Color wheel
    float dx = x - colorWheelCX, dy2 = y - colorWheelCY;
    if (dx*dx + dy2*dy2 <= (float)colorWheelR * colorWheelR) return true;

    // Brightness bar
    SDL_Rect bExp = {brightnessRect.x-2, brightnessRect.y-4,
                     brightnessRect.w+4, brightnessRect.h+8};
    if (SDL_PointInRect(&bsPt, &bExp)) return true;

    if (hitCustomSwatch(x, y) >= 0) return true;
    if (hitPresetSwatch(x, y) >= 0) return true;

    {
        int panelY = resizePanelY;
        int py     = panelY + 12;
        int fieldX = TB_PAD + 10;
        int fieldW = contentWidth() - 10;
        SDL_Rect wField   = { fieldX, py,           fieldW, 16 };
        SDL_Rect hField   = { fieldX, py + 16 + 4,  fieldW, 16 };
        int btnY  = py + 16 + 4 + 16 + 6;
        SDL_Rect lockBtn  = { TB_PAD, btnY, contentWidth(), 14 };
        SDL_Rect scaleBtn = { TB_PAD, btnY + 14 + 4, contentWidth(), 14 };
        SDL_Point rpt = {x, y};
        if (SDL_PointInRect(&rpt, &wField)   ||
            SDL_PointInRect(&rpt, &hField)   ||
            SDL_PointInRect(&rpt, &lockBtn)  ||
            SDL_PointInRect(&rpt, &scaleBtn)) return true;
    }

    return false;
}

bool Toolbar::onMouseDown(int x, int y) {
    if (!inToolbar(x, y)) return false;
    userScrolling = false;

    int sy = y + scrollY;

    if (brushSizeFocused) {
        SDL_Rect bsExp = { brushSizeFieldRect.x - 2, brushSizeFieldRect.y - 4,
                           brushSizeFieldRect.w + 4, brushSizeFieldRect.h + 8 };
        SDL_Point bsPt = { x, y };
        if (!SDL_PointInRect(&bsPt, &bsExp)) {
            int v = 0;
            for (int i = 0; i < brushSizeBufLen(); i++) v = v * 10 + (brushSizeBuf[i] - '0');
            brushSize = std::max(1, std::min(99, v > 0 ? v : brushSize));
            snprintf(brushSizeBuf, sizeof(brushSizeBuf), "%d", brushSize);
            brushSizeFocused = false;
            SDL_StopTextInput();
        }
    }

    if (resizeFocus != ResizeFocus::NONE) {
        int panelY = resizePanelY;    // screen space
        int py     = panelY + 12;
        // Include W field, H field, and lock/scale buttons so clicking them does not apply dimensions
        static const int RP_FH = 16, RP_BTN_H_RESIZE = 20;
        int panelH = RP_FH + 4 + RP_FH + 6 + RP_BTN_H_RESIZE;
        SDL_Rect resizePanelArea = { TB_PAD, py, contentWidth(), panelH };
        SDL_Point pt = { x, y };
        if (!SDL_PointInRect(&pt, &resizePanelArea))
            defocusResize(true);
    }

    int cellW = toolCellW();
    for (int row=0; row<3; row++) {
        for (int col=0; col<3; col++) {
            int idx = toolGrid[row][col];
            if (idx < 0) continue;
            int bx = TB_PAD/2 + col*cellW;
            int by = toolStartY() + row*(ICON_SIZE+ICON_GAP);
            SDL_Rect btn = {bx, by, cellW-2, ICON_SIZE};
            SDL_Point pt = {x, sy};
            if (SDL_PointInRect(&pt, &btn)) {
                ToolType t = toolTypes[idx];
                if (t == currentType && t == ToolType::RECT)
                    fillRect = !fillRect;
                else if (t == currentType && t == ToolType::CIRCLE)
                    fillCircle = !fillCircle;
                else if (t == currentType && t == ToolType::BRUSH)
                    squareBrush = !squareBrush;
                else if (t == currentType && t == ToolType::ERASER)
                    squareEraser = !squareEraser;
                else if (t == currentType && t == ToolType::SELECT)
                    lassoSelect = !lassoSelect;
                app->setTool(t);
                currentType = t;
                return true;
            }
        }
    }

    // Slider (row 2, full-width) + brush size field (row 1, left)
    {
        int sTop = sliderSectionY();
        int sH = sliderSectionH();

        SDL_Rect sliderArea = { 0, sTop - 4, TB_W, sH + 8 };
        SDL_Point pt2 = {x, sy};
        
        if (SDL_PointInRect(&pt2, &sliderArea)) {
            if (brushSizeFocused) { brushSizeFocused = false; SDL_StopTextInput(); }
            draggingSlider = true;
            updateSliderFromMouse(x);
            return true;
        }
        // Brush size field (row 1, screen-space rect cached by draw())
        SDL_Rect bsExp = { brushSizeFieldRect.x - 2, brushSizeFieldRect.y - 4,
                           brushSizeFieldRect.w + 4, brushSizeFieldRect.h + 8 };
        if (SDL_PointInRect(&pt2, &bsExp)) {
            if (!brushSizeFocused) {
                brushSizeFocused = true;
                brushSizeBuf[0] = 0;
                SDL_StartTextInput();
            }
            return true;
        }
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
                if (onColorChanged) onColorChanged(brushColor);
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
                if (onColorChanged) onColorChanged(brushColor);
            }
            if (i != 0) {
                draggingSwatch    = true;
                draggingSwatchIdx = i + NUM_CUSTOM;
            }
            return true;
        }
    }

    if (hitResizePanel(x, y, true)) return true;

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

static const uint8_t DIGIT_FONT[13][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b011, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111},
    {0b111, 0b101, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b010, 0b101, 0b101},
    {0b101, 0b101, 0b101, 0b111, 0b101},
    {0b101, 0b101, 0b111, 0b101, 0b101},
};

static void drawGlyph(SDL_Renderer* r, int x, int y, int glyphIdx, int scale = 1) {
    const uint8_t* rows = DIGIT_FONT[glyphIdx];
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (rows[row] & (1 << (2 - col))) {
                SDL_Rect px = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(r, &px);
            }
        }
    }
}

void Toolbar::drawDigitString(int x, int y, const char* s, int len) const {
    int cx = x;
    for (int i = 0; i < len; i++) {
        int gi = (s[i] >= '0' && s[i] <= '9') ? (s[i] - '0') : 10;
        drawGlyph(renderer, cx, y, gi, 2);
        cx += 8;  // 3*2 + 2 px kerning
    }
}

static const int RP_FIELD_H  = 16;
static const int RP_BTN_H    = 20;
static const int RP_LABEL_H  = 8;

void Toolbar::drawResizePanel(int panelY) {
    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderDrawLine(renderer, TB_PAD, panelY + 4, TB_PAD + contentWidth(), panelY + 4);

    int y = panelY + 12;
    int fieldX = TB_PAD;
    int fieldW = contentWidth();
    int labelX = fieldX + 2;
    SDL_SetRenderDrawColor(renderer, 140, 140, 155, 255);
    drawGlyph(renderer, labelX, y + (RP_FIELD_H - 10) / 2, 11, 2);
    SDL_Rect wField = { fieldX + 10, y, fieldW - 10, RP_FIELD_H };
    bool wFocused = (resizeFocus == ResizeFocus::W);
    SDL_SetRenderDrawColor(renderer, wFocused ? 45 : 38, wFocused ? 45 : 38, wFocused ? 55 : 45, 255);
    SDL_RenderFillRect(renderer, &wField);
    SDL_SetRenderDrawColor(renderer, wFocused ? 70 : 55, wFocused ? 130 : 55, wFocused ? 220 : 62, 255);
    SDL_RenderDrawRect(renderer, &wField);
    SDL_SetRenderDrawColor(renderer, 220, 220, 230, 255);
    drawDigitString(wField.x + 3, wField.y + (RP_FIELD_H - 10) / 2, resizeWBuf, resizeWBufLen());
    if (wFocused) {
        int cx2 = wField.x + 3 + resizeWBufLen() * 8;
        SDL_SetRenderDrawColor(renderer, 200, 200, 220, 255);
        SDL_RenderDrawLine(renderer, cx2, wField.y + 2, cx2, wField.y + RP_FIELD_H - 3);
    }

    y += RP_FIELD_H + 4;
    SDL_SetRenderDrawColor(renderer, 140, 140, 155, 255);
    drawGlyph(renderer, labelX, y + (RP_FIELD_H - 10) / 2, 12, 2);

    SDL_Rect hField = { fieldX + 10, y, fieldW - 10, RP_FIELD_H };
    bool hFocused = (resizeFocus == ResizeFocus::H);
    SDL_SetRenderDrawColor(renderer, hFocused ? 45 : 38, hFocused ? 45 : 38, hFocused ? 55 : 45, 255);
    SDL_RenderFillRect(renderer, &hField);
    SDL_SetRenderDrawColor(renderer, hFocused ? 70 : 55, hFocused ? 130 : 55, hFocused ? 220 : 62, 255);
    SDL_RenderDrawRect(renderer, &hField);

    SDL_SetRenderDrawColor(renderer, 220, 220, 230, 255);
    drawDigitString(hField.x + 3, hField.y + (RP_FIELD_H - 10) / 2, resizeHBuf, resizeHBufLen());

    if (hFocused) {
        int cx2 = hField.x + 3 + resizeHBufLen() * 8;
        SDL_SetRenderDrawColor(renderer, 200, 200, 220, 255);
        SDL_RenderDrawLine(renderer, cx2, hField.y + 2, cx2, hField.y + RP_FIELD_H - 3);
    }

    y += RP_FIELD_H + 6;
    int halfW = (fieldW - 2) / 2;
    SDL_Rect lockBtn = { fieldX, y, halfW, RP_BTN_H };
    bool la = resizeLockAspect;
    SDL_SetRenderDrawColor(renderer, la ? 70 : 45, la ? 130 : 45, la ? 220 : 52, 255);
    SDL_RenderFillRect(renderer, &lockBtn);
    SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
    SDL_RenderDrawRect(renderer, &lockBtn);
    SDL_SetRenderDrawColor(renderer, la ? 255 : 160, la ? 255 : 160, la ? 255 : 170, 255);
    {
        int iconW = 8, iconH = 11;
        int ix = lockBtn.x + (lockBtn.w - iconW) / 2;
        int iy = lockBtn.y + (lockBtn.h - iconH) / 2;
        SDL_Rect lockBody = { ix, iy + 5, iconW, 6 };
        SDL_RenderFillRect(renderer, &lockBody);
        if (la) {
            SDL_RenderDrawLine(renderer, ix + 1, iy + 5, ix + 1, iy + 2);
            SDL_RenderDrawLine(renderer, ix + 1, iy + 2, ix + 6, iy + 2);
            SDL_RenderDrawLine(renderer, ix + 6, iy + 2, ix + 6, iy + 5);
        } else {
            SDL_RenderDrawLine(renderer, ix + 1, iy + 3, ix + 1, iy + 0);
            SDL_RenderDrawLine(renderer, ix + 1, iy + 0, ix + 6, iy + 0);
        }
    }
    SDL_Rect scaleBtn = { fieldX + halfW + 2, y, halfW, RP_BTN_H };
    bool sc = resizeScaleMode;
    SDL_SetRenderDrawColor(renderer, sc ? 70 : 45, sc ? 130 : 45, sc ? 220 : 52, 255);
    SDL_RenderFillRect(renderer, &scaleBtn);
    SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
    SDL_RenderDrawRect(renderer, &scaleBtn);

    // Scale icon: two diagonal arrows crossing in an X (↖↘ and ↗↙)
    SDL_SetRenderDrawColor(renderer, sc ? 255 : 160, sc ? 255 : 160, sc ? 255 : 170, 255);
    {
        int cx2 = scaleBtn.x + scaleBtn.w / 2;
        int cy2 = scaleBtn.y + scaleBtn.h / 2;
        int ar = 4; // arrow arm radius

        // NW→SE arrow
        SDL_RenderDrawLine(renderer, cx2 - ar, cy2 - ar, cx2 + ar, cy2 + ar);
        // NW arrowhead
        SDL_RenderDrawLine(renderer, cx2 - ar, cy2 - ar, cx2 - ar + 2, cy2 - ar);
        SDL_RenderDrawLine(renderer, cx2 - ar, cy2 - ar, cx2 - ar, cy2 - ar + 2);
        // SE arrowhead
        SDL_RenderDrawLine(renderer, cx2 + ar, cy2 + ar, cx2 + ar - 2, cy2 + ar);
        SDL_RenderDrawLine(renderer, cx2 + ar, cy2 + ar, cx2 + ar, cy2 + ar - 2);

        // NE→SW arrow
        SDL_RenderDrawLine(renderer, cx2 + ar, cy2 - ar, cx2 - ar, cy2 + ar);
        // NE arrowhead
        SDL_RenderDrawLine(renderer, cx2 + ar, cy2 - ar, cx2 + ar - 2, cy2 - ar);
        SDL_RenderDrawLine(renderer, cx2 + ar, cy2 - ar, cx2 + ar, cy2 - ar + 2);
        // SW arrowhead
        SDL_RenderDrawLine(renderer, cx2 - ar, cy2 + ar, cx2 - ar + 2, cy2 + ar);
        SDL_RenderDrawLine(renderer, cx2 - ar, cy2 + ar, cx2 - ar, cy2 + ar - 2);
    }
}

// hitResizePanel: returns true if (x, y) hit any resize panel control.
// y is raw screen-space (NOT scroll-adjusted), matching how swatch hit-tests work —
// resizePanelY is cached in screen space by draw().
bool Toolbar::hitResizePanel(int x, int y, bool isDown) {
    // resizePanelY is in screen space. y is also screen space.
    // Mirror the exact layout arithmetic from drawResizePanel.
    int panelY = resizePanelY;
    int ry     = panelY + 12;   // top of W field

    int fieldX = TB_PAD;
    int fieldW = contentWidth();
    int halfW2 = (fieldW - 2) / 2;

    // W field: { fieldX+10, ry, fieldW-10, RP_FIELD_H }
    SDL_Rect wField = { fieldX + 10, ry, fieldW - 10, RP_FIELD_H };

    // H field: ry advances by RP_FIELD_H + 4
    int hY = ry + RP_FIELD_H + 4;
    SDL_Rect hField = { fieldX + 10, hY, fieldW - 10, RP_FIELD_H };

    // Buttons: y advances by another RP_FIELD_H + 6
    int btnY = hY + RP_FIELD_H + 6;
    SDL_Rect lockBtn  = { fieldX,              btnY, halfW2, RP_BTN_H };
    SDL_Rect scaleBtn = { fieldX + halfW2 + 2, btnY, halfW2, RP_BTN_H };

    SDL_Point pt = { x, y };

    if (SDL_PointInRect(&pt, &wField)) {
        if (isDown) {
            resizeFocus = ResizeFocus::W;
            SDL_StartTextInput();
        }
        return true;
    }
    if (SDL_PointInRect(&pt, &hField)) {
        if (isDown) {
            resizeFocus = ResizeFocus::H;
            SDL_StartTextInput();
        }
        return true;
    }
    if (SDL_PointInRect(&pt, &lockBtn)) {
        if (isDown) {
            if (resizeFocus == ResizeFocus::W || resizeFocus == ResizeFocus::H)
                applyAspectFromFocusedField();
            resizeLockAspect = !resizeLockAspect;
        }
        return true;
    }
    if (SDL_PointInRect(&pt, &scaleBtn)) {
        if (isDown) resizeScaleMode = !resizeScaleMode;
        return true;
    }
    // All named controls handled above. Return false so onMouseDown's defocusResize
    // (already called before hitResizePanel) took care of unfocusing the text fields.
    return false;
}

// Use the focused dimension field and set the other from aspect ratio (resizeLockW/H), with CANVAS_MAX.
void Toolbar::applyAspectFromFocusedField() {
    static const int CANVAS_MAX = 16384;
    if (resizeLockW <= 0 || resizeLockH <= 0) return;
    auto parseBuf = [](const char* buf, int len) {
        int v = 0; for (int i = 0; i < len; i++) v = v * 10 + (buf[i] - '0'); return v;
    };
    auto writeBuf = [](char* buf, int v) { snprintf(buf, 7, "%d", v); };

    if (resizeFocus == ResizeFocus::W) {
        int w = parseBuf(resizeWBuf, resizeWBufLen());
        if (w <= 0) return;
        if (w > CANVAS_MAX) { w = CANVAS_MAX; writeBuf(resizeWBuf, w); }
        int newH = std::max(1, (int)std::round((float)w * resizeLockH / resizeLockW));
        if (newH > CANVAS_MAX) {
            newH = CANVAS_MAX;
            w = std::max(1, (int)std::round((float)CANVAS_MAX * resizeLockW / resizeLockH));
            writeBuf(resizeWBuf, w);
        }
        writeBuf(resizeHBuf, newH);
    } else if (resizeFocus == ResizeFocus::H) {
        int h = parseBuf(resizeHBuf, resizeHBufLen());
        if (h <= 0) return;
        if (h > CANVAS_MAX) { h = CANVAS_MAX; writeBuf(resizeHBuf, h); }
        int newW = std::max(1, (int)std::round((float)h * resizeLockW / resizeLockH));
        if (newW > CANVAS_MAX) {
            newW = CANVAS_MAX;
            h = std::max(1, (int)std::round((float)CANVAS_MAX * resizeLockH / resizeLockW));
            writeBuf(resizeHBuf, h);
        }
        writeBuf(resizeWBuf, newW);
    }
}

// Apply aspect-ratio lock: after the "source" field changes, update the linked one.
// srcIsW = true means W was just edited, so we update H to match.
void Toolbar::applyAspectLock(bool srcIsW) {
    if (!resizeLockAspect) return;
    int w = 0; for (int i = 0; i < resizeWBufLen(); i++) w = w * 10 + (resizeWBuf[i] - '0');
    int h = 0; for (int i = 0; i < resizeHBufLen(); i++) h = h * 10 + (resizeHBuf[i] - '0');
    if (resizeLockW <= 0 || resizeLockH <= 0) return;
    if (srcIsW && w > 0) {
        int newH = std::max(1, (int)std::round((float)w * resizeLockH / resizeLockW));
        snprintf(resizeHBuf, sizeof(resizeHBuf), "%d", newH);
    } else if (!srcIsW && h > 0) {
        int newW = std::max(1, (int)std::round((float)h * resizeLockW / resizeLockH));
        snprintf(resizeWBuf, sizeof(resizeWBuf), "%d", newW);
    }
}

// Clamp the just-edited dimension to 16384, and if aspect lock is on, also
// ensure the linked dimension stays within 16384 (back-calculating the source cap).
static const int CANVAS_MAX = 16384;

void Toolbar::clampResizeInput(bool srcIsW) {
    auto parseBuf = [](const char* buf, int len) {
        int v = 0; for (int i = 0; i < len; i++) v = v * 10 + (buf[i] - '0'); return v;
    };
    auto writeBuf = [](char* buf, int v) { snprintf(buf, 7, "%d", v); };

    int w = parseBuf(resizeWBuf, resizeWBufLen());
    int h = parseBuf(resizeHBuf, resizeHBufLen());

    if (srcIsW) {
        if (w > CANVAS_MAX) { w = CANVAS_MAX; writeBuf(resizeWBuf, w); }
        if (resizeLockAspect && resizeLockW > 0) {
            int linkedH = (int)std::round((float)w * resizeLockH / resizeLockW);
            if (linkedH > CANVAS_MAX) {
                int cappedW = std::max(1, (int)std::floor((float)CANVAS_MAX * resizeLockW / resizeLockH));
                if (cappedW < w) { w = cappedW; writeBuf(resizeWBuf, w); }
            }
        }
    } else {
        if (h > CANVAS_MAX) { h = CANVAS_MAX; writeBuf(resizeHBuf, h); }
        if (resizeLockAspect && resizeLockH > 0) {
            int linkedW = (int)std::round((float)h * resizeLockW / resizeLockH);
            if (linkedW > CANVAS_MAX) {
                int cappedH = std::max(1, (int)std::floor((float)CANVAS_MAX * resizeLockH / resizeLockW));
                if (cappedH < h) { h = cappedH; writeBuf(resizeHBuf, h); }
            }
        }
    }
}

bool Toolbar::onTextInput(const char* text) {
    // Brush size field takes priority
    if (brushSizeFocused) {
        for (const char* c = text; *c; c++) {
            int len = brushSizeBufLen();
            if (*c >= '0' && *c <= '9' && len < 2) {
                brushSizeBuf[len] = *c;
                brushSizeBuf[len + 1] = 0;
            }
        }
        return true;
    }
    if (resizeFocus == ResizeFocus::NONE) return false;
    char* buf = (resizeFocus == ResizeFocus::W) ? resizeWBuf : resizeHBuf;
    for (const char* c = text; *c; c++) {
        int len = (int)std::strlen(buf);
        if (*c >= '0' && *c <= '9' && len < 6) {
            buf[len] = *c;
            buf[len + 1] = 0;
        }
    }
    clampResizeInput(resizeFocus == ResizeFocus::W);
    applyAspectLock(resizeFocus == ResizeFocus::W);
    return true;
}

bool Toolbar::onResizeKey(SDL_Keycode sym) {
    // Brush size field takes priority
    if (brushSizeFocused) {
        if (sym == SDLK_BACKSPACE) {
            int len = brushSizeBufLen();
            if (len > 0) { brushSizeBuf[len - 1] = 0; }
            return true;
        }
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_ESCAPE || sym == SDLK_TAB) {
            int v = 0;
            for (int i = 0; i < brushSizeBufLen(); i++) v = v * 10 + (brushSizeBuf[i] - '0');
            brushSize = std::max(1, std::min(99, v > 0 ? v : brushSize));
            snprintf(brushSizeBuf, sizeof(brushSizeBuf), "%d", brushSize);
            brushSizeFocused = false;
            SDL_StopTextInput();
            return true;
        }
        return false;
    }
    if (resizeFocus == ResizeFocus::NONE) return false;
    char* buf = (resizeFocus == ResizeFocus::W) ? resizeWBuf : resizeHBuf;

    if (sym == SDLK_BACKSPACE) {
        int len = (int)std::strlen(buf);
        if (len > 0) { buf[len - 1] = 0; }
        clampResizeInput(resizeFocus == ResizeFocus::W);
        applyAspectLock(resizeFocus == ResizeFocus::W);
        return true;
    }
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        commitResize();
        resizeFocus = ResizeFocus::NONE;
        SDL_StopTextInput();
        return true;
    }
    if (sym == SDLK_TAB) {
        resizeFocus = (resizeFocus == ResizeFocus::W) ? ResizeFocus::H : ResizeFocus::W;
        return true;
    }
    if (sym == SDLK_ESCAPE) {
        defocusResize(false);  // revert and unfocus
        return true;
    }
    return false;
}

bool Toolbar::onArrowKey(int dx, int dy) {
    const int CUSTOM_COLS = 3, CUSTOM_ROWS = 3;
    const int PRESET_COLS = 3, PRESET_ROWS = 9;
    if (selectedCustomSlot >= 0) {
        int col = selectedCustomSlot % CUSTOM_COLS;
        int row = selectedCustomSlot / CUSTOM_COLS;
        if (dy > 0 && row == CUSTOM_ROWS - 1) {
            // Down from bottom of custom → preset top row, same column
            int presetSlot = 0 * PRESET_COLS + std::max(0, std::min(PRESET_COLS - 1, col + dx));
            selectedCustomSlot = -1;
            selectedPresetSlot = presetSlot;
            brushColor = PRESETS[selectedPresetSlot];
            rgbToHsv(brushColor, hue, sat, val);
            if (onColorChanged) onColorChanged(brushColor);
            return true;
        }
        col = std::max(0, std::min(CUSTOM_COLS - 1, col + dx));
        row = std::max(0, std::min(CUSTOM_ROWS - 1, row + dy));
        int next = row * CUSTOM_COLS + col;
        if (next != selectedCustomSlot) {
            selectedCustomSlot = next;
            selectedPresetSlot = -1;
            brushColor = customColors[selectedCustomSlot];
            rgbToHsv(brushColor, hue, sat, val);
            if (onColorChanged) onColorChanged(brushColor);
            return true;
        }
        return false;
    }
    if (selectedPresetSlot >= 0) {
        int col = selectedPresetSlot % PRESET_COLS;
        int row = selectedPresetSlot / PRESET_COLS;
        if (dy < 0 && row == 0) {
            // Up from top of preset → custom bottom row, same column
            col = std::max(0, std::min(CUSTOM_COLS - 1, col + dx));
            int customSlot = (CUSTOM_ROWS - 1) * CUSTOM_COLS + col;
            selectedPresetSlot = -1;
            selectedCustomSlot = customSlot;
            brushColor = customColors[selectedCustomSlot];
            rgbToHsv(brushColor, hue, sat, val);
            if (onColorChanged) onColorChanged(brushColor);
            return true;
        }
        col = std::max(0, std::min(PRESET_COLS - 1, col + dx));
        row = std::max(0, std::min(PRESET_ROWS - 1, row + dy));
        int next = row * PRESET_COLS + col;
        if (next != selectedPresetSlot) {
            selectedPresetSlot = next;
            selectedCustomSlot = -1;
            brushColor = PRESETS[selectedPresetSlot];
            rgbToHsv(brushColor, hue, sat, val);
            if (onColorChanged) onColorChanged(brushColor);
            return true;
        }
        return false;
    }
    if (dx != 0 || dy != 0) {
        selectedCustomSlot = 0;
        selectedPresetSlot = -1;
        brushColor = customColors[0];
        rgbToHsv(brushColor, hue, sat, val);
        if (onColorChanged) onColorChanged(brushColor);
        return true;
    }
    return false;
}

Toolbar::CanvasResizeRequest Toolbar::getResizeRequest() {
    CanvasResizeRequest req = pendingResize;
    pendingResize.pending = false;
    return req;
}

void Toolbar::syncCanvasSize(int w, int h) {
    resizeLockW = w;
    resizeLockH = h;
    snprintf(resizeWBuf, sizeof(resizeWBuf), "%d", w);
    snprintf(resizeHBuf, sizeof(resizeHBuf), "%d", h);
}

void Toolbar::syncBrushSize() {
    snprintf(brushSizeBuf, sizeof(brushSizeBuf), "%d", brushSize);
}

void Toolbar::commitResize() {
    int w = 0, h = 0;
    for (int i = 0; i < resizeWBufLen(); i++) w = w * 10 + (resizeWBuf[i] - '0');
    for (int i = 0; i < resizeHBufLen(); i++) h = h * 10 + (resizeHBuf[i] - '0');
    if (w > 0 && h > 0) {
        pendingResize = { true, w, h, resizeScaleMode };
    }
}

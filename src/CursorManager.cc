#include "CursorManager.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>

static inline Uint32 argb(Uint8 a, Uint8 r, Uint8 g, Uint8 b) {
    return ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
}

static const Uint32 C_BLACK  = argb(255,   0,   0,   0);
static const Uint32 C_WHITE  = argb(255, 255, 255, 255);
static const Uint32 C_TRANSP = argb(  0,   0,   0,   0);
static const Uint32 C_BLUE   = argb(255, 100, 149, 237);

static SDL_Cursor* makePickCursor(SDL_Color tipColor);

struct Bitmap {
    int w, h;
    std::vector<Uint32> d;
    Bitmap(int w, int h) : w(w), h(h), d(w * h, C_TRANSP) {}
    void set(int x, int y, Uint32 c) { if (x>=0&&x<w&&y>=0&&y<h) d[y*w+x]=c; }
    Uint32 get(int x, int y) const   { return (x>=0&&x<w&&y>=0&&y<h) ? d[y*w+x] : C_TRANSP; }
    void hline(int x0, int x1, int y, Uint32 c) { for(int x=x0;x<=x1;x++) set(x,y,c); }
    void line(int x0, int y0, int x1, int y1, Uint32 c) {
        int dx=std::abs(x1-x0), dy=std::abs(y1-y0);
        int sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx-dy;
        while(true) {
            set(x0,y0,c);
            if(x0==x1&&y0==y1) break;
            int e2=2*err;
            if(e2>-dy){err-=dy;x0+=sx;}
            if(e2< dx){err+=dx;y0+=sy;}
        }
    }
    void outline(Uint32 c=C_BLACK) {
        Bitmap copy=*this;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            if(copy.get(x,y)!=C_TRANSP) continue;
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++)
                if(copy.get(x+dx,y+dy)!=C_TRANSP) { set(x,y,c); goto next; }
            next:;
        }
    }
    SDL_Cursor* toCursor(int hotX, int hotY) const {
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
        if(!s) return nullptr;
        memcpy(s->pixels, d.data(), w*h*4);
        SDL_Cursor* cur = SDL_CreateColorCursor(s, hotX, hotY);
        SDL_FreeSurface(s);
        return cur;
    }
};

static SDL_Cursor* makeBucketCursor(SDL_Color fillColor) {
    const int W=24, H=24;
    Bitmap b(W,H);
    const int ox=13, oy=15, s=7;
    const Uint32 WHITE = argb(255, 255, 255, 255);
    const Uint32 BLACK = argb(255,   0,   0,   0);
    const Uint32 FILL  = fillColor.a > 0
        ? argb(255, fillColor.r, fillColor.g, fillColor.b)
        : argb(255, 100, 149, 237);
    for(int row=0; row<=s; row++) {
        int halfW = s - row;
        for(int x = ox-halfW+1; x <= ox+halfW-1; x++)
            b.set(x, oy+row, WHITE);
    }
    for(int row=1; row<=s; row++) {
        int halfW = s - row;
        for(int x = ox-halfW+1; x <= ox+halfW-1; x++)
            b.set(x, oy-row, WHITE);
    }
    for(int row=-1; row<=s; row++) {
        int halfW = s - std::abs(row);
        for(int x = ox-halfW+1; x <= ox+halfW-1; x++)
            b.set(x, oy+row, FILL);
    }
    b.line(ox,    oy-s, ox+s, oy,   BLACK);
    b.line(ox+s,  oy,   ox,   oy+s, BLACK);
    b.line(ox,    oy+s, ox-s, oy,   BLACK);
    b.line(ox-s,  oy,   ox,   oy-s, BLACK);
    const int hLen = 2;
    b.line(ox,     oy-s, ox+hLen,   oy-s-hLen, BLACK);
    b.line(ox+1,   oy-s, ox+hLen+1, oy-s-hLen, BLACK);
    const int ddx = ox - s - 2, ddy = oy;

    b.set(ddx,     ddy,     BLACK);
    b.set(ddx - 1, ddy + 1, BLACK);
    b.set(ddx,     ddy + 1, WHITE);
    b.set(ddx + 1, ddy + 1, BLACK);
    b.set(ddx - 2, ddy + 2, BLACK);
    b.set(ddx - 1, ddy + 2, WHITE);
    b.set(ddx,     ddy + 2, WHITE);
    b.set(ddx + 1, ddy + 2, WHITE);
    b.set(ddx + 2, ddy + 2, BLACK);
    b.set(ddx - 1, ddy + 3, BLACK);
    b.set(ddx,     ddy + 3, WHITE);
    b.set(ddx + 1, ddy + 3, BLACK);
    b.set(ddx,     ddy + 4, BLACK);
    b.outline(WHITE);
    return b.toCursor(ddx, ddy + 2);
}

static void drawArrowUp(Bitmap& b, int cx, int cy, Uint32 fill, Uint32 outline) {
    b.hline(cx-1, cx,   cy-9, fill);
    b.hline(cx-2, cx+1, cy-8, fill);
    b.hline(cx-3, cx+2, cy-7, fill);
    for (int y = cy-6; y <= cy+6; y++) b.hline(cx-1, cx, y, fill);
    b.hline(cx-1, cx,   cy+9, fill);
    b.hline(cx-2, cx+1, cy+8, fill);
    b.hline(cx-3, cx+2, cy+7, fill);
    b.outline(outline);
}

static Bitmap rotateBitmap(const Bitmap& src, float angleDeg) {
    Bitmap dst(src.w, src.h);
    float rad  = angleDeg * (float)M_PI / 180.f;
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);
    float cx   = (src.w - 1) * 0.5f;
    float cy   = (src.h - 1) * 0.5f;
    for (int y = 0; y < dst.h; y++) {
        for (int x = 0; x < dst.w; x++) {
            float fx = (x - cx);
            float fy = (y - cy);
            int sx = (int)std::round( cosA * fx + sinA * fy + cx);
            int sy = (int)std::round(-sinA * fx + cosA * fy + cy);
            dst.set(x, y, src.get(sx, sy));
        }
    }
    return dst;
}

SDL_Cursor* CursorManager::makeResizeArrowCursor(float angleDeg) {
    const int SZ = 23;
    const int cx = SZ / 2;
    const int cy = SZ / 2;
    Bitmap base(SZ, SZ);
    drawArrowUp(base, cx, cy, C_BLACK, C_WHITE);
    Bitmap rotated = rotateBitmap(base, angleDeg);

    return rotated.toCursor(cx, cy);
}

static void drawRotateCursorBase(Bitmap& b, Uint32 fill, Uint32 outlineCol) {
    // 23x23 bitmap. Double-headed arrow with a smooth leftward bow.
    // Shaft is 2px wide throughout; the left column of the pair shifts left
    // toward the centre using a sine curve, giving 3px of total travel.
    //
    // Shaft pair (sc, sc+1) per row — sc = shaft_col(y):
    //   y= 1: sc=11  (tip, rightmost)
    //   y= 5: sc= 9
    //   y= 7: sc= 8  (peak bow, leftmost)
    //   ...
    //   y=13: sc= 8
    //   y=16: sc=10
    //   y=19: sc=11  (tip, rightmost)
    //
    // Arrowheads are 2/4/6px, centred on the tip shaft pair (sc=11 → cols 11,12).
    //
    // ASCII preview (# = fill, o = 1px white outline):
    //  y 0  ..........oooo.........
    //  y 1  .........oo##oo........   north tip
    //  y 2  ........oo####oo.......
    //  y 3  ........o######o.......   head base
    //  y 4  ........oo##oooo.......
    //  y 5  ........o##oo..........
    //  y 6  .......oo##o...........
    //  y 7  .......o##oo...........   peak bow
    //  y10  .......o##o............
    //  y13  .......o##oo...........
    //  y16  ........oo##oooo.......
    //  y17  ........o######o.......   head base
    //  y18  ........oo####oo.......
    //  y19  .........oo##oo........   south tip

    // North arrowhead — symmetric, centred on tip shaft position (cols 11,12)
    b.hline(11, 12,  1, fill);  // 2px tip
    b.hline(10, 13,  2, fill);  // 4px
    b.hline( 9, 14,  3, fill);  // 6px base

    // Shaft — row by row following the bow curve
    b.hline(10, 11,  4, fill);
    b.hline( 9, 10,  5, fill);
    b.hline( 9, 10,  6, fill);
    b.hline( 8,  9,  7, fill);
    b.hline( 8,  9,  8, fill);
    b.hline( 8,  9,  9, fill);
    b.hline( 8,  9, 10, fill);
    b.hline( 8,  9, 11, fill);
    b.hline( 8,  9, 12, fill);
    b.hline( 8,  9, 13, fill);
    b.hline( 9, 10, 14, fill);
    b.hline( 9, 10, 15, fill);
    b.hline(10, 11, 16, fill);

    // South arrowhead — symmetric, centred on tip shaft position (cols 11,12)
    b.hline( 9, 14, 17, fill);  // 6px base
    b.hline(10, 13, 18, fill);  // 4px
    b.hline(11, 12, 19, fill);  // 2px tip

    b.outline(outlineCol);
}

SDL_Cursor* CursorManager::makeRotateCursor(float angleDeg) {
    const int SZ = 23;
    Bitmap base(SZ, SZ);
    drawRotateCursorBase(base, C_BLACK, C_WHITE);
    // The arrow is drawn vertically (north↔south). Rotate 90° so it sits
    // horizontally, then add the shape's rotation on top.
    Bitmap rotated = rotateBitmap(base, angleDeg + 90.f);
    const int c = SZ / 2;
    return rotated.toCursor(c, c);
}

// Mirror a direction angle (degrees, 0 = north, clockwise) by flip.
// Used in shape local space: flipX = reflect through vertical axis, flipY = through horizontal.
static float applyFlipToAngle(float deg, bool flipX, bool flipY) {
    if (flipX) deg = 360.f - deg;
    if (flipY) deg = 180.f - deg;
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

void CursorManager::buildRotateCursor(float rotationRad) {
    float deg = std::fmod(rotationRad * 180.f / (float)M_PI, 360.f);
    if (deg < 0.f) deg += 360.f;
    if (std::fabs(deg - lastRotateCursorDeg) < 0.5f && lastRotateCursorDeg > -999.f) return;
    lastRotateCursorDeg = deg;
    if (curRotate) { SDL_FreeCursor(curRotate); curRotate = nullptr; }
    curRotate = makeRotateCursor(deg);
}

// ── Build / cache the 8 resize cursors ───────────────────────────────────────

void CursorManager::buildResizeCursors(float rotationRad, bool flipX, bool flipY) {
    float deg = std::fmod(rotationRad * 180.f / (float)M_PI, 360.f);
    if (deg < 0.f) deg += 360.f;

    // Never use cache when flipped so cursor always reflects current flip; when not flipped, cache by rotation.
    if (flipX || flipY) { /* always rebuild */ }
    else if (lastResizeRotationDeg >= -999.f &&
             std::fabs(deg - lastResizeRotationDeg) < 0.5f &&
             flipX == lastResizeFlipX && flipY == lastResizeFlipY)
        return;

    lastResizeRotationDeg = deg;
    lastResizeFlipX = flipX;
    lastResizeFlipY = flipY;

    for (int i = 0; i < NUM_RESIZE_SLOTS; i++) {
        if (curResize[i]) { SDL_FreeCursor(curResize[i]); curResize[i] = nullptr; }
    }

    // Cursor arrow = handle direction in shape local frame, mirrored by flip, then rotated to world.
    // So: localAngle = i*45° (N=0, NE=45, E=90, ...), mirror by flip, then worldAngle = localAngle' + rotation.
    // This makes the arrow reflect the flipped content at any rotation.
    for (int i = 0; i < NUM_RESIZE_SLOTS; i++) {
        float localAngle = (float)i * 45.f;
        if (flipX || flipY) localAngle = applyFlipToAngle(localAngle, flipX, flipY);
        float arrowAngle = localAngle + deg;
        arrowAngle = std::fmod(arrowAngle, 360.f);
        if (arrowAngle < 0.f) arrowAngle += 360.f;
        curResize[i] = makeResizeArrowCursor(arrowAngle);
    }
}

// Map a Handle to its slot index (0-7: N, NE, E, SE, S, SW, W, NW)
// and return the matching pre-built cursor.
SDL_Cursor* CursorManager::getResizeCursor(TransformTool::Handle h, float rotationRad, bool flipX, bool flipY) {
    buildResizeCursors(rotationRad, flipX, flipY);
    switch (h) {
        case TransformTool::Handle::N:  return curResize[0];
        case TransformTool::Handle::NE: return curResize[1];
        case TransformTool::Handle::E:  return curResize[2];
        case TransformTool::Handle::SE: return curResize[3];
        case TransformTool::Handle::S:  return curResize[4];
        case TransformTool::Handle::SW: return curResize[5];
        case TransformTool::Handle::W:  return curResize[6];
        case TransformTool::Handle::NW: return curResize[7];
        default:                        return nullptr;
    }
}

// ── Brush / eraser pixel helpers ──────────────────────────────────────────────

void CursorManager::fillCircle(Uint32* buf, int w, int h, int cx, int cy, int r, Uint32 color) {
    for(int dy=-r;dy<=r;dy++) {
        int row=cy+dy; if(row<0||row>=h) continue;
        int half=(int)std::sqrt((float)(r*r-dy*dy)+0.5f);
        int x0=std::max(0,cx-half), x1=std::min(w-1,cx+half);
        for(int x=x0;x<=x1;x++) buf[row*w+x]=color;
    }
}
void CursorManager::fillSquare(Uint32* buf, int w, int h, int cx, int cy, int half, Uint32 color) {
    int x0=std::max(0,cx-half), x1=std::min(w-1,cx+half);
    int y0=std::max(0,cy-half), y1=std::min(h-1,cy+half);
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) buf[y*w+x]=color;
}
void CursorManager::outlineCircle(Uint32* buf, int w, int h, int cx, int cy, int r, Uint32 color) {
    auto plot8=[&](int x, int y){
        auto s=[&](int px,int py){ if(px>=0&&px<w&&py>=0&&py<h) buf[py*w+px]=color; };
        s(cx+x,cy+y);s(cx-x,cy+y);s(cx+x,cy-y);s(cx-x,cy-y);
        s(cx+y,cy+x);s(cx-y,cy+x);s(cx+y,cy-x);s(cx-y,cy-x);
    };
    int x=0,y=r,d=3-2*r;
    while(y>=x){plot8(x,y);if(d<0)d+=4*x+6;else{d+=4*(x-y)+10;y--;}x++;}
}
void CursorManager::outlineSquare(Uint32* buf, int w, int h, int cx, int cy, int half, Uint32 color) {
    int x0=std::max(0,cx-half),x1=std::min(w-1,cx+half);
    int y0=std::max(0,cy-half),y1=std::min(h-1,cy+half);
    for(int x=x0;x<=x1;x++){
        if(y0>=0&&y0<h) buf[y0*w+x]=color;
        if(y1>=0&&y1<h) buf[y1*w+x]=color;
    }
    for(int y=y0+1;y<y1;y++){
        if(x0>=0&&x0<w) buf[y*w+x0]=color;
        if(x1>=0&&x1<w) buf[y*w+x1]=color;
    }
}

SDL_Cursor* CursorManager::makeColorCursor(const Uint32* argbBuf, int w, int h, int hotX, int hotY) {
    SDL_Surface* surf=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    if(!surf) return nullptr;
    memcpy(surf->pixels,argbBuf,w*h*4);
    SDL_Cursor* cur=SDL_CreateColorCursor(surf,hotX,hotY);
    SDL_FreeSurface(surf);
    return cur;
}

void CursorManager::buildBucketCursor(SDL_Color color) {
    bool colorChanged = (color.r != lastBucketColor.r || color.g != lastBucketColor.g ||
                         color.b != lastBucketColor.b || color.a != lastBucketColor.a);
    if (!colorChanged) return;
    lastBucketColor = color;
    if (curBucket) { SDL_FreeCursor(curBucket); curBucket = nullptr; }
    curBucket = makeBucketCursor(color);
}

void CursorManager::buildPickCursor(SDL_Color tipColor) {
    if (tipColor.a == 0) tipColor = { 128, 128, 128, 255 };
    bool same = (tipColor.r == lastPickTipColor.r && tipColor.g == lastPickTipColor.g &&
                 tipColor.b == lastPickTipColor.b && tipColor.a == lastPickTipColor.a);
    if (same && lastPickTipColor.a != 0) return;  // reuse cached (lastPickTipColor.a==0 is sentinel)
    lastPickTipColor = tipColor;
    if (curPick) { SDL_FreeCursor(curPick); curPick = nullptr; }
    curPick = makePickCursor(tipColor);
}

// Draw crosshair arms into buf (size canvasDim×canvasDim), centered at (cx,cy).
// Arms are 1px black lines with a full 1px white outline including end caps.
// armStart: first pixel distance from center where the arm begins.
// canvasDim: bitmap side length (arms extend to canvasDim/2 from center).
// Draw crosshair arms into buf (canvasDim x canvasDim), centered at (cx,cy).
// Arms run from armStart to armEnd (= canvasDim/2 - 1).
//
// End-cap pattern (shown for the W arm, left=tip side, right=center side):
//
//   row cy-1:  . W W W W W    side outline, armStart..armEnd-1 (stops 1px short)
//   row cy:    W L L L L W    cap, arm pixels (armStart..armEnd), cap
//   row cy+1:  . W W W W W    side outline, armStart..armEnd-1 (stops 1px short)
//
// L = lineCol,  W = outlineCol,  . = transparent
static void drawCrosshairArms(Uint32* buf, int canvasDim, int cx, int cy,
                               int armStart, Uint32 lineCol, Uint32 outlineCol) {
    const int armEnd = canvasDim / 2 - 1;  // last arm pixel, 1px from bitmap edge

    auto set = [&](int x, int y, Uint32 c) {
        if (x >= 0 && x < canvasDim && y >= 0 && y < canvasDim)
            buf[y * canvasDim + x] = c;
    };

    // ── Outline pass ─────────────────────────────────────────────────────────
    // Side outline: cardinal neighbors of arm pixels, stopping 1px short at tip.
    for (int d = armStart; d <= armEnd; d++) {
        set(cx - 1, cy - d, outlineCol);  // N left
        set(cx + 1, cy - d, outlineCol);  // N right
        set(cx - 1, cy + d, outlineCol);  // S left
        set(cx + 1, cy + d, outlineCol);  // S right
        set(cx - d, cy - 1, outlineCol);  // W top
        set(cx - d, cy + 1, outlineCol);  // W bottom
        set(cx + d, cy - 1, outlineCol);  // E top
        set(cx + d, cy + 1, outlineCol);  // E bottom
    }
    // End cap: full 3-pixel row/column beyond the last arm pixel
    set(cx - 1, cy - armEnd - 1, outlineCol);  // N cap left
    set(cx,     cy - armEnd - 1, outlineCol);  // N cap center
    set(cx + 1, cy - armEnd - 1, outlineCol);  // N cap right
    set(cx - 1, cy + armEnd + 1, outlineCol);  // S cap left
    set(cx,     cy + armEnd + 1, outlineCol);  // S cap center
    set(cx + 1, cy + armEnd + 1, outlineCol);  // S cap right
    set(cx - armEnd - 1, cy - 1, outlineCol);  // W cap top
    set(cx - armEnd - 1, cy,     outlineCol);  // W cap center
    set(cx - armEnd - 1, cy + 1, outlineCol);  // W cap bottom
    set(cx + armEnd + 1, cy - 1, outlineCol);  // E cap top
    set(cx + armEnd + 1, cy,     outlineCol);  // E cap center
    set(cx + armEnd + 1, cy + 1, outlineCol);  // E cap bottom

    // ── Line pixels on top ────────────────────────────────────────────────────
    for (int d = armStart; d <= armEnd; d++) {
        set(cx,     cy - d, lineCol);  // N
        set(cx,     cy + d, lineCol);  // S
        set(cx - d, cy,     lineCol);  // W
        set(cx + d, cy,     lineCol);  // E
    }
}

void CursorManager::buildBrushCursors(ICoordinateMapper* mapper, int brushSize, bool squareBrush, SDL_Color color) {
    int winSize = std::max(1, mapper->getWindowSize(brushSize));
    bool colorChanged = (color.r != lastBrushColor.r || color.g != lastBrushColor.g ||
                         color.b != lastBrushColor.b || color.a != lastBrushColor.a);
    if (winSize == lastWinSize && squareBrush == lastSquareBrush && !colorChanged) return;
    lastWinSize = winSize; lastSquareBrush = squareBrush; lastBrushColor = color;
    if (curBrush)  { SDL_FreeCursor(curBrush);  curBrush  = nullptr; }
    if (curEraser) { SDL_FreeCursor(curEraser); curEraser = nullptr; }

    // ── Two cursor modes for brushes above the tiny threshold (winSize > 8) ───
    //
    //  winSize <= MAX_DIM → normal: draw brush dot only, NO crosshair arms.
    //                       The dot accurately represents the brush footprint.
    //
    //  winSize > MAX_DIM  → capped: the brush is too large to fit in a cursor
    //                       bitmap. Show a plain crosshair with a fixed 1px
    //                       center dot so the hotspot is always precise.
    //                       Both brush and eraser get the same crosshair here.

    const int MAX_DIM = 63;   // maximum bitmap side length SDL reliably supports

    if (winSize > MAX_DIM) {
        // Capped: brush is too large for a cursor bitmap. Show a crosshair with
        // a 1px center dot colored to match the brush / eraser respectively.
        Uint32 brushDot  = argb(255, color.r, color.g, color.b);
        const Uint32 ERASER_DOT = argb(128, 100, 149, 237);
        curBrush  = makeCrossHairCursor(0, squareBrush, brushDot);
        curEraser = makeCrossHairCursor(0, squareBrush, ERASER_DOT);
        return;
    }

    // ── Normal mode: draw brush dot only, no crosshair arms ───────────────────
    const int canvasDim = MAX_DIM;
    const int cx = canvasDim / 2;  // = 31
    const int cy = canvasDim / 2;
    const int hotX = cx, hotY = cy;

    int r = (winSize - 1) / 2;

    std::vector<Uint32> bb(canvasDim * canvasDim, C_TRANSP);
    std::vector<Uint32> eb(canvasDim * canvasDim, C_TRANSP);
    Uint32 bc = argb(255, color.r, color.g, color.b);
    const Uint32 ERASER_FILL = argb(128, 100, 149, 237);

    if (squareBrush) {
        fillSquare  (bb.data(), canvasDim, canvasDim, cx, cy, r, bc);
        outlineSquare(bb.data(), canvasDim, canvasDim, cx, cy, r, C_WHITE);
        fillSquare  (eb.data(), canvasDim, canvasDim, cx, cy, r, ERASER_FILL);
        outlineSquare(eb.data(), canvasDim, canvasDim, cx, cy, r, C_WHITE);
    } else {
        fillCircle  (bb.data(), canvasDim, canvasDim, cx, cy, r, bc);
        outlineCircle(bb.data(), canvasDim, canvasDim, cx, cy, r, C_WHITE);
        fillCircle  (eb.data(), canvasDim, canvasDim, cx, cy, r, ERASER_FILL);
        outlineCircle(eb.data(), canvasDim, canvasDim, cx, cy, r, C_WHITE);
    }

    curBrush  = makeColorCursor(bb.data(), canvasDim, canvasDim, hotX, hotY);
    curEraser = makeColorCursor(eb.data(), canvasDim, canvasDim, hotX, hotY);
}

// ── Eyedropper / pick cursor ──────────────────────────────────────────────────
//
// Classic eyedropper: diagonal tube body (NE→SW), small square cap at the top,
// angled nib at the bottom-left. The hotspot is the very tip (bottom-left pixel).
//
// Bitmap is 22×22. The dropper body runs diagonally from (16,2) down to (6,12).
// The tip nib extends to (3,15). Hotspot = (3,15).
//
//  ....XXXX......................
//  ...XXXXXXX....................
//  ..XXXXXXXXX...................
//  ...XXXXXXXXX..................
//  ....XXXXXXXX..................
//  .....XXXXXXX..................
//  ......XXXXXXX.................
//  .......XXXXXXX................
//  ........XXXXXXX...............
//  .........XXXXXXX..............
//  ..........XXXXXX..............
//  ...........XXXX...............
//  ..........XXXX................
//  .........XXX..................
//  ........XX....................
//  .......X......................  ← hotspot (tip)
//  The cap (top end) is drawn in tipColor so it shows the color under the cursor.

static SDL_Cursor* makePickCursor(SDL_Color tipColor) {
    const int W = 22, H = 22;
    Bitmap b(W, H);

    // Default when transparent or not over canvas
    if (tipColor.a == 0) tipColor = { 128, 128, 128, 255 };
    Uint32 capARGB = argb(tipColor.a, tipColor.r, tipColor.g, tipColor.b);

    // Body: 2-px wide diagonal tube (SW direction)
    for (int i = 0; i <= 9; i++) {
        int x = 16 - i, y = 2 + i;
        b.set(x,   y,   C_WHITE);
        b.set(x-1, y,   C_WHITE);
        b.set(x,   y+1, C_WHITE);
    }

    // Cap at top-right (the other end): show the sampled color.
    // Taper 4-4-3-2 so the bottom flows into the body.
    b.set(14, 0, capARGB); b.set(15, 0, capARGB); b.set(16, 0, capARGB); b.set(17, 0, capARGB);   // y=0: 4 px
    b.set(14, 1, capARGB); b.set(15, 1, capARGB); b.set(16, 1, capARGB); b.set(17, 1, capARGB);   // y=1: 4 px
    b.set(14, 2, capARGB); b.set(15, 2, capARGB); b.set(16, 2, capARGB);   // y=2: 3 px
    b.set(15, 3, capARGB); b.set(16, 3, capARGB);   // y=3: 2 px, flows into body

    // Nib: tapered tip (hotspot end) stays white
    b.set(7, 12, C_WHITE);
    b.set(6, 12, C_WHITE);
    b.set(6, 13, C_WHITE);
    b.set(5, 13, C_WHITE);
    b.set(5, 14, C_WHITE);
    b.set(4, 14, C_WHITE);
    b.set(4, 15, C_WHITE);
    b.set(3, 15, C_WHITE); // tip pixel (hotspot)

    // Black outline around everything for visibility
    b.outline(C_BLACK);

    // Re-draw body and nib (outline may have overwritten)
    for (int i = 0; i <= 9; i++) {
        int x = 16 - i, y = 2 + i;
        b.set(x,   y,   C_WHITE);
        b.set(x-1, y,   C_WHITE);
        b.set(x,   y+1, C_WHITE);
    }
    b.set(7, 12, C_WHITE); b.set(6, 12, C_WHITE);
    b.set(6, 13, C_WHITE); b.set(5, 13, C_WHITE);
    b.set(5, 14, C_WHITE); b.set(4, 14, C_WHITE);
    b.set(4, 15, C_WHITE); b.set(3, 15, C_WHITE);
    // Re-draw cap in sampled color (same tapered shape: 4-4-3-2)
    b.set(14, 0, capARGB); b.set(15, 0, capARGB); b.set(16, 0, capARGB); b.set(17, 0, capARGB);
    b.set(14, 1, capARGB); b.set(15, 1, capARGB); b.set(16, 1, capARGB); b.set(17, 1, capARGB);
    b.set(14, 2, capARGB); b.set(15, 2, capARGB); b.set(16, 2, capARGB);
    b.set(15, 3, capARGB); b.set(16, 3, capARGB);

    // 1px white outline so the dropper reads on dark backgrounds
    b.outline(C_WHITE);

    return b.toCursor(3, 15);
}

// ── Crosshair cursor ──────────────────────────────────────────────────────────
//
// Used when the brush/eraser stamp would render smaller than TINY_BRUSH_WIN_PX.
// The center dot grows with the brush window size so it echoes the actual stamp.
//
// Built with SDL_CreateColorCursor (ARGB8888 Bitmap):
//   - Arms and dot drawn in BLACK.
//   - 1-pixel WHITE outline added via Bitmap::outline() so the cursor reads on
//     both dark and light backgrounds (XOR cursor mode is not reliably supported
//     on macOS under SDL2 — it produces an invisible cursor there).
//
// Layout (dotRadius = winSz / 2, bitmap 23×23, center c=11):
//   Filled circle of radius dotRadius at (c, c)   ← dot tracks brush size
//   Gap of (dotRadius + 3) transparent px from center ← scales with dot
//   Single-pixel arms from gap edge to bitmap edge

SDL_Cursor* CursorManager::makeCrossHairCursor(int dotRadius, bool squareDot, Uint32 dotColor) {
    // Used for winSize <= TINY_BRUSH_WIN_PX (<=8px). The center dot echoes the
    // actual brush shape (circle or square) and scales with winSize / 2.
    // Arms are 1px black with full white outline including end caps.
    const int SZ = 23;
    const int c  = SZ / 2;   // = 11
    const int gapSize = 2;   // transparent gap between dot white-outline and arm start

    std::vector<Uint32> buf(SZ * SZ, C_TRANSP);

    auto set = [&](int x, int y, Uint32 col) {
        if (x >= 0 && x < SZ && y >= 0 && y < SZ) buf[y * SZ + x] = col;
    };

    // ── Center dot (black, then white outline) ────────────────────────────────
    if (squareDot) {
        for (int dy = -dotRadius; dy <= dotRadius; dy++)
            for (int dx = -dotRadius; dx <= dotRadius; dx++)
                set(c + dx, c + dy, dotColor);
    } else {
        for (int dy = -dotRadius; dy <= dotRadius; dy++)
            for (int dx = -dotRadius; dx <= dotRadius; dx++)
                if (dx * dx + dy * dy <= dotRadius * dotRadius)
                    set(c + dx, c + dy, dotColor);
    }
    // Always draw the center pixel in dotColor regardless of radius
    set(c, c, dotColor);
    // White outline around just the dot (applied before arms so arms don't bleed in)
    {
        std::vector<Uint32> copy = buf;
        for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
            if (copy[y * SZ + x] != C_TRANSP) continue;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < SZ && ny >= 0 && ny < SZ &&
                    copy[ny * SZ + nx] != C_TRANSP) { buf[y * SZ + x] = C_WHITE; goto next; }
            }
            next:;
        }
    }

    // ── Arms ──────────────────────────────────────────────────────────────────
    int armStart = dotRadius + 1 + gapSize + 1;
    drawCrosshairArms(buf.data(), SZ, c, c, armStart, C_BLACK, C_WHITE);

    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, SZ, SZ, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return nullptr;
    memcpy(s->pixels, buf.data(), SZ * SZ * 4);
    SDL_Cursor* cur = SDL_CreateColorCursor(s, c, c);
    SDL_FreeSurface(s);
    return cur;
}

void CursorManager::buildCrossHairCursor(int winSz, bool squareBrush, SDL_Color brushColor) {
    bool colorChanged = (brushColor.r != lastCrossHairColor.r ||
                         brushColor.g != lastCrossHairColor.g ||
                         brushColor.b != lastCrossHairColor.b ||
                         brushColor.a != lastCrossHairColor.a);
    if (winSz == lastCrossHairWinSz && squareBrush == lastCrossHairSquare &&
        !colorChanged && curCrossHairBrush && curCrossHairEraser) return;
    lastCrossHairWinSz  = winSz;
    lastCrossHairSquare = squareBrush;
    lastCrossHairColor  = brushColor;
    if (curCrossHairBrush)  { SDL_FreeCursor(curCrossHairBrush);  curCrossHairBrush  = nullptr; }
    if (curCrossHairEraser) { SDL_FreeCursor(curCrossHairEraser); curCrossHairEraser = nullptr; }
    int dotR = (winSz - 1) / 2;
    Uint32 brushDot  = argb(255, brushColor.r, brushColor.g, brushColor.b);
    const Uint32 ERASER_DOT = argb(128, 100, 149, 237);
    curCrossHairBrush  = makeCrossHairCursor(dotR, squareBrush, brushDot);
    curCrossHairEraser = makeCrossHairCursor(dotR, squareBrush, ERASER_DOT);
}

// ── Constructor / destructor ──────────────────────────────────────────────────

CursorManager::CursorManager() {
    // Cursors are built in init(), called after SDL_CreateWindow.
    // SDL_CreateColorCursor requires a window to exist first.
}

void CursorManager::init() {
    curArrow    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    curCross    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    curHand     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    curSizeAll  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    curSizeNS   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    curSizeWE   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    curSizeNWSE = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    curSizeNESW = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    curBucket    = makeBucketCursor({100, 149, 237, 255}); // default cornflower blue
    buildPickCursor({128, 128, 128, 255});  // default gray tip until over canvas
    buildCrossHairCursor(0, false, {0,0,0,255});  // build with dot radius 0

    // Pre-build resize cursors for the default (zero) rotation so they are
    // immediately available before any shape is drawn.
    buildResizeCursors(0.f, false, false);
    buildRotateCursor(0.f);
}

CursorManager::~CursorManager() {
    SDL_FreeCursor(curArrow);
    SDL_FreeCursor(curCross);
    SDL_FreeCursor(curHand);
    SDL_FreeCursor(curSizeAll);
    SDL_FreeCursor(curSizeNS);
    SDL_FreeCursor(curSizeWE);
    SDL_FreeCursor(curSizeNWSE);
    SDL_FreeCursor(curSizeNESW);
    if(curBucket) SDL_FreeCursor(curBucket);
    if(curBrush)  SDL_FreeCursor(curBrush);
    if(curEraser) SDL_FreeCursor(curEraser);
    if(curPick)   SDL_FreeCursor(curPick);
    if(curCrossHairBrush)  SDL_FreeCursor(curCrossHairBrush);
    if(curCrossHairEraser) SDL_FreeCursor(curCrossHairEraser);
    for (int i = 0; i < NUM_RESIZE_SLOTS; i++)
        if (curResize[i]) SDL_FreeCursor(curResize[i]);
    if (curRotate) SDL_FreeCursor(curRotate);
}

// ── setCursor ─────────────────────────────────────────────────────────────────

void CursorManager::setCursor(SDL_Cursor* c) {
    if(!c) return;
    SDL_SetCursor(c);
    SDL_ShowCursor(SDL_ENABLE);
}

void CursorManager::forceSetCursor(SDL_Cursor* c) {
    setCursor(c);
}

// ── update ────────────────────────────────────────────────────────────────────

void CursorManager::update(ICoordinateMapper* mapper,
                            ToolType currentType, ToolType /*originalType*/,
                            AbstractTool* currentTool,
                            int brushSize, bool squareBrush,
                            SDL_Color brushColor,
                            int mouseWinX, int mouseWinY,
                            bool overToolbar,
                            bool overCanvas,
                            bool nearHandle,
                            const CanvasResizer* canvasResizer,
                            int canvasW, int canvasH,
                            const SDL_Color* pickHoverColor) {
    if (overToolbar) { setCursor(curArrow); return; }

    // Canvas resize drag — lock the directional cursor for the entire drag,
    // even when the mouse wanders off the canvas edge.
    if (canvasResizer && canvasResizer->isDragging()) {
        // Re-run the hit-test at the drag origin isn't available here, so we
        // store the locked cursor when the drag begins (nearHandle + hitTest).
        // Instead, keep it simple: the locked handle was captured into
        // dragResizeHandle at the moment nearHandle+hitTest fired, so just use it.
        if (dragResizeCursor) { setCursor(dragResizeCursor); return; }
    } else {
        dragResizeCursor = nullptr; // clear when not dragging
    }

    // Canvas resize handles take priority over everything — show the correct
    // directional arrow even while a draw tool is active.
    if (nearHandle && canvasResizer) {
        CanvasResizer::Handle ch = canvasResizer->hitTest(mouseWinX, mouseWinY, canvasW, canvasH);
        SDL_Cursor* rc = nullptr;
        switch (ch) {
            case CanvasResizer::Handle::N:
            case CanvasResizer::Handle::S:  rc = curSizeNS;   break;
            case CanvasResizer::Handle::E:
            case CanvasResizer::Handle::W:  rc = curSizeWE;   break;
            case CanvasResizer::Handle::NW:
            case CanvasResizer::Handle::SE: rc = curSizeNWSE; break;
            case CanvasResizer::Handle::NE:
            case CanvasResizer::Handle::SW: rc = curSizeNESW; break;
            default: break;
        }
        if (rc) { dragResizeCursor = rc; setCursor(rc); return; }
    }

    // Outside canvas and not actively drawing — hand cursor still shows (everywhere except toolbar).
    // Treat SELECT/RESIZE as "active" when mutating (resizing/rotating/moving) so we still show
    // the correct resize cursor with flip even when the mouse leaves the canvas during drag.
    bool toolActive = currentTool && currentTool->isActive();
    if (!toolActive && currentTool && (currentType == ToolType::SELECT || currentType == ToolType::RESIZE)) {
        if (currentType == ToolType::SELECT) {
            SelectTool* st = static_cast<SelectTool*>(currentTool);
            if (st->isMutating()) toolActive = true;
        } else if (currentType == ToolType::RESIZE) {
            ResizeTool* rt = static_cast<ResizeTool*>(currentTool);
            if (rt->isMutating()) toolActive = true;
        }
    }
    if (currentType != ToolType::HAND && !overCanvas && !toolActive) {
        setCursor(curArrow);
        return;
    }

    switch(currentType) {
        case ToolType::BRUSH: {
            int winSz = std::max(1, mapper->getWindowSize(brushSize));
            if (winSz <= TINY_BRUSH_WIN_PX) {
                buildCrossHairCursor(winSz, squareBrush, brushColor);
                if (curCrossHairBrush) setCursor(curCrossHairBrush);
            } else {
                buildBrushCursors(mapper,brushSize,squareBrush,brushColor);
                if(curBrush) setCursor(curBrush);
            }
            break;
        }
        case ToolType::ERASER: {
            int winSz = std::max(1, mapper->getWindowSize(brushSize));
            if (winSz <= TINY_BRUSH_WIN_PX) {
                buildCrossHairCursor(winSz, squareBrush, brushColor);
                if (curCrossHairEraser) setCursor(curCrossHairEraser);
            } else {
                buildBrushCursors(mapper,brushSize,squareBrush,brushColor);
                if(curEraser) setCursor(curEraser);
            }
            break;
        }
        case ToolType::LINE:
        case ToolType::RECT:
        case ToolType::CIRCLE:
            setCursor(curCross);
            break;
        case ToolType::FILL:
            buildBucketCursor(brushColor);
            if(curBucket) setCursor(curBucket);
            break;
        case ToolType::PICK: {
            SDL_Color tipColor = { 128, 128, 128, 255 };
            if (pickHoverColor && pickHoverColor->a != 0) tipColor = *pickHoverColor;
            buildPickCursor(tipColor);
            if (curPick) setCursor(curPick);
            break;
        }
        case ToolType::SELECT: {
            auto* st = static_cast<SelectTool*>(currentTool);
            if (!st || !st->isSelectionActive()) { setCursor(curCross); break; }
            float rot = st->getRotation();
            // Resize cursor uses mirrored orientation (flip on) so arrow matches handle direction in all cases.
            bool flipX = true;
            bool flipY = true;
            int cX, cY; mapper->getCanvasCoords(mouseWinX, mouseWinY, &cX, &cY);
            TransformTool::Handle h;
            if (st->isMutating()) {
                TransformTool::Handle activeResize = st->getResizingHandle();
                if (activeResize != TransformTool::Handle::NONE) {
                    h = activeResize;  // resizing: use tool's current handle (updates when shape flips)
                } else {
                    if (!dragHandleLocked) {
                        dragHandleLocked = true;
                        lockedHandle = st->getHandleForCursor(cX, cY);
                    }
                    h = lockedHandle;  // rotating or moving
                }
            } else {
                dragHandleLocked = false;
                h = st->getHandleForCursor(cX, cY);
            }
            if (h == TransformTool::Handle::ROTATE) {
                buildRotateCursor(rot);
                if (curRotate) setCursor(curRotate);
                break;
            }
            SDL_Cursor* rc = getResizeCursor(h, rot, flipX, flipY);
            if (rc) {
                setCursor(rc);
            } else {
                // Not on a resize handle — check move vs. outside using rotated bounds
                setCursor(st->isHit(cX, cY) ? curSizeAll : curArrow);
            }
            break;
        }
        case ToolType::RESIZE: {
            auto* rt = static_cast<ResizeTool*>(currentTool);
            if (!rt) { setCursor(curArrow); break; }
            float rot = rt->getRotation();
            bool flipX = true;
            bool flipY = true;
            int cX, cY; mapper->getCanvasCoords(mouseWinX, mouseWinY, &cX, &cY);
            TransformTool::Handle h;
            if (rt->isMutating()) {
                TransformTool::Handle activeResize = rt->getResizingHandle();
                if (activeResize != TransformTool::Handle::NONE) {
                    h = activeResize;  // resizing: use tool's current handle (updates when shape flips)
                } else {
                    if (!dragHandleLocked) {
                        dragHandleLocked = true;
                        lockedHandle = rt->getHandleForCursor(cX, cY);
                    }
                    h = lockedHandle;  // rotating or moving
                }
            } else {
                dragHandleLocked = false;
                h = rt->getHandleForCursor(cX, cY);
            }
            if (h == TransformTool::Handle::ROTATE) {
                buildRotateCursor(rot);
                if (curRotate) setCursor(curRotate);
                break;
            }
            SDL_Cursor* rc = getResizeCursor(h, rot, flipX, flipY);
            if (rc) {
                setCursor(rc);
            } else {
                // Not on a resize handle — check move vs. outside using rotated bounds
                setCursor(rt->isHit(cX, cY) ? curSizeAll : curArrow);
            }
            break;
        }
        case ToolType::HAND:
            if (curHand) setCursor(curHand);
            break;
        default:
            setCursor(curArrow);
            break;
    }
}

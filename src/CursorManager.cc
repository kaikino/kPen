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

// ── Simple pixel buffer for building cursors ──────────────────────────────────

struct Bitmap {
    int w, h;
    std::vector<Uint32> d;
    Bitmap(int w, int h) : w(w), h(h), d(w * h, C_TRANSP) {}
    void set(int x, int y, Uint32 c) { if (x>=0&&x<w&&y>=0&&y<h) d[y*w+x]=c; }
    Uint32 get(int x, int y) const   { return (x>=0&&x<w&&y>=0&&y<h) ? d[y*w+x] : C_TRANSP; }
    void hline(int x0, int x1, int y, Uint32 c) { for(int x=x0;x<=x1;x++) set(x,y,c); }
    void vline(int x, int y0, int y1, Uint32 c) { for(int y=y0;y<=y1;y++) set(x,y,c); }
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
    // Add 1-pixel outline around all non-transparent pixels
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

// ── Hand-drawn cursors (all use SDL_CreateColorCursor — works on macOS) ───────

static SDL_Cursor* makeBucketCursor(SDL_Color fillColor) {
    const int W=24, H=24;
    Bitmap b(W,H);

    const int ox=10, oy=15, s=7;

    // ── Opaque Color Definitions ─────────────────────────────────────────────
    const Uint32 WHITE = argb(255, 255, 255, 255);
    const Uint32 BLACK = argb(255,   0,   0,   0);
    const Uint32 FILL  = fillColor.a > 0
        ? argb(255, fillColor.r, fillColor.g, fillColor.b)
        : argb(255, 100, 149, 237);

    // ── Diamond body (Main Bucket) ───────────────────────────────────────────
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

    // Fill color (Lowered height: starts at -1 instead of -2)
    for(int row=-1; row<=s; row++) {
        int halfW = s - std::abs(row);
        for(int x = ox-halfW+1; x <= ox+halfW-1; x++)
            b.set(x, oy+row, FILL);
    }

    // Diamond outline
    b.line(ox,    oy-s, ox+s, oy,   BLACK);
    b.line(ox+s,  oy,   ox,   oy+s, BLACK);
    b.line(ox,    oy+s, ox-s, oy,   BLACK);
    b.line(ox-s,  oy,   ox,   oy-s, BLACK);

    // ── Shorter Handle ───────────────────────────────────────────────────────
    const int hLen = 2; // Reduced from 3 to 2
    b.line(ox,   oy-s,   ox-hLen,   oy-s-hLen, BLACK);
    b.line(ox-1, oy-s,   ox-hLen-1, oy-s-hLen, BLACK);

    // ── Indicator diamond (5x5 Opaque) ───────────────────────────────────────
    const int ddx = ox + s + 2, ddy = oy; 

    b.set(ddx,     ddy,     BLACK); // Row 0
    
    b.set(ddx - 1, ddy + 1, BLACK); // Row 1
    b.set(ddx,     ddy + 1, WHITE);
    b.set(ddx + 1, ddy + 1, BLACK);
    
    b.set(ddx - 2, ddy + 2, BLACK); // Row 2 (Center)
    b.set(ddx - 1, ddy + 2, WHITE);
    b.set(ddx,     ddy + 2, WHITE); 
    b.set(ddx + 1, ddy + 2, WHITE);
    b.set(ddx + 2, ddy + 2, BLACK);

    b.set(ddx - 1, ddy + 3, BLACK); // Row 3
    b.set(ddx,     ddy + 3, WHITE);
    b.set(ddx + 1, ddy + 3, BLACK);

    b.set(ddx,     ddy + 4, BLACK); // Row 4

    // Hotspot centered in the indicator diamond
    return b.toCursor(ddx, ddy + 2);
}

// ── Rotated resize-arrow cursors ──────────────────────────────────────────────
//
// We draw a double-headed arrow pointing straight up (north↔south) in a 21×21
// bitmap, then rotate the whole bitmap to the target angle. The hotspot is
// always the bitmap center (10,10).
//
// Arrow anatomy (north-pointing half, mirrored for south):
//
//      █          ← tip
//     ███
//    █████
//      █          ← shaft
//      █
//      █
//
// The shaft runs through the center; arrowheads sit at the top and bottom.

static void drawArrowUp(Bitmap& b, int cx, int cy, Uint32 fill, Uint32 outline) {
    // Symmetry axis is between cx-1 and cx (i.e. x = cx - 0.5).
    // All even-width rows use cx-N..cx+N-1 to stay balanced about that axis.
    //   2px: cx-1..cx
    //   4px: cx-2..cx+1
    //   6px: cx-3..cx+2

    // Arrowhead (north): 2→4→6px, tip is 2px to match shaft
    b.hline(cx-1, cx,   cy-9, fill);  // 2px tip
    b.hline(cx-2, cx+1, cy-8, fill);  // 4px
    b.hline(cx-3, cx+2, cy-7, fill);  // 6px base

    // Shaft: 2px wide
    for (int y = cy-6; y <= cy+6; y++) b.hline(cx-1, cx, y, fill);

    // Arrowhead (south) — exact vertical mirror
    b.hline(cx-1, cx,   cy+9, fill);  // 2px tip
    b.hline(cx-2, cx+1, cy+8, fill);  // 4px
    b.hline(cx-3, cx+2, cy+7, fill);  // 6px base

    // 1-pixel outline
    b.outline(outline);
}

// Rotate src bitmap by angleDeg (clockwise) into a new same-size bitmap.
// Uses nearest-neighbour so pixels stay crisp at cursor resolution.
static Bitmap rotateBitmap(const Bitmap& src, float angleDeg) {
    Bitmap dst(src.w, src.h);
    float rad  = angleDeg * (float)M_PI / 180.f;
    float cosA = std::cos(rad);
    float sinA = std::sin(rad);
    float cx   = (src.w - 1) * 0.5f;
    float cy   = (src.h - 1) * 0.5f;
    for (int y = 0; y < dst.h; y++) {
        for (int x = 0; x < dst.w; x++) {
            // Map destination pixel back to source space (inverse rotation)
            float fx = (x - cx);
            float fy = (y - cy);
            int sx = (int)std::round( cosA * fx + sinA * fy + cx);
            int sy = (int)std::round(-sinA * fx + cosA * fy + cy);
            dst.set(x, y, src.get(sx, sy));
        }
    }
    return dst;
}

// Build one resize-arrow cursor pointing at angleDeg clockwise from north.
SDL_Cursor* CursorManager::makeResizeArrowCursor(float angleDeg) {
    const int SZ = 23;            // bitmap size; must be odd so center is exact
    const int cx = SZ / 2;       // = 10
    const int cy = SZ / 2;

    // Draw a north↔south double-headed arrow (angle 0°)
    Bitmap base(SZ, SZ);
    drawArrowUp(base, cx, cy, C_BLACK, C_WHITE);

    // Rotate to the desired direction
    Bitmap rotated = rotateBitmap(base, angleDeg);

    return rotated.toCursor(cx, cy);
}

// ── Rotate cursor ─────────────────────────────────────────────────────────────
//
// A circular arc (~270°) with a filled arrowhead at the clockwise end.
// Drawn in a 23×23 bitmap, then rotated to match the shape's current rotation
// so the open gap always faces the rotate-handle stem direction.

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

void CursorManager::buildRotateCursor(float rotationRad) {
    float deg = std::fmod(rotationRad * 180.f / (float)M_PI, 360.f);
    if (deg < 0.f) deg += 360.f;
    if (std::fabs(deg - lastRotateCursorDeg) < 0.5f && lastRotateCursorDeg > -999.f) return;
    lastRotateCursorDeg = deg;
    if (curRotate) { SDL_FreeCursor(curRotate); curRotate = nullptr; }
    curRotate = makeRotateCursor(deg);
}

// ── Build / cache the 8 resize cursors ───────────────────────────────────────

void CursorManager::buildResizeCursors(float rotationRad) {
    // Convert to degrees, normalize to [0, 360)
    float deg = std::fmod(rotationRad * 180.f / (float)M_PI, 360.f);
    if (deg < 0.f) deg += 360.f;

    // Skip rebuild if rotation hasn't changed by more than 0.5°
    if (std::fabs(deg - lastResizeRotationDeg) < 0.5f &&
        lastResizeRotationDeg > -999.f) return;
    lastResizeRotationDeg = deg;

    // Free old cursors
    for (int i = 0; i < NUM_RESIZE_SLOTS; i++) {
        if (curResize[i]) { SDL_FreeCursor(curResize[i]); curResize[i] = nullptr; }
    }

    // Slot i corresponds to a handle whose unrotated axis sits at i*45° from
    // north (N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7).
    // With shape rotation added, the actual screen direction is i*45° + deg.
    // A double-headed arrow for slot i should point along that axis, so we
    // draw the 0°-base arrow (N↔S) and rotate it by (i*45° + deg).
    for (int i = 0; i < NUM_RESIZE_SLOTS; i++) {
        float arrowAngle = (float)i * 45.f + deg;
        curResize[i] = makeResizeArrowCursor(arrowAngle);
    }
}

// Map a Handle to its slot index (0-7: N, NE, E, SE, S, SW, W, NW)
// and return the matching pre-built cursor.
SDL_Cursor* CursorManager::getResizeCursor(TransformTool::Handle h, float rotationRad) {
    buildResizeCursors(rotationRad);
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

void CursorManager::buildBrushCursors(ICoordinateMapper* mapper, int brushSize, bool squareBrush, SDL_Color color) {
    int winSize=std::max(1, mapper->getWindowSize(brushSize));
    bool colorChanged=(color.r!=lastBrushColor.r||color.g!=lastBrushColor.g||
                       color.b!=lastBrushColor.b||color.a!=lastBrushColor.a);
    if(winSize==lastWinSize&&squareBrush==lastSquareBrush&&!colorChanged) return;
    lastWinSize=winSize; lastSquareBrush=squareBrush; lastBrushColor=color;
    if(curBrush){SDL_FreeCursor(curBrush);curBrush=nullptr;}
    if(curEraser){SDL_FreeCursor(curEraser);curEraser=nullptr;}

    // Use winSize directly as the bitmap dimension so even sizes are not
    // truncated. hotspot = dim/2 matches the brush stamp's left inset of
    // (brushSize-1)/2: for odd dim hotspot is the exact centre pixel; for
    // even dim it is the top-left pixel of the centre 2x2 block, which is
    // where the stamp begins painting.
    const int MAX_DIM=63;
    int dim=std::min(winSize, MAX_DIM);
    int r=(dim-1)/2;            // drawing radius fits within [0, dim-1]
    int hotX=dim/2, hotY=dim/2; // top-left of centre for even, centre for odd
    int cx=hotX, cy=hotY;
    std::vector<Uint32> bb(dim*dim,C_TRANSP), eb(dim*dim,C_TRANSP);
    Uint32 bc=argb(255,color.r,color.g,color.b);
    if(squareBrush){
        fillSquare(bb.data(),dim,dim,cx,cy,r,bc);
        outlineSquare(bb.data(),dim,dim,cx,cy,r,C_BLACK);
        outlineSquare(eb.data(),dim,dim,cx,cy,r,C_BLUE);
    } else {
        fillCircle(bb.data(),dim,dim,cx,cy,r,bc);
        outlineCircle(bb.data(),dim,dim,cx,cy,r,C_BLACK);
        outlineCircle(eb.data(),dim,dim,cx,cy,r,C_BLUE);
    }
    curBrush=makeColorCursor(bb.data(),dim,dim,hotX,hotY);
    curEraser=makeColorCursor(eb.data(),dim,dim,hotX,hotY);
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
    curBucket   = makeBucketCursor({100, 149, 237, 255}); // default cornflower blue

    // Pre-build resize cursors for the default (zero) rotation so they are
    // immediately available before any shape is drawn.
    buildResizeCursors(0.f);
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
                            int canvasW, int canvasH) {
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

    // Outside canvas and not actively drawing
    bool toolActive = currentTool && currentTool->isActive();
    if (!overCanvas && !toolActive) {
        setCursor(curArrow);
        return;
    }

    switch(currentType) {
        case ToolType::BRUSH:
            buildBrushCursors(mapper,brushSize,squareBrush,brushColor);
            if(curBrush) setCursor(curBrush);
            break;
        case ToolType::ERASER:
            buildBrushCursors(mapper,brushSize,squareBrush,brushColor);
            if(curEraser) setCursor(curEraser);
            break;
        case ToolType::LINE:
        case ToolType::RECT:
        case ToolType::CIRCLE:
            setCursor(curCross);
            break;
        case ToolType::FILL:
            buildBucketCursor(brushColor);
            if(curBucket) setCursor(curBucket);
            break;
        case ToolType::SELECT: {
            auto* st = static_cast<SelectTool*>(currentTool);
            if (!st || !st->isSelectionActive()) { setCursor(curCross); break; }
            float rot = st->getRotation();
            int cX, cY; mapper->getCanvasCoords(mouseWinX, mouseWinY, &cX, &cY);
            // Track which handle started the drag so the cursor stays locked
            // for the full duration, even when the mouse wanders off the shape.
            if (st->isMutating()) {
                if (!dragHandleLocked) {
                    dragHandleLocked = true;
                    lockedHandle = st->getHandleForCursor(cX, cY);
                }
            } else {
                dragHandleLocked = false;
            }
            TransformTool::Handle h = dragHandleLocked ? lockedHandle
                                                       : st->getHandleForCursor(cX, cY);
            if (h == TransformTool::Handle::ROTATE) {
                buildRotateCursor(rot);
                if (curRotate) setCursor(curRotate);
                break;
            }
            SDL_Cursor* rc = getResizeCursor(h, rot);
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
            int cX, cY; mapper->getCanvasCoords(mouseWinX, mouseWinY, &cX, &cY);
            if (rt->isMutating()) {
                if (!dragHandleLocked) {
                    dragHandleLocked = true;
                    lockedHandle = rt->getHandleForCursor(cX, cY);
                }
            } else {
                dragHandleLocked = false;
            }
            TransformTool::Handle h = dragHandleLocked ? lockedHandle
                                                       : rt->getHandleForCursor(cX, cY);
            if (h == TransformTool::Handle::ROTATE) {
                buildRotateCursor(rot);
                if (curRotate) setCursor(curRotate);
                break;
            }
            SDL_Cursor* rc = getResizeCursor(h, rot);
            if (rc) {
                setCursor(rc);
            } else {
                // Not on a resize handle — check move vs. outside using rotated bounds
                setCursor(rt->isHit(cX, cY) ? curSizeAll : curArrow);
            }
            break;
        }
        default:
            setCursor(curArrow);
            break;
    }
}

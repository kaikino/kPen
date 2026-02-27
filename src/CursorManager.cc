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
    curSizeAll  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    curSizeNS   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    curSizeWE   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    curSizeNWSE = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    curSizeNESW = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    curBucket   = makeBucketCursor({100, 149, 237, 255}); // default cornflower blue
}

CursorManager::~CursorManager() {
    SDL_FreeCursor(curArrow);
    SDL_FreeCursor(curCross);
    SDL_FreeCursor(curSizeAll);
    SDL_FreeCursor(curSizeNS);
    SDL_FreeCursor(curSizeWE);
    SDL_FreeCursor(curSizeNWSE);
    SDL_FreeCursor(curSizeNESW);
    if(curBucket) SDL_FreeCursor(curBucket);
    if(curBrush)  SDL_FreeCursor(curBrush);
    if(curEraser) SDL_FreeCursor(curEraser);
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
                            bool overToolbar) {
    if(overToolbar) { setCursor(curArrow); return; }

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
            auto* st=static_cast<SelectTool*>(currentTool);
            if(!st||!st->isSelectionActive()){ setCursor(curCross); break; }
            int cX,cY; mapper->getCanvasCoords(mouseWinX,mouseWinY,&cX,&cY);
            TransformTool::Handle h=st->getHandleForCursor(cX,cY);
            switch(h){
                case TransformTool::Handle::N:
                case TransformTool::Handle::S:  setCursor(curSizeNS);   break;
                case TransformTool::Handle::E:
                case TransformTool::Handle::W:  setCursor(curSizeWE);   break;
                case TransformTool::Handle::NW:
                case TransformTool::Handle::SE: setCursor(curSizeNWSE); break;
                case TransformTool::Handle::NE:
                case TransformTool::Handle::SW: setCursor(curSizeNESW); break;
                default: {
                    SDL_Point pt={cX,cY}; SDL_Rect bounds=st->getFloatingBounds();
                    setCursor(SDL_PointInRect(&pt,&bounds)?curSizeAll:curArrow); break;
                }
            }
            break;
        }
        case ToolType::RESIZE: {
            auto* rt=static_cast<ResizeTool*>(currentTool);
            if(!rt){ setCursor(curArrow); break; }
            int cX,cY; mapper->getCanvasCoords(mouseWinX,mouseWinY,&cX,&cY);
            TransformTool::Handle h=rt->getHandleForCursor(cX,cY);
            switch(h){
                case TransformTool::Handle::N:
                case TransformTool::Handle::S:  setCursor(curSizeNS);   break;
                case TransformTool::Handle::E:
                case TransformTool::Handle::W:  setCursor(curSizeWE);   break;
                case TransformTool::Handle::NW:
                case TransformTool::Handle::SE: setCursor(curSizeNWSE); break;
                case TransformTool::Handle::NE:
                case TransformTool::Handle::SW: setCursor(curSizeNESW); break;
                default: {
                    SDL_Point pt={cX,cY}; SDL_Rect bounds=rt->getBounds();
                    setCursor(SDL_PointInRect(&pt,&bounds)?curSizeAll:curArrow); break;
                }
            }
            break;
        }
        default:
            setCursor(curArrow);
            break;
    }
}

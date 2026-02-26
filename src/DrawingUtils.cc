#ifdef _WIN32
  #define NOMINMAX  // prevent Windows.h from defining min/max macros
#endif

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "DrawingUtils.h"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

// ── Platform clipboard headers ────────────────────────────────────────────────
#if defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_MAC
    #define KPEN_CLIPBOARD_MAC 1
    // Implementation lives in ClipboardMac.mm
  #endif
#elif defined(_WIN32)
  #define KPEN_CLIPBOARD_WIN 1
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

namespace DrawingUtils {

// ── Drawing primitives ────────────────────────────────────────────────────────

    void drawFillCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius) {
        if (radius <= 0) {
            SDL_RenderDrawPoint(renderer, centerX, centerY);
            return;
        }
        for (int h = -radius; h <= radius; h++) {
            int half = (int)std::sqrt((float)(radius * radius - h * h));
            SDL_RenderDrawLine(renderer, centerX - half, centerY + h, centerX + half, centerY + h);
        }
    }

    struct SpanBuffer {
        int canvasW, canvasH;
        std::vector<std::vector<std::pair<int,int>>> spans;
        SpanBuffer(int w, int h) : canvasW(w), canvasH(h), spans(h) {}
        void addCircle(int cx, int cy, int radius) {
            int r = std::max(0, radius);
            for (int h = -r; h <= r; h++) {
                int row = cy + h;
                if (row < 0 || row >= canvasH) continue;
                int half = (int)std::sqrt((float)(r * r - h * h));
                int x0 = std::max(0, cx - half);
                int x1 = std::min(canvasW - 1, cx + half);
                spans[row].push_back({x0, x1});
            }
        }
        // Stamp a brush of the given pixel diameter at (cx, cy).
        // For odd sizes: single circle with radius=(size-1)/2 centered on pixel.
        // For even sizes: four circles with radius=size/2-1 at the 2x2 sub-pixel
        // center, so the brush spans exactly 'size' pixels in each axis.
        void addBrush(int cx, int cy, int size) {
            if (size <= 1) { addCircle(cx, cy, 0); return; }
            if (size % 2 == 1) {
                addCircle(cx, cy, (size - 1) / 2);
            } else {
                int r = size / 2 - 1;
                addCircle(cx,     cy,     r);
                addCircle(cx + 1, cy,     r);
                addCircle(cx,     cy + 1, r);
                addCircle(cx + 1, cy + 1, r);
            }
        }
        void flush(SDL_Renderer* renderer) {
            for (int row = 0; row < canvasH; row++)
                for (auto& seg : spans[row])
                    SDL_RenderDrawLine(renderer, seg.first, row, seg.second, row);
        }
    };

    void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int size, int w, int h) {
        if (size <= 1) { SDL_RenderDrawLine(renderer, x1, y1, x2, y2); return; }
        SpanBuffer spans(w, h);
        int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1, sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;
        while (true) {
            spans.addBrush(x1, y1, size);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx)  { err += dx; y1 += sy; }
        }
        spans.flush(renderer);
    }

    void drawRect(SDL_Renderer* renderer, const SDL_Rect* rect, int size, int w, int h) {
        // Use square brush stamps so corners are sharp right angles, not rounded.
        // The stroke is centred on the rect edge: li pixels hang inward, ri outward,
        // matching the inset logic in ShapeTool / ResizeTool.
        int li = (size - 1) / 2;  // left/top half  (asymmetric for even sizes)
        int ri = size / 2;         // right/bottom half

        // Each edge is a clipped filled rect, brush-size thick.
        // Horizontal edges (top/bottom) span the full outer width — they own the corners.
        // Vertical edges span only the inner height between the two bars so corners
        // are not double-painted (avoids alpha-blending artefacts on transparent canvas).
        int x0 = rect->x - li;
        int y0 = rect->y - li;
        int x1 = rect->x + rect->w + ri + 1;  // exclusive right  (outer edge + 1)
        int y1 = rect->y + rect->h + ri + 1;  // exclusive bottom (outer edge + 1)
        int innerY0 = rect->y + ri + 1;        // top    of inner vertical span
        int innerY1 = rect->y + rect->h - li;  // bottom of inner vertical span

        auto fillClipped = [&](int fx, int fy, int fw, int fh) {
            int cx0 = std::max(0, fx),     cy0 = std::max(0, fy);
            int cx1 = std::min(w, fx + fw), cy1 = std::min(h, fy + fh);
            if (cx1 > cx0 && cy1 > cy0) {
                SDL_Rect r = { cx0, cy0, cx1 - cx0, cy1 - cy0 };
                SDL_RenderFillRect(renderer, &r);
            }
        };

        fillClipped(x0, y0,          x1 - x0, size);            // top bar
        fillClipped(x0, y1 - size,   x1 - x0, size);            // bottom bar
        fillClipped(x0, innerY0,     size, innerY1 - innerY0);  // left bar
        fillClipped(x1 - size, innerY0, size, innerY1 - innerY0); // right bar
    }

    void drawOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int size, int w, int h) {
        int left = std::min(x0,x1), top = std::min(y0,y1);
        int right = std::max(x0,x1), bottom = std::max(y0,y1);
        if (left == right || top == bottom) return;
        int cx = (left+right)/2, cy = (top+bottom)/2;
        int rx = cx-left, ry = cy-top;
        long rx2 = (long)rx*rx, ry2 = (long)ry*ry;
        auto plot = [&](SpanBuffer& spans, int x, int y) {
            // Clamp brush centers to the oval's bounding box so the stroke never
            // bleeds outside the region the caller reserved for it.
            auto clampX = [&](int px){ return std::max(left, std::min(right, px)); };
            auto clampY = [&](int py){ return std::max(top,  std::min(bottom, py)); };
            spans.addBrush(clampX(cx+x), clampY(cy+y), size);
            spans.addBrush(clampX(cx-x), clampY(cy+y), size);
            spans.addBrush(clampX(cx+x), clampY(cy-y), size);
            spans.addBrush(clampX(cx-x), clampY(cy-y), size);
        };
        SpanBuffer spans(w, h);
        int x = 0, y = ry;
        long d1 = ry2 - rx2*ry + rx2/4;
        long ddx = 2*ry2*x, ddy = 2*rx2*y;
        while (ddx < ddy) {
            plot(spans, x, y); x++; ddx += 2*ry2;
            if (d1 < 0) { d1 += ddx + ry2; }
            else { y--; ddy -= 2*rx2; d1 += ddx - ddy + ry2; }
        }
        long d2 = ry2*((long)x*x+x) + rx2*((long)(y-1)*(y-1)) - rx2*ry2;
        while (y >= 0) {
            plot(spans, x, y); y--; ddy -= 2*rx2;
            if (d2 > 0) { d2 += rx2 - ddy; }
            else { x++; ddx += 2*ry2; d2 += ddx - ddy + rx2; }
        }
        spans.flush(renderer);
    }

    SDL_Rect getOvalCenterBounds(int x0, int y0, int x1, int y1) {
        int left = std::min(x0,x1), top = std::min(y0,y1);
        int right = std::max(x0,x1), bottom = std::max(y0,y1);
        if (left == right || top == bottom) return {x0, y0, 0, 0};
        int cx = (left+right)/2, cy = (top+bottom)/2;
        int rx = cx-left, ry = cy-top;
        long rx2 = (long)rx*rx, ry2 = (long)ry*ry;
        int minCX = cx, maxCX = cx, minCY = cy, maxCY = cy;
        auto track = [&](int x, int y) {
            int pxL = std::max(left,  std::min(right,  cx-x));
            int pxR = std::max(left,  std::min(right,  cx+x));
            int pyT = std::max(top,   std::min(bottom, cy-y));
            int pyB = std::max(top,   std::min(bottom, cy+y));
            minCX = std::min(minCX, pxL); maxCX = std::max(maxCX, pxR);
            minCY = std::min(minCY, pyT); maxCY = std::max(maxCY, pyB);
        };
        int x = 0, y = ry;
        long d1 = ry2 - rx2*ry + rx2/4;
        long ddx = 2*ry2*x, ddy = 2*rx2*y;
        while (ddx < ddy) {
            track(x, y); x++; ddx += 2*ry2;
            if (d1 < 0) { d1 += ddx + ry2; }
            else { y--; ddy -= 2*rx2; d1 += ddx - ddy + ry2; }
        }
        long d2 = ry2*((long)x*x+x) + rx2*((long)(y-1)*(y-1)) - rx2*ry2;
        while (y >= 0) {
            track(x, y); y--; ddy -= 2*rx2;
            if (d2 > 0) { d2 += rx2 - ddy; }
            else { x++; ddx += 2*ry2; d2 += ddx - ddy + rx2; }
        }
        return { minCX, minCY, maxCX - minCX, maxCY - minCY };
    }

    void drawFilledRect(SDL_Renderer* renderer, const SDL_Rect* rect, int w, int h) {
        SDL_Rect clipped = {
            std::max(0, rect->x), std::max(0, rect->y),
            0, 0
        };
        int x2 = std::min(w, rect->x + rect->w);
        int y2 = std::min(h, rect->y + rect->h);
        clipped.w = x2 - clipped.x;
        clipped.h = y2 - clipped.y;
        if (clipped.w > 0 && clipped.h > 0)
            SDL_RenderFillRect(renderer, &clipped);
    }

    void drawFilledOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int w, int h) {
        int left = std::min(x0,x1), top = std::min(y0,y1);
        int right = std::max(x0,x1), bottom = std::max(y0,y1);
        if (left == right || top == bottom) return;
        int cx = (left+right)/2, cy = (top+bottom)/2;
        int rx = cx-left, ry = cy-top;
        long rx2 = (long)rx*rx, ry2 = (long)ry*ry;
        // Collect the leftmost and rightmost x for each row, then fill spans.
        std::vector<int> rowL(bottom-top+1, right), rowR(bottom-top+1, left);
        auto plot = [&](int x, int y) {
            int pxL = std::max(left, std::min(right, cx-x));
            int pxR = std::max(left, std::min(right, cx+x));
            for (int py : {cy-y, cy+y}) {
                int row = py - top;
                if (row < 0 || row >= (int)rowL.size()) continue;
                rowL[row] = std::min(rowL[row], pxL);
                rowR[row] = std::max(rowR[row], pxR);
            }
        };
        int x = 0, y = ry;
        long d1 = ry2 - rx2*ry + rx2/4, ddx = 2*ry2*x, ddy = 2*rx2*y;
        while (ddx < ddy) {
            plot(x, y); x++; ddx += 2*ry2;
            if (d1 < 0) { d1 += ddx + ry2; }
            else { y--; ddy -= 2*rx2; d1 += ddx - ddy + ry2; }
        }
        long d2 = ry2*((long)x*x+x) + rx2*((long)(y-1)*(y-1)) - rx2*ry2;
        while (y >= 0) {
            plot(x, y); y--; ddy -= 2*rx2;
            if (d2 > 0) { d2 += rx2 - ddy; }
            else { x++; ddx += 2*ry2; d2 += ddx - ddy + rx2; }
        }
        for (int row = 0; row < (int)rowL.size(); row++) {
            int py = top + row;
            if (py < 0 || py >= h) continue;
            int lx = std::max(0, rowL[row]);
            int rx2c = std::min(w-1, rowR[row]);
            if (lx <= rx2c)
                SDL_RenderDrawLine(renderer, lx, py, rx2c, py);
        }
    }

    void drawMarchingRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
        const int dashLen = 4;
        int x2 = rect->x + rect->w, y2 = rect->y + rect->h;
        int perim = 2 * (rect->w + rect->h);
        for (int p = 0; p < perim; p++) {
            int x, y;
            if      (p < rect->w)             { x = rect->x + p;                   y = rect->y; }
            else if (p < rect->w + rect->h)   { x = x2;                            y = rect->y + (p - rect->w); }
            else if (p < 2*rect->w + rect->h) { x = x2-(p-rect->w-rect->h);        y = y2; }
            else                              { x = rect->x;                        y = y2-(p-2*rect->w-rect->h); }
            bool black = (p / dashLen) % 2 == 0;
            SDL_SetRenderDrawColor(renderer, black?0:255, black?0:255, black?0:255, 255);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    }

// ── Pixel format conversion ───────────────────────────────────────────────────
// SDL ARGB8888 (0xAARRGGBB) <-> stb RGBA8888 (bytes R,G,B,A)

    static std::vector<uint8_t> argbToRGBA(const uint32_t* argb, int w, int h) {
        std::vector<uint8_t> rgba(w * h * 4);
        for (int i = 0; i < w * h; i++) {
            uint32_t px = argb[i];
            rgba[i*4+0] = (px >> 16) & 0xFF; // R
            rgba[i*4+1] = (px >>  8) & 0xFF; // G
            rgba[i*4+2] = (px >>  0) & 0xFF; // B
            rgba[i*4+3] = (px >> 24) & 0xFF; // A
        }
        return rgba;
    }

    static std::vector<uint32_t> rgbaToARGB(const uint8_t* rgba, int w, int h) {
        std::vector<uint32_t> argb(w * h);
        for (int i = 0; i < w * h; i++) {
            uint8_t r=rgba[i*4+0], g=rgba[i*4+1], b=rgba[i*4+2], a=rgba[i*4+3];
            argb[i] = ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|b;
        }
        return argb;
    }

// ── Encode / decode ───────────────────────────────────────────────────────────

    std::vector<uint8_t> encodeJPEG(const uint32_t* argbPixels, int w, int h, int quality) {
        // JPEG is RGB-only; flatten alpha onto white background
        std::vector<uint8_t> rgb(w * h * 3);
        for (int i = 0; i < w * h; i++) {
            uint32_t px = argbPixels[i];
            uint8_t  a  = (px >> 24) & 0xFF;
            uint8_t  r  = (px >> 16) & 0xFF;
            uint8_t  g  = (px >>  8) & 0xFF;
            uint8_t  b  = (px >>  0) & 0xFF;
            // Composite over white
            rgb[i*3+0] = (uint8_t)(r + (255-r)*(255-a)/255);
            rgb[i*3+1] = (uint8_t)(g + (255-g)*(255-a)/255);
            rgb[i*3+2] = (uint8_t)(b + (255-b)*(255-a)/255);
        }
        std::vector<uint8_t> out;
        auto cb = [](void* ctx, void* data, int size) {
            auto* buf = static_cast<std::vector<uint8_t>*>(ctx);
            auto* bytes = static_cast<uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
        };
        stbi_write_jpg_to_func(cb, &out, w, h, 3, rgb.data(), quality);
        return out;
    }

    std::vector<uint8_t> encodePNG(const uint32_t* argbPixels, int w, int h) {
        auto rgba = argbToRGBA(argbPixels, w, h);
        std::vector<uint8_t> out;
        auto cb = [](void* ctx, void* data, int size) {
            auto* buf = static_cast<std::vector<uint8_t>*>(ctx);
            auto* bytes = static_cast<uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
        };
        stbi_write_png_to_func(cb, &out, w, h, 4, rgba.data(), w * 4);
        return out;
    }

    std::vector<uint32_t> decodeImage(const uint8_t* data, int dataLen, int& outW, int& outH) {
        int channels;
        uint8_t* raw = stbi_load_from_memory(data, dataLen, &outW, &outH, &channels, 4);
        if (!raw) return {};
        auto argb = rgbaToARGB(raw, outW, outH);
        stbi_image_free(raw);
        return argb;
    }

// ── Platform clipboard ────────────────────────────────────────────────────────

#if defined(KPEN_CLIPBOARD_MAC)
    // Implemented in ClipboardMac.mm (compiled as Objective-C++ so AppKit
    // types and objc_msgSend link correctly without extra CMake flags).
    // Declarations are in DrawingUtils.h — definitions provided by that TU.

#elif defined(KPEN_CLIPBOARD_WIN)

    static UINT sCFPng = 0;
    static UINT getPngFormat() {
        if (!sCFPng) sCFPng = RegisterClipboardFormatW(L"PNG");
        return sCFPng;
    }

    bool setClipboardImage(const uint32_t* argbPixels, int w, int h) {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        bool ok = false;

        // PNG (lossless, preserves alpha)
        auto png = encodePNG(argbPixels, w, h);
        if (!png.empty()) {
            HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, png.size());
            if (hPng) {
                memcpy(GlobalLock(hPng), png.data(), png.size());
                GlobalUnlock(hPng);
                SetClipboardData(getPngFormat(), hPng);
                ok = true;
            }
        }

        // CF_DIB (broadest compatibility, no alpha)
        int stride = ((w * 3 + 3) & ~3);
        size_t dibSize = sizeof(BITMAPINFOHEADER) + (size_t)stride * h;
        HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, dibSize);
        if (hDib) {
            uint8_t* p = (uint8_t*)GlobalLock(hDib);
            BITMAPINFOHEADER bih = {};
            bih.biSize=sizeof(bih); bih.biWidth=w; bih.biHeight=h;
            bih.biPlanes=1; bih.biBitCount=24; bih.biCompression=BI_RGB;
            memcpy(p, &bih, sizeof(bih));
            uint8_t* dst = p + sizeof(bih);
            for (int row = h-1; row >= 0; row--) {  // bottom-up
                const uint32_t* src = argbPixels + row * w;
                uint8_t* rowDst = dst + (size_t)(h-1-row) * stride;
                for (int col = 0; col < w; col++) {
                    uint32_t px = src[col];
                    rowDst[col*3+0] = (px)      & 0xFF; // B
                    rowDst[col*3+1] = (px >> 8)  & 0xFF; // G
                    rowDst[col*3+2] = (px >> 16) & 0xFF; // R
                }
            }
            GlobalUnlock(hDib);
            SetClipboardData(CF_DIB, hDib);
            ok = true;
        }

        CloseClipboard();
        return ok;
    }

    bool getClipboardImage(std::vector<uint32_t>& outPixels, int& outW, int& outH) {
        if (!OpenClipboard(nullptr)) return false;
        bool got = false;

        // Prefer PNG (has alpha)
        UINT cfPng = getPngFormat();
        if (IsClipboardFormatAvailable(cfPng)) {
            HGLOBAL h = (HGLOBAL)GetClipboardData(cfPng);
            if (h) {
                size_t sz = GlobalSize(h);
                const uint8_t* p = (const uint8_t*)GlobalLock(h);
                if (p && sz > 0) { outPixels = decodeImage(p, (int)sz, outW, outH); got = !outPixels.empty(); }
                GlobalUnlock(h);
            }
        }

        // Fall back to CF_DIB
        if (!got && IsClipboardFormatAvailable(CF_DIB)) {
            HGLOBAL h = (HGLOBAL)GetClipboardData(CF_DIB);
            if (h) {
                const uint8_t* p = (const uint8_t*)GlobalLock(h);
                if (p) {
                    const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)p;
                    int bw = bih->biWidth, bh = std::abs((int)bih->biHeight);
                    bool bottomUp = bih->biHeight > 0;
                    int bpp = bih->biBitCount;
                    if ((bpp==24||bpp==32) && bih->biCompression==BI_RGB) {
                        int srcStride = ((bw*(bpp/8)+3)&~3);
                        const uint8_t* pix = p + bih->biSize;
                        outPixels.resize(bw*bh);
                        for (int row=0; row<bh; row++) {
                            int srcRow = bottomUp ? (bh-1-row) : row;
                            const uint8_t* src = pix + srcRow*srcStride;
                            uint32_t* dst2 = outPixels.data() + row*bw;
                            for (int col=0; col<bw; col++) {
                                uint8_t b=src[col*(bpp/8)], g=src[col*(bpp/8)+1], r=src[col*(bpp/8)+2];
                                uint8_t a=(bpp==32)?src[col*4+3]:255;
                                dst2[col]=((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|b;
                            }
                        }
                        outW=bw; outH=bh; got=true;
                    }
                }
                GlobalUnlock(h);
            }
        }

        CloseClipboard();
        return got;
    }

#else
    bool setClipboardImage(const uint32_t*, int, int) { return false; }
    bool getClipboardImage(std::vector<uint32_t>&, int&, int&) { return false; }
#endif

} // namespace DrawingUtils

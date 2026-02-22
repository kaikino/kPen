#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdlib>

#include "DrawingUtils.h"

// Shared Drawing Helpers
namespace DrawingUtils {
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

    // Accumulates horizontal spans across multiple circle stamps, then flushes in one pass.
    // Dramatically faster than calling drawFillCircle per outline point for thick brushes.
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

        void flush(SDL_Renderer* renderer) {
            for (int row = 0; row < canvasH; row++) {
                for (auto& seg : spans[row])
                    SDL_RenderDrawLine(renderer, seg.first, row, seg.second, row);
            }
        }
    };

    void drawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, int size, int w, int h) {
        if (size <= 1) {
            SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
            return;
        }
        int radius = size / 2;
        SpanBuffer spans(w, h);
        int dx = std::abs(x2 - x1);
        int dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;
        while (true) {
            spans.addCircle(x1, y1, radius);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx)  { err += dx; y1 += sy; }
        }
        spans.flush(renderer);
    }

    void drawRect(SDL_Renderer* renderer, const SDL_Rect* rect, int size, int w, int h) {
        drawLine(renderer, rect->x, rect->y, rect->x + rect->w, rect->y, size, w, h); 
        drawLine(renderer, rect->x + rect->w, rect->y, rect->x + rect->w, rect->y + rect->h, size, w, h); 
        drawLine(renderer, rect->x + rect->w, rect->y + rect->h, rect->x, rect->y + rect->h, size, w, h); 
        drawLine(renderer, rect->x, rect->y + rect->h, rect->x, rect->y, size, w, h); 
    }

    void drawOval(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int size, int w, int h) {
        int left   = std::min(x0, x1);
        int top    = std::min(y0, y1);
        int right  = std::max(x0, x1);
        int bottom = std::max(y0, y1);

        if (left == right || top == bottom) return;

        int cx = (left + right) / 2;
        int cy = (top + bottom) / 2;
        int rx = cx - left;
        int ry = cy - top;
        int brushRadius = size / 2;

        long rx2 = (long)rx * rx, ry2 = (long)ry * ry;

        auto plot = [&](SpanBuffer& spans, int x, int y) {
            spans.addCircle(cx + x, cy + y, brushRadius);
            spans.addCircle(cx - x, cy + y, brushRadius);
            spans.addCircle(cx + x, cy - y, brushRadius);
            spans.addCircle(cx - x, cy - y, brushRadius);
        };

        SpanBuffer spans(w, h);

        // Midpoint ellipse algorithm — decides pixel placement based on
        // which candidate is closer to the true ellipse, giving rounder
        // results at small radii compared to the sqrt-based approach.

        // Region 1: slope magnitude < 1 (step in x)
        int x = 0, y = ry;
        long d1 = ry2 - rx2 * ry + rx2 / 4;
        long dx = 2 * ry2 * x, dy = 2 * rx2 * y;
        while (dx < dy) {
            plot(spans, x, y);
            x++;
            dx += 2 * ry2;
            if (d1 < 0) {
                d1 += dx + ry2;
            } else {
                y--;
                dy -= 2 * rx2;
                d1 += dx - dy + ry2;
            }
        }

        // Region 2: slope magnitude >= 1 (step in y)
        long d2 = ry2 * ((long)(x) * x + x) + rx2 * ((long)(y - 1) * (y - 1)) - rx2 * ry2;
        while (y >= 0) {
            plot(spans, x, y);
            y--;
            dy -= 2 * rx2;
            if (d2 > 0) {
                d2 += rx2 - dy;
            } else {
                x++;
                dx += 2 * ry2;
                d2 += dx - dy + rx2;
            }
        }
        spans.flush(renderer);
    }

    // Alternating black/white dashes around the full perimeter (marching ants style).
    // Always visible regardless of what color is underneath.
    void drawMarchingRect(SDL_Renderer* renderer, const SDL_Rect* rect) {
        const int dashLen = 4;
        int x2 = rect->x + rect->w, y2 = rect->y + rect->h;
        int perim = 2 * (rect->w + rect->h);
        for (int p = 0; p < perim; p++) {
            int x, y;
            if      (p < rect->w)                 { x = rect->x + p;                       y = rect->y; }
            else if (p < rect->w + rect->h)       { x = x2;                                y = rect->y + (p - rect->w); }
            else if (p < 2*rect->w + rect->h)     { x = x2 - (p - rect->w - rect->h);     y = y2; }
            else                                  { x = rect->x;                           y = y2 - (p - 2*rect->w - rect->h); }
            bool black = (p / dashLen) % 2 == 0;
            SDL_SetRenderDrawColor(renderer, black ? 0 : 255, black ? 0 : 255, black ? 0 : 255, 255);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    }
}

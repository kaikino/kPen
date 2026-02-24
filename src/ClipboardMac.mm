// ClipboardMac.mm — Objective-C++ implementation of macOS image clipboard.
// Compiled as ObjC++ (.mm) so AppKit types and objc_msgSend are available
// without any extra linker flags beyond -framework AppKit (auto-linked).

#import <AppKit/AppKit.h>
#include <vector>
#include <cstdint>
#include "DrawingUtils.h"

namespace DrawingUtils {

bool setClipboardImage(const uint32_t* argbPixels, int w, int h) {
    // Encode to PNG (preserves alpha)
    auto png = encodePNG(argbPixels, w, h);
    if (png.empty()) return false;

    NSData* data = [NSData dataWithBytes:png.data() length:png.size()];
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setData:data forType:NSPasteboardTypePNG];
}

bool getClipboardImage(std::vector<uint32_t>& outPixels, int& outW, int& outH) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];

    // Try PNG first (has alpha), then TIFF (written by most macOS apps)
    NSData* data = [pb dataForType:NSPasteboardTypePNG];
    if (!data) data = [pb dataForType:NSPasteboardTypeTIFF];
    if (!data || [data length] == 0) return false;

    const uint8_t* bytes = (const uint8_t*)[data bytes];
    outPixels = decodeImage(bytes, (int)[data length], outW, outH);
    return !outPixels.empty();
}

} // namespace DrawingUtils

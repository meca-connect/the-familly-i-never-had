#pragma once

#include <stddef.h>
#include <stdint.h>

namespace family {

struct ReferenceInfo {
    uint16_t width = 0;
    uint16_t height = 0;
};

constexpr size_t MAX_REFERENCE_BYTES = 1048576;

// Header-only admission check, not a JPEG decoder. Accepts 8-bit grayscale or
// three-component SOF0/SOF1/SOF2 frames. The caller must still decode the image:
// tables, component-record semantics and bytes after the first SOF are not
// validated here (except for the required final EOI). No allocation or I/O.
// On rejection, info is reset so callers cannot accidentally use stale bounds.
inline bool inspectReferenceJpeg(const uint8_t* data, size_t size,
                                ReferenceInfo& info) {
    info = {};
    if (!data || size < 4 || size > MAX_REFERENCE_BYTES ||
        data[0] != 0xFF || data[1] != 0xD8 ||
        data[size - 2] != 0xFF || data[size - 1] != 0xD9) {
        return false;
    }
    size_t pos = 2;
    while (pos < size - 2) {
        if (data[pos++] != 0xFF) return false;
        while (pos < size - 2 && data[pos] == 0xFF) ++pos;
        if (pos >= size - 2) return false;
        const uint8_t marker = data[pos++];
        const bool supportedFrame = marker == 0xC0 || marker == 0xC1 || marker == 0xC2;
        const bool metadataOrTable = (marker >= 0xE0 && marker <= 0xEF) ||
                                     marker == 0xFE || marker == 0xDB ||
                                     marker == 0xC4 || marker == 0xDD;
        if (!supportedFrame && !metadataOrTable) return false;
        if (size - 2 - pos < 2) return false;
        const size_t length = (size_t(data[pos]) << 8) | data[pos + 1];
        if (length < 2 || length > size - 2 - pos) return false;
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            if (length < 8 || data[pos + 2] != 8) return false;
            const uint8_t components = data[pos + 7];
            if ((components != 1 && components != 3) ||
                length != size_t(8) + size_t(3) * components) {
                return false;
            }
            const uint16_t height = uint16_t((uint16_t(data[pos + 3]) << 8) | data[pos + 4]);
            const uint16_t width = uint16_t((uint16_t(data[pos + 5]) << 8) | data[pos + 6]);
            if (width < 128 || height < 128 || width > 1024 || height > 1024) {
                return false;
            }
            info.width = width;
            info.height = height;
            return true;
        }
        pos += length;
    }
    return false;
}

}  // namespace family

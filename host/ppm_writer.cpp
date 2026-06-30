/**
 * @file host/ppm_writer.cpp
 * @brief Binary PPM (P6) writer for XRGB8888 pixel buffers
 *
 * See ppm_writer.hpp. Each XRGB8888 pixel (0x00RRGGBB) is emitted as 3 bytes
 * R,G,B (the X/alpha byte is dropped). memcpy per pixel avoids aliasing UB and
 * keeps the value-level shift endian-agnostic.
 */

#include "ppm_writer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace cinux::gui {

bool write_ppm(const char* path, uint32_t width, uint32_t height, const void* pixels,
               uint32_t stride_bytes) {
    if (pixels == nullptr) {
        return false;
    }
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", width, height);

    const uint8_t* base = static_cast<const uint8_t*>(pixels);
    bool           ok   = true;
    for (uint32_t y = 0; y < height && ok; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t pv;
            std::memcpy(&pv,
                        base + static_cast<size_t>(y) * stride_bytes +
                            static_cast<size_t>(x) * 4u,
                        4u);
            const uint8_t rgb[3] = {static_cast<uint8_t>((pv >> 16) & 0xFFu),
                                    static_cast<uint8_t>((pv >> 8) & 0xFFu),
                                    static_cast<uint8_t>(pv & 0xFFu)};
            if (std::fwrite(rgb, 1, 3, f) != 3u) {
                ok = false;
                break;
            }
        }
    }
    std::fclose(f);
    return ok;
}

}  // namespace cinux::gui

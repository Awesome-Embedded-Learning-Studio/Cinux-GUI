/**
 * @file host/offscreen_host_main.cpp
 * @brief Offscreen host: paints a static scene + dumps one PPM frame
 *
 * The first "you can see it" probe of the core render pipeline. Builds a
 * GuiCore over a hand-filled Host table whose render_frame paints a static
 * desktop scene (background + window with title bar + multi-line text +
 * cursor) into the CORE-owned staging buffer via swraster + PsfFont, then dumps
 * that staging buffer to a PPM file for human inspection.
 *
 * No input, no real display, no interaction (that is P0-b3). This proves the
 * pump -> staging -> swraster -> glyph_blit -> PPM path end to end, standalone.
 *
 * Built only standalone (this dir is the build root). NOT built inside a host
 * tree.
 */

#include <cstdint>
#include <cstdio>

#include "font.hpp"      // PsfFont
#include "gui_core.hpp"  // GuiCore
#include "host.hpp"      // Host, Frame, PixelFormat
#include "ppm_writer.hpp"  // write_ppm
#include "swraster.hpp"  // Surface, ClipRect, fill_rect, draw_line, glyph_blit

namespace {

using namespace cinux::gui;

// ---- display geometry ----
constexpr uint32_t kW = 320, kH = 240;

// ---- palette (XRGB8888, 0x00RRGGBB) ----
constexpr uint32_t kBg        = 0x0018182Au;
constexpr uint32_t kWinFace   = 0x00C8C8C8u;
constexpr uint32_t kWinEdge   = 0x00404040u;
constexpr uint32_t kTitleBar  = 0x003060A0u;
constexpr uint32_t kTitleText = 0x00FFFFFFu;
constexpr uint32_t kText      = 0x00181818u;
constexpr uint32_t kCursor    = 0x00FFFFFFu;

// ---- window geometry ----
constexpr int32_t  kWinX    = 60;
constexpr int32_t  kWinY    = 40;
constexpr uint32_t kWinW    = 200;
constexpr uint32_t kWinH    = 160;
constexpr uint32_t kTitleH  = 16;  // one glyph row
constexpr int32_t  kCursorX = 180;
constexpr int32_t  kCursorY = 144;

PsfFont g_font;  // initialised in main; borrowed by render_frame

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: " __VA_ARGS__);                                 \
            std::printf("\n");                                                 \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// ---- small paint helpers (kept here -- b2 scope, not promoted to swraster) ----
void draw_rect_outline(Surface& s, int32_t x, int32_t y, uint32_t w, uint32_t h,
                       uint32_t color, const ClipRect* clip) {
    const int32_t x1 = x + static_cast<int32_t>(w) - 1;
    const int32_t y1 = y + static_cast<int32_t>(h) - 1;
    draw_line(s, x, y, x1, y, color, clip);    // top
    draw_line(s, x, y1, x1, y1, color, clip);  // bottom
    draw_line(s, x, y, x, y1, color, clip);    // left
    draw_line(s, x1, y, x1, y1, color, clip);  // right
}

void draw_text(Surface& s, const PsfFont& font, const char* str, int32_t x, int32_t y,
               uint32_t color, const ClipRect* clip) {
    int32_t cx = x;
    int32_t cy = y;
    for (const char* p = str; *p != '\0'; p++) {
        if (*p == '\n') {
            cx = x;
            cy += static_cast<int32_t>(font.height());
            continue;
        }
        glyph_blit(s, cx, cy, font.glyph(static_cast<uint8_t>(*p)), font.width(),
                   font.height(), color, clip);
        cx += static_cast<int32_t>(font.width());
    }
}

// ---- host callbacks ----
bool offscreen_poll_event(void* /*ctx*/, EventHeader* /*out*/, uint16_t /*cap*/) {
    return false;  // no input in the static-scene probe
}

void offscreen_dispatch_event(void* /*ctx*/, const EventHeader* /*ev*/,
                              const void* /*payload*/) {}

void offscreen_render_frame(void* /*ctx*/, Frame* frame) {
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    fill_rect(s, 0, 0, kW, kH, kBg, nullptr);                              // screen bg
    fill_rect(s, kWinX, kWinY, kWinW, kWinH, kWinFace, nullptr);           // window face
    fill_rect(s, kWinX, kWinY, kWinW, kTitleH, kTitleBar, nullptr);        // title bar
    draw_rect_outline(s, kWinX, kWinY, kWinW, kWinH, kWinEdge, nullptr);   // window border
    draw_text(s, g_font, "Cinux", kWinX + 8, kWinY, kTitleText, nullptr);  // title text
    draw_text(s, g_font, "Hello\nCinux-GUI", kWinX + 8,
              kWinY + static_cast<int32_t>(kTitleH) + 8, kText, nullptr);  // body text
    fill_rect(s, kCursorX, kCursorY, 4, 4, kCursor, nullptr);             // cursor
    frame->rects[0] = Rect{0, 0, static_cast<int32_t>(kW), static_cast<int32_t>(kH)};
    frame->count    = 1;
}

void offscreen_flush(void* /*ctx*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                     const void* /*pixels*/, uint32_t /*stride*/, PixelFormat /*fmt*/) {
    // offscreen: no real display. The staging buffer is dumped after pump().
}

Host make_host() {
    Host h{};
    h.core.poll_event     = offscreen_poll_event;
    h.core.dispatch_event = offscreen_dispatch_event;
    h.core.render_frame   = offscreen_render_frame;
    h.core.flush          = offscreen_flush;
    h.desktop             = nullptr;
    h.ctx                 = nullptr;
    return h;
}

// Verify the scene actually painted into the staging buffer (structure pixels).
int verify_scene(const Surface& s) {
    const uint32_t* px  = static_cast<const uint32_t*>(s.pixels);
    const uint32_t  ppr = s.stride_bytes / 4u;
    CHECK(px[0] == kBg, "bg pixel 0x%08X (expected 0x%08X)", px[0], kBg);
    const uint32_t wc =
        px[static_cast<uint32_t>(kWinY + static_cast<int32_t>(kWinH) / 2) * ppr +
           static_cast<uint32_t>(kWinX + static_cast<int32_t>(kWinW) / 2)];
    CHECK(wc == kWinFace, "window-centre pixel 0x%08X (expected 0x%08X)", wc, kWinFace);
    const uint32_t tb =
        px[static_cast<uint32_t>(kWinY + static_cast<int32_t>(kTitleH) / 2) * ppr +
           static_cast<uint32_t>(kWinX + static_cast<int32_t>(kWinW) / 2)];
    CHECK(tb == kTitleBar, "title-bar pixel 0x%08X (expected 0x%08X)", tb, kTitleBar);
    return 0;
}

}  // namespace

int main() {
    g_font.init();
    CHECK(g_font.ready(), "font not ready");

    Host    host = make_host();
    GuiCore core(&host, kW, kH, PixelFormat::kXrgb8888);
    core.pump();

    // 1. verify the scene painted into the core-owned staging buffer.
    if (verify_scene(core.staging()) != 0) {
        return 1;
    }

    // 2. dump the staging buffer to PPM for human inspection.
    const char* path = "offscreen_frame.ppm";
    CHECK(write_ppm(path, kW, kH, core.staging().pixels, core.staging().stride_bytes),
          "write_ppm(%s) failed", path);

    std::printf("offscreen-dump: OK -> %s (%ux%u: bg + window + title bar + "
                "Hello/Cinux-GUI + cursor)\n", path, kW, kH);
    return 0;
}

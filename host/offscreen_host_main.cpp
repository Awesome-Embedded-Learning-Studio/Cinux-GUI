/**
 * @file host/offscreen_host_main.cpp
 * @brief Offscreen host: paints a static scene (via the host-neutral
 *        Compositor) + dumps one PPM frame (P0-b2, refactored P2-d)
 *
 * The first "you can see it" probe of the core render pipeline. Builds a
 * GuiCore over a hand-filled Host table whose render_frame hands a DATA Scene
 * to the host-neutral compose() (P2-d: the scene is now data; the paint lives
 * in core/compositor, shared across the offscreen/replay/fbdev hosts). Dumps
 * the core-owned staging buffer to PPM for human inspection.
 *
 * No input, no real display, no interaction (that is the replay host). Built
 * standalone only.
 */

#include <cstdint>
#include <cstdio>

#include "compositor.hpp"  // compose
#include "font.hpp"        // PsfFont
#include "gui_core.hpp"    // GuiCore
#include "host.hpp"        // Host, Frame, PixelFormat
#include "ppm_writer.hpp"  // write_ppm
#include "scene.hpp"       // Scene, Window, window_set_*
#include "swraster.hpp"    // Surface

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

PsfFont g_font;   // initialised in main; borrowed by render_frame
Scene   g_scene;  // initialised in main; the DATA compose() paints

#define CHECK(cond, ...)                       \
    do {                                       \
        if (!(cond)) {                         \
            std::printf("FAIL: " __VA_ARGS__); \
            std::printf("\n");                 \
            return 1;                          \
        }                                      \
    } while (0)

// ---- host callbacks ----
bool offscreen_poll_event(void* /*ctx*/, EventHeader* /*out*/, uint16_t /*cap*/) {
    return false;  // no input in the static-scene probe
}

void offscreen_dispatch_event(void* /*ctx*/, const EventHeader* /*ev*/, const void* /*payload*/) {}

void offscreen_render_frame(void* /*ctx*/, Frame* frame) {
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    compose(s, g_scene, g_font);  // host-neutral paint of the whole scene
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
    const uint32_t wc = px[static_cast<uint32_t>(kWinY + static_cast<int32_t>(kWinH) / 2) * ppr +
                           static_cast<uint32_t>(kWinX + static_cast<int32_t>(kWinW) / 2)];
    CHECK(wc == kWinFace, "window-centre pixel 0x%08X (expected 0x%08X)", wc, kWinFace);
    const uint32_t tb = px[static_cast<uint32_t>(kWinY + static_cast<int32_t>(kTitleH) / 2) * ppr +
                           static_cast<uint32_t>(kWinX + static_cast<int32_t>(kWinW) / 2)];
    CHECK(tb == kTitleBar, "title-bar pixel 0x%08X (expected 0x%08X)", tb, kTitleBar);
    return 0;
}

}  // namespace

int main() {
    g_font.init();
    CHECK(g_font.ready(), "font not ready");

    // Build the scene (window + title bar + text + cursor) as DATA; compose()
    // paints it into the core-owned staging buffer via swraster.
    g_scene.bg_color = kBg;
    Window w{};
    w.x                = kWinX;
    w.y                = kWinY;
    w.w                = kWinW;
    w.h                = kWinH;
    w.face_color       = kWinFace;
    w.edge_color       = kWinEdge;
    w.titlebar_color   = kTitleBar;
    w.titlebar_height  = kTitleH;
    w.title_text_color = kTitleText;
    w.body_text_color  = kText;
    window_set_title(w, "Cinux");
    window_set_body(w, "Hello\nCinux-GUI");
    scene_add_window(g_scene, w);
    g_scene.cursor.x     = kCursorX;
    g_scene.cursor.y     = kCursorY;
    g_scene.cursor.w     = 4;
    g_scene.cursor.h     = 4;
    g_scene.cursor.color = kCursor;

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

    std::printf(
        "offscreen-dump: OK -> %s (%ux%u: bg + window + title bar + "
        "Hello/Cinux-GUI + cursor)\n",
        path, kW, kH);
    return 0;
}

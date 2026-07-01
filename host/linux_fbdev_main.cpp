/**
 * @file host/linux_fbdev_main.cpp
 * @brief Linux fbdev+evdev host (P1 Probe-1) -- the second real host (P2-d:
 *        scene data + core Compositor, no host-side paint)
 *
 * Opens /dev/fb0 (mmap display) + /dev/input/event* (evdev pointer) and fills
 * the Host ABI table with callbacks that wire those devices to the SAME core
 * (pump / staging / Compositor) that drives the offscreen + replay hosts.
 * render_frame syncs host pointer/window state into a Scene and hands it to
 * the stateful Compositor, which paints + reports the dirty region -- the host
 * no longer hand-writes any paint or dirty logic. Result: a real Linux GUI you
 * drive with the mouse, proving the Host ABI seam is clean (swap the table
 * fill, core unchanged).
 *
 * Manual smoke ONLY (needs real /dev/fb0 + /dev/input/event*): NOT a ctest. Run
 * under QEMU with a framebuffer + pointing device, e.g.:
 *     timeout 40 ./fbdev-host
 * watch the VNC display, move/drag the mouse, record the result in notes.
 */

#include <unistd.h>  // usleep

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "compositor.hpp"  // Compositor
#include "evdev_device.hpp"
#include "fbdev_device.hpp"
#include "font.hpp"
#include "gui_core.hpp"
#include "host.hpp"
#include "region.hpp"
#include "scene.hpp"     // Scene, Window, window_set_*
#include "swraster.hpp"  // Surface

namespace {

using namespace cinux::gui;

constexpr uint32_t kCursorSize = 4;
constexpr uint32_t kWinW = 200, kWinH = 160, kTitleH = 16;

// palette (XRGB8888) -- same scene as offscreen/replay hosts
constexpr uint32_t kBg        = 0x0018182Au;
constexpr uint32_t kWinFace   = 0x00C8C8C8u;
constexpr uint32_t kWinEdge   = 0x00404040u;
constexpr uint32_t kTitleBar  = 0x003060A0u;
constexpr uint32_t kTitleText = 0x00FFFFFFu;
constexpr uint32_t kText      = 0x00181818u;
constexpr uint32_t kCursor    = 0x00FFFFFFu;

struct HostState {
    FbdevDevice&   fb;
    EvdevDevice&   ev;
    const PsfFont& font;
    int32_t        cursor_x;
    int32_t        cursor_y;
    int32_t        win_x;
    int32_t        win_y;
    bool           dragging;
    Scene          scene;  // P2-d: synced from cursor/win each frame
    Compositor     comp;   // P2-d: owns the frame-to-frame dirty diff
};

// Build the initial scene (positions only change later; palette/text constant).
Scene make_scene(int32_t wx, int32_t wy, int32_t cx, int32_t cy) {
    Scene sc{};
    sc.bg_color = kBg;
    Window w{};
    w.x                = wx;
    w.y                = wy;
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
    scene_add_window(sc, w);
    sc.cursor.x     = cx;
    sc.cursor.y     = cy;
    sc.cursor.w     = kCursorSize;
    sc.cursor.h     = kCursorSize;
    sc.cursor.color = kCursor;
    return sc;
}

bool in_title_bar(const HostState& st, int32_t x, int32_t y) {
    return x >= st.win_x && x < st.win_x + static_cast<int32_t>(kWinW) && y >= st.win_y &&
           y < st.win_y + static_cast<int32_t>(kTitleH);
}

// ---- host callbacks ----
bool fbdev_poll_event(void* ctx, EventHeader* out, uint16_t cap) {
    auto* st = static_cast<HostState*>(ctx);
    if (cap < sizeof(EventHeader) + sizeof(PointerPayload)) {
        return false;
    }
    EvdevFrame f = st->ev.read_frame();
    if (!f.valid) {
        return false;
    }
    out->magic   = kEventMagic;
    out->version = kAbiVersion;
    out->type    = EventCode::kPointer;
    out->flags   = 0;
    PointerPayload p;
    p.kind    = f.kind;
    p.x       = f.x;
    p.y       = f.y;
    p.dx      = 0;
    p.dy      = 0;
    p.buttons = f.buttons;
    std::memcpy(reinterpret_cast<uint8_t*>(out) + sizeof(EventHeader), &p, sizeof(p));
    out->payload_len = sizeof(p);
    return true;
}

void fbdev_dispatch_event(void* ctx, const EventHeader* ev, const void* payload) {
    auto* st = static_cast<HostState*>(ctx);
    if (ev->magic != kEventMagic || ev->type != EventCode::kPointer) {
        return;
    }
    PointerPayload p;
    std::memcpy(&p, payload, sizeof(p));
    const int32_t old_cx = st->cursor_x;
    const int32_t old_cy = st->cursor_y;
    st->cursor_x         = p.x;
    st->cursor_y         = p.y;
    switch (p.kind) {
        case kPointerKindDown:
            if (in_title_bar(*st, p.x, p.y)) {
                st->dragging = true;
            }
            break;
        case kPointerKindUp:
            st->dragging = false;
            break;
        case kPointerKindMove:
            if (st->dragging) {
                st->win_x += p.x - old_cx;
                st->win_y += p.y - old_cy;
            }
            break;
        default:
            break;
    }
}

void fbdev_render_frame(void* ctx, Frame* frame) {
    auto* st = static_cast<HostState*>(ctx);
    // sync host state -> scene (positions only; palette/text stay constant)
    st->scene.windows[0].x = st->win_x;
    st->scene.windows[0].y = st->win_y;
    st->scene.cursor.x     = st->cursor_x;
    st->scene.cursor.y     = st->cursor_y;
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    Region  dirty;
    st->comp.compose(s, st->scene, st->font, &dirty);  // paint + frame diff
    const uint32_t n = dirty.count();
    for (uint32_t i = 0u; i < n && i < frame->max_rects; i++) {
        frame->rects[i] = dirty.rects()[i];
    }
    frame->count = n;  // 0 = idle -> pump flushes nothing
}

void fbdev_flush(void* ctx, int x, int y, int w, int h, const void* pixels, uint32_t stride,
                 PixelFormat /*fmt*/) {
    auto* st = static_cast<HostState*>(ctx);
    st->fb.blit_rect(x, y, w, h, pixels, stride);
}

Host make_host(HostState& st) {
    Host h{};
    h.core.poll_event     = fbdev_poll_event;
    h.core.dispatch_event = fbdev_dispatch_event;
    h.core.render_frame   = fbdev_render_frame;
    h.core.flush          = fbdev_flush;
    h.desktop             = nullptr;
    h.ctx                 = &st;
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    PsfFont font;
    font.init();
    if (!font.ready()) {
        std::printf("fbdev-host: font init failed\n");
        return 1;
    }

    FbdevDevice fb;
    const char* fb_path = (argc > 1) ? argv[1] : "/dev/fb0";
    if (!fb.open(fb_path)) {
        std::printf("fbdev-host: cannot open %s\n", fb_path);
        return 1;
    }
    std::printf("fbdev-host: fb %ux%u stride %u\n", fb.width(), fb.height(), fb.stride_bytes());

    const char*   evdev_path = (argc > 2) ? argv[2] : "/dev/input/event0";
    const int32_t cx0        = static_cast<int32_t>(fb.width()) / 2;
    const int32_t cy0        = static_cast<int32_t>(fb.height()) / 2;
    EvdevDevice   ev;
    if (!ev.open(evdev_path, cx0, cy0)) {
        std::printf("fbdev-host: cannot open %s\n", evdev_path);
        return 1;
    }

    const int32_t wx0 = static_cast<int32_t>(fb.width()) / 2 - 100;
    const int32_t wy0 = static_cast<int32_t>(fb.height()) / 2 - 80;
    HostState     st{fb, ev, font, cx0, cy0, wx0, wy0, false, {}, {}};
    st.scene = make_scene(wx0, wy0, cx0, cy0);

    Host    host = make_host(st);
    GuiCore core(&host, fb.width(), fb.height(), fb.format());

    std::printf(
        "fbdev-host: running -- move/drag the mouse (VNC). "
        "External `timeout` or Ctrl-C exits.\n");
    std::fflush(stdout);
    for (;;) {
        core.pump();
        ::usleep(16000);  // ~60 fps cap; idle frames flush nothing
    }
    return 0;
}

/**
 * @file host/linux_fbdev_main.cpp
 * @brief Linux fbdev+evdev host (P1 Probe-1) -- the second real host
 *
 * Opens /dev/fb0 (mmap display) + /dev/input/event* (evdev pointer) and fills
 * the Host ABI table with callbacks that wire those devices to the SAME core
 * (pump / staging / swraster / region) that drove the offscreen + replay hosts.
 * Result: a real Linux GUI you drive with the mouse -- proving the Host ABI seam
 * is clean (swap the table fill, core unchanged).
 *
 * Manual smoke ONLY (needs real /dev/fb0 + /dev/input/event*): NOT a ctest. Run
 * under QEMU with a framebuffer + pointing device, e.g.:
 *     timeout 40 ./fbdev-host
 * watch the VNC display, move/drag the mouse, record the result in notes.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>  // usleep

#include "evdev_device.hpp"
#include "fbdev_device.hpp"
#include "font.hpp"
#include "gui_core.hpp"
#include "host.hpp"
#include "region.hpp"
#include "swraster.hpp"

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
    FbdevDevice&  fb;
    EvdevDevice&  ev;
    const PsfFont& font;
    int32_t cursor_x, cursor_y;
    int32_t win_x, win_y;
    bool    dragging;
    int32_t prev_cx, prev_cy, prev_wx, prev_wy;
    bool    first_frame;
};

void draw_rect_outline(Surface& s, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color,
                       const ClipRect* clip) {
    const int32_t x1 = x + static_cast<int32_t>(w) - 1;
    const int32_t y1 = y + static_cast<int32_t>(h) - 1;
    draw_line(s, x, y, x1, y, color, clip);
    draw_line(s, x, y1, x1, y1, color, clip);
    draw_line(s, x, y, x, y1, color, clip);
    draw_line(s, x1, y, x1, y1, color, clip);
}
void draw_text(Surface& s, const PsfFont& font, const char* str, int32_t x, int32_t y,
               uint32_t color, const ClipRect* clip) {
    int32_t cx = x, cy = y;
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

Rect cursor_rect(int32_t x, int32_t y) {
    return Rect{x, y, x + static_cast<int32_t>(kCursorSize),
                y + static_cast<int32_t>(kCursorSize)};
}
Rect win_rect(int32_t x, int32_t y) {
    return Rect{x, y, x + static_cast<int32_t>(kWinW), y + static_cast<int32_t>(kWinH)};
}
bool in_title_bar(const HostState& st, int32_t x, int32_t y) {
    return x >= st.win_x && x < st.win_x + static_cast<int32_t>(kWinW) && y >= st.win_y &&
           y < st.win_y + static_cast<int32_t>(kTitleH);
}

void paint_scene(Surface& s, const HostState& st) {
    fill_rect(s, 0, 0, s.width, s.height, kBg, nullptr);
    fill_rect(s, st.win_x, st.win_y, kWinW, kWinH, kWinFace, nullptr);
    fill_rect(s, st.win_x, st.win_y, kWinW, kTitleH, kTitleBar, nullptr);
    draw_rect_outline(s, st.win_x, st.win_y, kWinW, kWinH, kWinEdge, nullptr);
    draw_text(s, st.font, "Cinux", st.win_x + 8, st.win_y, kTitleText, nullptr);
    draw_text(s, st.font, "Hello\nCinux-GUI", st.win_x + 8,
              st.win_y + static_cast<int32_t>(kTitleH) + 8, kText, nullptr);
    fill_rect(s, st.cursor_x, st.cursor_y, kCursorSize, kCursorSize, kCursor, nullptr);
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
    out->magic = kEventMagic;
    out->version = kAbiVersion;
    out->type = EventCode::kPointer;
    out->flags = 0;
    PointerPayload p;
    p.kind = f.kind;
    p.x = f.x;
    p.y = f.y;
    p.dx = 0;
    p.dy = 0;
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
    st->cursor_x = p.x;
    st->cursor_y = p.y;
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
    const bool cm = st->cursor_x != st->prev_cx || st->cursor_y != st->prev_cy;
    const bool wm = st->win_x != st->prev_wx || st->win_y != st->prev_wy;
    if (!st->first_frame && !cm && !wm) {
        frame->count = 0;  // idle
        return;
    }
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    paint_scene(s, *st);
    Region dirty;
    if (st->first_frame) {
        dirty.add(Rect{0, 0, static_cast<int32_t>(s.width), static_cast<int32_t>(s.height)});
        st->first_frame = false;
    } else {
        if (cm) {
            dirty.add(cursor_rect(st->prev_cx, st->prev_cy));
            dirty.add(cursor_rect(st->cursor_x, st->cursor_y));
        }
        if (wm) {
            dirty.add(win_rect(st->prev_wx, st->prev_wy));
            dirty.add(win_rect(st->win_x, st->win_y));
        }
    }
    st->prev_cx = st->cursor_x;
    st->prev_cy = st->cursor_y;
    st->prev_wx = st->win_x;
    st->prev_wy = st->win_y;
    const uint32_t n = dirty.count();
    for (uint32_t i = 0; i < n && i < frame->max_rects; i++) {
        frame->rects[i] = dirty.rects()[i];
    }
    frame->count = n;
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
    const char*  fb_path = (argc > 1) ? argv[1] : "/dev/fb0";
    if (!fb.open(fb_path)) {
        std::printf("fbdev-host: cannot open %s\n", fb_path);
        return 1;
    }
    std::printf("fbdev-host: fb %ux%u stride %u\n", fb.width(), fb.height(), fb.stride_bytes());

    const char* evdev_path = (argc > 2) ? argv[2] : "/dev/input/event0";
    const int32_t cx0 = static_cast<int32_t>(fb.width()) / 2;
    const int32_t cy0 = static_cast<int32_t>(fb.height()) / 2;
    EvdevDevice ev;
    if (!ev.open(evdev_path, cx0, cy0)) {
        std::printf("fbdev-host: cannot open %s\n", evdev_path);
        return 1;
    }

    const int32_t wx0 = static_cast<int32_t>(fb.width()) / 2 - 100;
    const int32_t wy0 = static_cast<int32_t>(fb.height()) / 2 - 80;
    HostState st{fb, ev, font, cx0, cy0, wx0, wy0, false, cx0, cy0, wx0, wy0, true};

    Host    host = make_host(st);
    GuiCore core(&host, fb.width(), fb.height(), fb.format());

    std::printf("fbdev-host: running -- move/drag the mouse (VNC). "
                "External `timeout` or Ctrl-C exits.\n");
    std::fflush(stdout);
    for (;;) {
        core.pump();
        ::usleep(16000);  // ~60 fps cap; idle frames flush nothing
    }
    return 0;
}

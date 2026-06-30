/**
 * @file host/replay_host_main.cpp
 * @brief Replay host: scripted pointer events drive a draggable scene (P0-b3)
 *
 * The interaction probe. Feeds a hardcoded replay script (pointer move/down/up)
 * through poll_event -> dispatch_event -- one event per frame -- while a tiny
 * host state machine moves the cursor and drags the window by the title bar.
 * render_frame paints the dynamic scene and reports the TRUE dirty region each
 * frame (full screen on the first frame; only the old+new cursor/window boxes
 * afterwards; idle -- count==0 -- when nothing moved). The harness then asserts
 * end-state geometry AND dirty-region discipline (no full-screen repaint after
 * the first frame; idle frames flush nothing; changed pixels are flushed).
 * Key frames are dumped to PPM for human inspection.
 *
 * This is the first test that drives the core with REAL input through the event
 * ABI, standalone. No real device/display. Built standalone only.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "event.hpp"         // EventHeader, EventCode, kEventMagic, kAbiVersion
#include "event_payload.hpp"  // PointerPayload, kPointerKind*
#include "font.hpp"          // PsfFont
#include "gui_core.hpp"      // GuiCore
#include "host.hpp"          // Host, Frame, PixelFormat
#include "ppm_writer.hpp"    // write_ppm
#include "region.hpp"        // Rect, Region
#include "swraster.hpp"      // Surface, ClipRect, fill_rect, draw_line, glyph_blit

namespace {

using namespace cinux::gui;

// ---- display + window geometry ----
constexpr uint32_t kW = 320, kH = 240;
constexpr int32_t  kInitWinX = 60, kInitWinY = 40;
constexpr uint32_t kWinW = 200, kWinH = 160, kTitleH = 16;
constexpr int32_t  kInitCursorX = 160, kInitCursorY = 144;
constexpr uint32_t kCursorSize = 4;

// ---- palette (XRGB8888) ----
constexpr uint32_t kBg        = 0x0018182Au;
constexpr uint32_t kWinFace   = 0x00C8C8C8u;
constexpr uint32_t kWinEdge   = 0x00404040u;
constexpr uint32_t kTitleBar  = 0x003060A0u;
constexpr uint32_t kTitleText = 0x00FFFFFFu;
constexpr uint32_t kText      = 0x00181818u;
constexpr uint32_t kCursor    = 0x00FFFFFFu;

// ---- replay script: one pointer event per frame ----
struct ReplayEvent {
    uint8_t kind;     // kPointerKind*
    int32_t x, y;
    uint8_t buttons;  // bit0 = left
};
const ReplayEvent kScript[] = {
    {kPointerKindMove, 100, 48, 0x00u},   // cursor onto the title bar
    {kPointerKindDown, 100, 48, 0x01u},   // press  -> start drag (in title bar)
    {kPointerKindMove, 130, 48, 0x01u},   // drag +30x
    {kPointerKindMove, 160, 52, 0x01u},   // drag +30x +4y
    {kPointerKindUp, 160, 52, 0x00u},     // release -> stop drag
    {kPointerKindMove, 250, 200, 0x00u},  // move away; window stays
};
constexpr uint32_t kScriptLen = sizeof(kScript) / sizeof(kScript[0]);

// expected (cursor, window) after each frame's event, and whether it is idle
struct FrameExpect {
    int32_t cx, cy, wx, wy;
    bool    idle;
};
const FrameExpect kExpect[kScriptLen] = {
    {100, 48, 60, 40, false},    // f0 move only (first frame -> full-screen dirty)
    {100, 48, 60, 40, true},     // f1 down only -> no pixel change -> idle
    {130, 48, 90, 40, false},    // f2 drag +30x
    {160, 52, 120, 44, false},   // f3 drag +30x +4y
    {160, 52, 120, 44, true},    // f4 up only -> idle
    {250, 200, 120, 44, false},  // f5 move away, window stays
};

// ---- host state (what a desktop layer would own in a real host) ----
struct HostState {
    int32_t  cursor_x = kInitCursorX, cursor_y = kInitCursorY;
    int32_t  win_x = kInitWinX, win_y = kInitWinY;
    bool     dragging = false;
    int32_t  prev_cx = kInitCursorX, prev_cy = kInitCursorY;
    int32_t  prev_wx = kInitWinX, prev_wy = kInitWinY;
    bool     first_frame = true;
    uint32_t replay_idx = 0;
    uint32_t frame_quota = 0;   // events poll_event may emit this frame
    Rect     flushed[64];
    uint32_t flush_count = 0;
};

PsfFont g_font;  // initialised in main; borrowed by paint_scene

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: " __VA_ARGS__);                                 \
            std::printf("\n");                                                 \
            return 1;                                                          \
        }                                                                      \
    } while (0)

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
uint64_t flushed_area(const HostState& st) {
    uint64_t a = 0;
    for (uint32_t i = 0; i < st.flush_count && i < 64u; i++) {
        a += static_cast<uint64_t>(st.flushed[i].width()) * st.flushed[i].height();
    }
    return a;
}
bool flushed_contains(const HostState& st, int32_t x, int32_t y) {
    for (uint32_t i = 0; i < st.flush_count && i < 64u; i++) {
        if (st.flushed[i].contains(x, y)) {
            return true;
        }
    }
    return false;
}

// ---- paint helpers (mirror offscreen_host_main; not shared to keep b3 local) ----
void draw_rect_outline(Surface& s, int32_t x, int32_t y, uint32_t w, uint32_t h,
                       uint32_t color, const ClipRect* clip) {
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

void paint_scene(Surface& s, const HostState& st) {
    fill_rect(s, 0, 0, kW, kH, kBg, nullptr);
    fill_rect(s, st.win_x, st.win_y, kWinW, kWinH, kWinFace, nullptr);
    fill_rect(s, st.win_x, st.win_y, kWinW, kTitleH, kTitleBar, nullptr);
    draw_rect_outline(s, st.win_x, st.win_y, kWinW, kWinH, kWinEdge, nullptr);
    draw_text(s, g_font, "Cinux", st.win_x + 8, st.win_y, kTitleText, nullptr);
    draw_text(s, g_font, "Hello\nCinux-GUI", st.win_x + 8,
              st.win_y + static_cast<int32_t>(kTitleH) + 8, kText, nullptr);
    fill_rect(s, st.cursor_x, st.cursor_y, kCursorSize, kCursorSize, kCursor, nullptr);
}

// ---- host callbacks ----
bool replay_poll_event(void* ctx, EventHeader* out, uint16_t cap) {
    auto* st = static_cast<HostState*>(ctx);
    if (out == nullptr || cap < sizeof(EventHeader) + sizeof(PointerPayload)) {
        return false;
    }
    if (st->frame_quota == 0u || st->replay_idx >= kScriptLen) {
        return false;
    }
    st->frame_quota--;
    const ReplayEvent& e = kScript[st->replay_idx++];
    out->magic   = kEventMagic;
    out->version = kAbiVersion;
    out->type    = EventCode::kPointer;
    out->flags   = 0;
    PointerPayload p;
    p.kind    = e.kind;
    p.x       = e.x;
    p.y       = e.y;
    p.dx      = 0;
    p.dy      = 0;
    p.buttons = e.buttons;
    std::memcpy(reinterpret_cast<uint8_t*>(out) + sizeof(EventHeader), &p, sizeof(p));
    out->payload_len = sizeof(p);
    return true;
}

void replay_dispatch_event(void* ctx, const EventHeader* ev, const void* payload) {
    auto* st = static_cast<HostState*>(ctx);
    if (ev == nullptr || ev->magic != kEventMagic || ev->type != EventCode::kPointer) {
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
        if (st->dragging) {  // window follows the cursor delta while held
            st->win_x += p.x - old_cx;
            st->win_y += p.y - old_cy;
        }
        break;
    default:
        break;
    }
}

void replay_render_frame(void* ctx, Frame* frame) {
    auto* st = static_cast<HostState*>(ctx);
    const bool cursor_moved = st->cursor_x != st->prev_cx || st->cursor_y != st->prev_cy;
    const bool win_moved    = st->win_x != st->prev_wx || st->win_y != st->prev_wy;
    if (!st->first_frame && !cursor_moved && !win_moved) {
        frame->count = 0;  // idle: nothing on screen changed
        return;
    }
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    paint_scene(s, *st);  // repaint whole staging (correct); flush only dirty

    Region dirty;
    if (st->first_frame) {
        dirty.add(Rect{0, 0, static_cast<int32_t>(kW), static_cast<int32_t>(kH)});
        st->first_frame = false;
    } else {
        if (cursor_moved) {
            dirty.add(cursor_rect(st->prev_cx, st->prev_cy));
            dirty.add(cursor_rect(st->cursor_x, st->cursor_y));
        }
        if (win_moved) {
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

void replay_flush(void* ctx, int x, int y, int w, int h, const void* /*pixels*/,
                 uint32_t /*stride*/, PixelFormat /*fmt*/) {
    auto* st = static_cast<HostState*>(ctx);
    if (st->flush_count < 64u) {
        st->flushed[st->flush_count] = Rect{x, y, x + w, y + h};
    }
    st->flush_count++;
}

Host make_host(HostState& st) {
    Host h{};
    h.core.poll_event     = replay_poll_event;
    h.core.dispatch_event = replay_dispatch_event;
    h.core.render_frame   = replay_render_frame;
    h.core.flush          = replay_flush;
    h.desktop             = nullptr;
    h.ctx                 = &st;
    return h;
}

}  // namespace

int main() {
    g_font.init();
    CHECK(g_font.ready(), "font not ready");

    HostState st;
    Host      host = make_host(st);
    GuiCore    core(&host, kW, kH, PixelFormat::kXrgb8888);

    constexpr uint64_t kFullScreen = static_cast<uint64_t>(kW) * kH;

    for (uint32_t f = 0; f < kScriptLen; f++) {
        st.frame_quota = 1u;  // one event per frame
        st.flush_count = 0u;
        core.pump();

        const FrameExpect& e = kExpect[f];
        CHECK(st.cursor_x == e.cx && st.cursor_y == e.cy,
              "f%u cursor (%d,%d) expected (%d,%d)", f, st.cursor_x, st.cursor_y, e.cx, e.cy);
        CHECK(st.win_x == e.wx && st.win_y == e.wy, "f%u window (%d,%d) expected (%d,%d)", f,
              st.win_x, st.win_y, e.wx, e.wy);

        if (e.idle) {
            CHECK(st.flush_count == 0u, "f%u idle but flushed %u rects", f, st.flush_count);
        } else {
            CHECK(st.flush_count >= 1u, "f%u expected >=1 flush, got %u", f, st.flush_count);
            if (f == 0u) {
                CHECK(flushed_area(st) >= kFullScreen,
                      "f0 first frame should flush full screen (area=%llu)",
                      static_cast<unsigned long long>(flushed_area(st)));
            } else {
                CHECK(flushed_area(st) < kFullScreen,
                      "f%u full-screen repaint after first frame (area=%llu)", f,
                      static_cast<unsigned long long>(flushed_area(st)));
                CHECK(flushed_contains(st, e.cx, e.cy),
                      "f%u new cursor (%d,%d) not inside any flushed rect", f, e.cx, e.cy);
            }
        }

        // dump key frames for human inspection (first, mid-drag, final)
        if (f == 0u || f == 3u || f == 5u) {
            char path[64];
            std::snprintf(path, sizeof(path), "replay_frame_%u.ppm", f);
            write_ppm(path, kW, kH, core.staging().pixels, core.staging().stride_bytes);
        }
        std::printf("  f%u: cursor(%d,%d) win(%d,%d) flush=%u %s\n", f, st.cursor_x,
                    st.cursor_y, st.win_x, st.win_y, st.flush_count, e.idle ? "(idle)" : "");
    }

    // one more frame after the script ends: no more events -> must be idle
    st.frame_quota = 1u;
    st.flush_count = 0u;
    core.pump();
    CHECK(st.flush_count == 0u, "post-script frame should be idle, flushed %u",
          st.flush_count);

    std::printf("replay-dump: OK (6 frames: drag window 60,40 -> 120,44; cursor "
                "-> 250,200; idle frames flush nothing)\n");
    return 0;
}

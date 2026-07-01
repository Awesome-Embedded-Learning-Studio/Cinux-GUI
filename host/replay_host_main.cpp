/**
 * @file host/replay_host_main.cpp
 * @brief Replay host: scripted pointer events drive a draggable scene (P0-b3,
 *        refactored P2-d)
 *
 * The interaction probe. Feeds a hardcoded replay script (pointer move/down/up)
 * through poll_event -> dispatch_event -- one event per frame -- while a tiny
 * host state machine moves the cursor and drags the window by the title bar.
 * render_frame syncs host state into a Scene and hands it to the stateful
 * Compositor (P2-d: the dirty diff that used to be hand-written here is now
 * core/Compositor's job). The harness asserts end-state geometry AND dirty-
 * region discipline (no full-screen repaint after the first frame; idle frames
 * flush nothing; changed pixels are flushed). Key frames are dumped to PPM.
 *
 * Standalone only.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "compositor.hpp"     // Compositor
#include "event.hpp"          // EventHeader, EventCode, kEventMagic, kAbiVersion
#include "event_payload.hpp"  // PointerPayload, kPointerKind*
#include "font.hpp"           // PsfFont
#include "gui_core.hpp"       // GuiCore
#include "host.hpp"           // Host, Frame, PixelFormat
#include "ppm_writer.hpp"     // write_ppm
#include "region.hpp"         // Rect, Region
#include "scene.hpp"          // Scene, Window, window_set_*
#include "swraster.hpp"       // Surface

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
    uint8_t kind;  // kPointerKind*
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
    int32_t    cursor_x    = kInitCursorX;
    int32_t    cursor_y    = kInitCursorY;
    int32_t    win_x       = kInitWinX;
    int32_t    win_y       = kInitWinY;
    bool       dragging    = false;
    uint32_t   replay_idx  = 0;
    uint32_t   frame_quota = 0;  // events poll_event may emit this frame
    Rect       flushed[64];
    uint32_t   flush_count = 0;
    Scene      scene;  // P2-d: synced from cursor/win each frame
    Compositor comp;   // P2-d: owns the frame-to-frame dirty diff
};

PsfFont g_font;  // initialised in main; borrowed by render_frame

#define CHECK(cond, ...)                       \
    do {                                       \
        if (!(cond)) {                         \
            std::printf("FAIL: " __VA_ARGS__); \
            std::printf("\n");                 \
            return 1;                          \
        }                                      \
    } while (0)

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
    out->magic           = kEventMagic;
    out->version         = kAbiVersion;
    out->type            = EventCode::kPointer;
    out->flags           = 0;
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
    // sync host state -> scene (positions only; palette/text stay constant)
    st->scene.windows[0].x = st->win_x;
    st->scene.windows[0].y = st->win_y;
    st->scene.cursor.x     = st->cursor_x;
    st->scene.cursor.y     = st->cursor_y;
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    Region  dirty;
    st->comp.compose(s, st->scene, g_font, &dirty);  // paint + frame diff
    const uint32_t n = dirty.count();
    for (uint32_t i = 0u; i < n && i < frame->max_rects; i++) {
        frame->rects[i] = dirty.rects()[i];
    }
    frame->count = n;  // 0 = idle -> pump flushes nothing
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
    st.scene     = make_scene(kInitWinX, kInitWinY, kInitCursorX, kInitCursorY);
    Host    host = make_host(st);
    GuiCore core(&host, kW, kH, PixelFormat::kXrgb8888);

    constexpr uint64_t kFullScreen = static_cast<uint64_t>(kW) * kH;

    for (uint32_t f = 0; f < kScriptLen; f++) {
        st.frame_quota = 1u;  // one event per frame
        st.flush_count = 0u;
        core.pump();

        const FrameExpect& e = kExpect[f];
        CHECK(st.cursor_x == e.cx && st.cursor_y == e.cy, "f%u cursor (%d,%d) expected (%d,%d)", f,
              st.cursor_x, st.cursor_y, e.cx, e.cy);
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
        std::printf("  f%u: cursor(%d,%d) win(%d,%d) flush=%u %s\n", f, st.cursor_x, st.cursor_y,
                    st.win_x, st.win_y, st.flush_count, e.idle ? "(idle)" : "");
    }

    // one more frame after the script ends: no more events -> must be idle
    st.frame_quota = 1u;
    st.flush_count = 0u;
    core.pump();
    CHECK(st.flush_count == 0u, "post-script frame should be idle, flushed %u", st.flush_count);

    std::printf(
        "replay-dump: OK (6 frames: drag window 60,40 -> 120,44; cursor "
        "-> 250,200; idle frames flush nothing)\n");
    return 0;
}

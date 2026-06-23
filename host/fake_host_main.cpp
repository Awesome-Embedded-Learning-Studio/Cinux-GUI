/**
 * @file host/fake_host_main.cpp
 * @brief Standalone host-neutrality smoke harness for the cinux::gui core
 *
 * Drives cinux::gui::pump() with a hand-filled Host table -- NO host, NO
 * framebuffer, NO real input. Built only when this dir is the build root
 * (`cmake -S . -B build && ctest --test-dir build`). If it links and runs green
 * under a standard hosted compiler, the core is provably host-neutral: the same
 * core TUs that drive the Cinux kernel today drive this fake host with only the
 * table fill swapped.
 *
 * This file is the seed of the SDL / X11 / Wayland host adapters: replace the
 * fake_* callbacks with real host input/render/flush and the core runs unchanged.
 *
 * NOT built inside a host tree -- the Cinux kernel has its own in-QEMU tests
 * under kernel/test/.
 */

#include <cstdint>
#include <cstdio>

#include "host.hpp"    // Host, HostCore, Frame, PixelFormat
#include "pump.hpp"    // cinux::gui::pump
#include "region.hpp"  // Rect, Region, rect_intersect

namespace {

using namespace cinux::gui;  // the core types live here

// ---- recorded flush calls (the harness's pretend "display") ----
struct FlushRecord {
    int      x, y, w, h;
    uint32_t stride;
    int      format;
};
constexpr uint32_t kMaxRecorded = 8;
FlushRecord g_flushed[kMaxRecorded];
uint32_t    g_flush_count = 0;

// ---- per-test render behaviour (set before each pump call) ----
enum class RenderMode { Idle, OneRect };
RenderMode g_render_mode = RenderMode::Idle;
Rect       g_test_rect{};

// ---- fake host callbacks (the table fill a real host swaps) ----
bool fake_poll_event(void* /*ctx*/, EventHeader* /*out*/, uint16_t /*out_cap*/) {
    return false;  // no input queued -- the drain loop does nothing
}

void fake_dispatch_event(void* /*ctx*/, const EventHeader* /*ev*/, const void* /*payload*/) {
    // no-op: nothing to apply in a fake host
}

void fake_render_frame(void* /*ctx*/, Frame* frame) {
    if (g_render_mode == RenderMode::Idle) {
        frame->count = 0;  // idle: nothing changed this iteration
        return;
    }
    frame->rects[0] = g_test_rect;
    frame->count    = 1;
    static uint32_t stage[4 * 4];  // dummy staging buffer -- only its address matters
    frame->pixels   = stage;
    frame->stride   = 4 * static_cast<uint32_t>(sizeof(uint32_t));
    frame->width    = 4;
    frame->height   = 4;
    frame->format   = PixelFormat::kXrgb8888;
}

void fake_flush(void* /*ctx*/, int x, int y, int w, int h, const void* /*pixels*/,
                uint32_t stride, PixelFormat fmt) {
    if (g_flush_count < kMaxRecorded) {
        g_flushed[g_flush_count] = {x, y, w, h, stride, static_cast<int>(fmt)};
    }
    g_flush_count++;
}

Host make_host() {
    Host h{};
    h.core.poll_event     = fake_poll_event;
    h.core.dispatch_event = fake_dispatch_event;
    h.core.render_frame   = fake_render_frame;
    h.core.flush          = fake_flush;
    h.desktop             = nullptr;
    h.ctx                 = nullptr;
    return h;
}

void reset(RenderMode mode, Rect r) {
    g_render_mode = mode;
    g_test_rect   = r;
    g_flush_count = 0;
}

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: " __VA_ARGS__);                                 \
            std::printf("\n");                                                 \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int run() {
    // 1. NULL host is a safe no-op (defensive discipline of pump).
    pump(nullptr);

    // 2. Idle frame: render_frame reports count == 0 -> pump flushes nothing.
    reset(RenderMode::Idle, {});
    Host h = make_host();
    pump(&h);
    CHECK(g_flush_count == 0, "idle frame flushed %u times (expected 0)", g_flush_count);

    // 3. Dirty frame: one rect {x0=1,y0=2,x1=3,y1=4} -> flush(x=1,y=2,w=2,h=2) once.
    reset(RenderMode::OneRect, Rect{1, 2, 3, 4});
    h = make_host();
    pump(&h);
    CHECK(g_flush_count == 1, "dirty frame flushed %u times (expected 1)", g_flush_count);
    CHECK(g_flushed[0].x == 1 && g_flushed[0].y == 2 && g_flushed[0].w == 2 &&
              g_flushed[0].h == 2,
          "flush geometry {%d,%d,%d,%d} (expected 1,2,2,2)", g_flushed[0].x, g_flushed[0].y,
          g_flushed[0].w, g_flushed[0].h);
    CHECK(g_flushed[0].format == static_cast<int>(PixelFormat::kXrgb8888),
          "flush format %d (expected kXrgb8888)", g_flushed[0].format);

    // 4. Region algebra (pure logic, no host): intersect + bounded Region add.
    Rect a{0, 0, 10, 10}, b{5, 5, 15, 15};
    Rect isect = rect_intersect(a, b);
    CHECK(isect.x0 == 5 && isect.y0 == 5 && isect.x1 == 10 && isect.y1 == 10,
          "rect_intersect {%d,%d,%d,%d} (expected 5,5,10,10)", isect.x0, isect.y0, isect.x1,
          isect.y1);

    Region reg;
    reg.add(Rect{0, 0, 4, 4});
    reg.add(Rect{10, 10, 14, 14});
    CHECK(reg.count() == 2, "region count %u (expected 2)", reg.count());

    std::printf(
        "cinux-gui-smoke: OK (null-host safe + idle skip + dirty flush + region algebra)\n");
    return 0;
}

}  // namespace

int main() { return run(); }

/**
 * @file test/test_compositor.cpp
 * @brief Compositor unit test -- Scene -> pixels (P2-b) + dirty diff (P2-c)
 *
 * P2-b: builds the SAME scene the offscreen host paints by hand and verifies
 * compose() reproduces it at the structural sample points (zero-regression),
 * plus z-order, the empty scene, and the no-titlebar case.
 *
 * P2-c: drives the stateful Compositor class across frames and asserts dirty-
 * region discipline -- first frame full screen, identical frame idle, a moved
 * cursor/window dirties only old∪new (area < full) AND repaints correctly
 * (new footprint painted, old footprint exposed back to background).
 */

#include <cstdint>
#include <cstdio>

#include "compositor.hpp"
#include "font.hpp"
#include "region.hpp"
#include "scene.hpp"
#include "swraster.hpp"

using namespace cinux::gui;

#define CHECK(cond, ...)                       \
    do {                                       \
        if (!(cond)) {                         \
            std::printf("FAIL: " __VA_ARGS__); \
            std::printf("\n");                 \
            return 1;                          \
        }                                      \
    } while (0)

namespace {

constexpr uint32_t kW = 320, kH = 240;
constexpr uint64_t kFullScreen = static_cast<uint64_t>(kW) * kH;

// Palette identical to offscreen_host_main (zero-regression baseline).
constexpr uint32_t kBg        = 0x0018182Au;
constexpr uint32_t kWinFace   = 0x00C8C8C8u;
constexpr uint32_t kWinEdge   = 0x00404040u;
constexpr uint32_t kTitleBar  = 0x003060A0u;
constexpr uint32_t kTitleText = 0x00FFFFFFu;
constexpr uint32_t kText      = 0x00181818u;
constexpr uint32_t kCursor    = 0x00FFFFFFu;

/* Owned staging buffer + Surface descriptor; reads back as uint32_t pixels. */
struct Stage {
    uint8_t* buf;
    Surface  s;
    Stage() : buf(new uint8_t[kW * kH * 4u]), s{buf, kW, kH, kW * 4u, PixelFormat::kXrgb8888} {}
    ~Stage() { delete[] buf; }
    const uint32_t* px() const { return reinterpret_cast<const uint32_t*>(buf); }
};

/* The offscreen host's scene, built through the Scene model. */
Scene offscreen_scene() {
    Scene sc{};
    sc.bg_color = kBg;
    Window w{};
    w.x                = 60;
    w.y                = 40;
    w.w                = 200;
    w.h                = 160;
    w.face_color       = kWinFace;
    w.edge_color       = kWinEdge;
    w.titlebar_color   = kTitleBar;
    w.titlebar_height  = 16;
    w.title_text_color = kTitleText;
    w.body_text_color  = kText;
    window_set_title(w, "Cinux");
    window_set_body(w, "Hello\nCinux-GUI");
    scene_add_window(sc, w);
    sc.cursor.x     = 180;
    sc.cursor.y     = 144;
    sc.cursor.w     = 4;
    sc.cursor.h     = 4;
    sc.cursor.color = kCursor;
    return sc;
}

bool region_contains(const Region& reg, int32_t x, int32_t y) {
    const uint32_t n = reg.count();
    for (uint32_t i = 0u; i < n; i++) {
        if (reg.rects()[i].contains(x, y)) {
            return true;
        }
    }
    return false;
}

uint64_t region_area(const Region& reg) {
    uint64_t       a = 0;
    const uint32_t n = reg.count();
    for (uint32_t i = 0u; i < n; i++) {
        a += static_cast<uint64_t>(reg.rects()[i].width()) * reg.rects()[i].height();
    }
    return a;
}

}  // namespace

int main() {
    PsfFont font;
    font.init();
    CHECK(font.ready(), "font not ready");

    // ===== P2-b: stateless full-scene paint =====

    // 1. zero-regression vs the offscreen host's hand-written paint
    {
        Stage st;
        Scene sc = offscreen_scene();
        compose(st.s, sc, font);

        const uint32_t* px  = st.px();
        const uint32_t  ppr = kW;
        CHECK(px[0] == kBg, "origin pixel 0x%08X expected bg 0x%08X", px[0], kBg);
        const uint32_t wc = px[(40 + 160 / 2) * ppr + (60 + 200 / 2)];
        CHECK(wc == kWinFace, "window-centre 0x%08X expected face 0x%08X", wc, kWinFace);
        const uint32_t tb = px[(40 + 16 / 2) * ppr + (60 + 200 / 2)];
        CHECK(tb == kTitleBar, "titlebar-centre 0x%08X expected 0x%08X", tb, kTitleBar);
        const uint32_t cp = px[144 * ppr + 180];
        CHECK(cp == kCursor, "cursor pixel 0x%08X expected 0x%08X", cp, kCursor);
        CHECK(px[(kH - 1) * ppr + (kW - 1)] == kBg, "far corner not bg");
    }

    // 2. z-order: a later window paints over an earlier one at the overlap
    {
        Stage st;
        Scene sc{};
        sc.bg_color = 0u;
        Window a{};
        a.x          = 50;
        a.y          = 50;
        a.w          = 100;
        a.h          = 100;
        a.face_color = 0x00AA0000u;
        Window b{};
        b.x          = 80;
        b.y          = 80;
        b.w          = 40;
        b.h          = 40;
        b.face_color = 0x0000BB00u;
        scene_add_window(sc, a);
        scene_add_window(sc, b);
        compose(st.s, sc, font);
        const uint32_t* px = st.px();
        CHECK(px[100 * kW + 100] == 0x0000BB00u, "overlap should show top window");
        CHECK(px[55 * kW + 55] == 0x00AA0000u, "bottom-only area should show bottom window");
    }

    // 3. empty scene -> whole staging is background
    {
        Stage st;
        Scene sc{};
        sc.bg_color = 0x00112233u;
        compose(st.s, sc, font);
        const uint32_t* px = st.px();
        CHECK(px[0] == 0x00112233u && px[(kH - 1) * kW + (kW - 1)] == 0x00112233u,
              "empty scene not all-bg");
    }

    // 4. titlebar_height == 0 -> no title band; window top inside the edge is face
    {
        Stage st;
        Scene sc{};
        sc.bg_color = 0u;
        Window w{};
        w.x               = 10;
        w.y               = 10;
        w.w               = 80;
        w.h               = 80;
        w.face_color      = kWinFace;
        w.edge_color      = kWinEdge;
        w.titlebar_height = 0;
        scene_add_window(sc, w);
        compose(st.s, sc, font);
        const uint32_t* px = st.px();
        CHECK(px[12 * kW + 50] == kWinFace, "no-titlebar top inside edge should be face");
    }

    // ===== P2-c: stateful dirty diff =====

    // 5. first frame -> full-screen dirty, pixels correct
    {
        Stage      st;
        Scene      sc = offscreen_scene();
        Compositor comp;
        Region     dirty;
        comp.compose(st.s, sc, font, &dirty);
        CHECK(region_area(dirty) >= kFullScreen, "first frame should dirty full screen");
        const uint32_t* px = st.px();
        CHECK(px[(40 + 80) * kW + (60 + 100)] == kWinFace, "first-frame window pixel wrong");
    }

    // 6. identical frame -> idle (empty dirty, nothing to repaint)
    {
        Stage      st;
        Scene      sc = offscreen_scene();
        Compositor comp;
        Region     dirty;
        comp.compose(st.s, sc, font, &dirty);  // first frame
        dirty.clear();
        comp.compose(st.s, sc, font, &dirty);  // identical
        CHECK(dirty.count() == 0u, "identical frame should be idle, dirty=%u", dirty.count());
    }

    // 7. cursor move -> dirty is old∪new cursor (area < full); new cursor painted
    {
        Stage      st;
        Scene      sc = offscreen_scene();
        Compositor comp;
        Region     dirty;
        comp.compose(st.s, sc, font, &dirty);  // first frame
        sc.cursor.x = 200;
        sc.cursor.y = 160;
        dirty.clear();
        comp.compose(st.s, sc, font, &dirty);
        CHECK(region_area(dirty) < kFullScreen, "cursor move should dirty < full screen");
        CHECK(region_contains(dirty, 200, 160), "new cursor not in dirty");
        CHECK(region_contains(dirty, 180, 144), "old cursor not in dirty");
        const uint32_t* px = st.px();
        CHECK(px[160 * kW + 200] == kCursor, "new cursor pixel not painted");
    }

    // 8. window move -> dirty old∪new (< full); new footprint painted, old
    //    footprint exposed back to background
    {
        Stage      st;
        Scene      sc = offscreen_scene();
        Compositor comp;
        Region     dirty;
        comp.compose(st.s, sc, font, &dirty);  // first frame
        sc.windows[0].x = 120;
        sc.windows[0].y = 44;
        dirty.clear();
        comp.compose(st.s, sc, font, &dirty);
        CHECK(region_area(dirty) < kFullScreen, "window move should dirty < full");
        const uint32_t* px = st.px();
        // new window centre painted with face
        CHECK(px[(44 + 80) * kW + (120 + 100)] == kWinFace, "moved window centre not face");
        // a point in the OLD footprint but OUTSIDE the new window -> bg exposed
        CHECK(px[42 * kW + 62] == kBg, "old footprint should expose bg, got 0x%08X",
              px[42 * kW + 62]);
    }

    // 9. background colour change -> full-screen dirty
    {
        Stage      st;
        Scene      sc = offscreen_scene();
        Compositor comp;
        Region     dirty;
        comp.compose(st.s, sc, font, &dirty);  // first frame
        sc.bg_color = 0x00FFFFFFu;
        dirty.clear();
        comp.compose(st.s, sc, font, &dirty);
        CHECK(region_area(dirty) >= kFullScreen, "bg change should dirty full screen");
    }

    std::printf(
        "compositor-test: OK (zero-regression/z-order/empty/no-titlebar + dirty: "
        "first/idle/cursor/window-move/bg)\n");
    return 0;
}

/**
 * @file test/test_compositor.cpp
 * @brief Compositor unit test -- Scene -> pixels via swraster (P2-b)
 *
 * Builds the SAME scene the offscreen host paints by hand (window + title bar
 * + text + cursor) and verifies compose() reproduces it at the structural
 * sample points -- the zero-regression proof that P2-b converges the three
 * hosts' paint without changing what reaches the screen. Also covers z-order,
 * the empty scene, and the no-titlebar case.
 */

#include <cstdint>
#include <cstdio>

#include "compositor.hpp"
#include "font.hpp"
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

}  // namespace

int main() {
    PsfFont font;
    font.init();
    CHECK(font.ready(), "font not ready");

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
        // a corner away from any window/cursor stays background
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

    // 4. titlebar_height == 0 -> no title band; window top inside the edge is
    //    the face colour (not a title bar)
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

    std::printf("compositor-test: OK (zero-regression/z-order/empty/no-titlebar)\n");
    return 0;
}

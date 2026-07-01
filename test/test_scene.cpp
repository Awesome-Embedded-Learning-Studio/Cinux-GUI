/**
 * @file test/test_scene.cpp
 * @brief Scene data-model unit test -- pure geometry / capacity / text (P2-a)
 *
 * No rasteriser, no host: validates the Scene/Window/Cursor POD shapes, the
 * window_rect/cursor_rect half-open geometry, the fixed-capacity z-stack
 * discipline, degenerate-window rejection, and NUL-terminated text copy +
 * truncation. The Compositor (P2-b) consumes the same Scene this exercises.
 */

#include <cstdint>
#include <cstdio>

#include "scene.hpp"

using namespace cinux::gui;

#define CHECK(cond, ...)                       \
    do {                                       \
        if (!(cond)) {                         \
            std::printf("FAIL: " __VA_ARGS__); \
            std::printf("\n");                 \
            return 1;                          \
        }                                      \
    } while (0)

int main() {
    // 1. fresh scene is empty
    {
        Scene sc{};
        CHECK(sc.window_count == 0u, "fresh scene window_count=%u", sc.window_count);
    }

    // 2. add one window -> count 1, all fields preserved
    {
        Scene  sc{};
        Window w{};
        w.x               = 60;
        w.y               = 40;
        w.w               = 200;
        w.h               = 160;
        w.face_color      = 0x00C8C8C8u;
        w.titlebar_color  = 0x003060A0u;
        w.titlebar_height = 16;
        CHECK(scene_add_window(sc, w), "add should succeed");
        CHECK(sc.window_count == 1u, "count=%u", sc.window_count);
        CHECK(sc.windows[0].x == 60 && sc.windows[0].w == 200, "geometry not preserved");
        CHECK(sc.windows[0].face_color == 0x00C8C8C8u, "face_color not preserved");
        CHECK(sc.windows[0].titlebar_height == 16u, "titlebar_height not preserved");
    }

    // 3. window_rect / cursor_rect are half-open [x0,x1) x [y0,y1)
    {
        Window w{};
        w.x          = 60;
        w.y          = 40;
        w.w          = 200;
        w.h          = 160;
        const Rect r = window_rect(w);
        CHECK(r.x0 == 60 && r.y0 == 40 && r.x1 == 260 && r.y1 == 200,
              "window_rect wrong (%d,%d,%d,%d)", r.x0, r.y0, r.x1, r.y1);
        CHECK(r.width() == 200u && r.height() == 160u, "window_rect w/h=%u/%u", r.width(),
              r.height());
        CHECK(!r.empty(), "non-zero window_rect should not be empty");

        Cursor c{};
        c.x           = 10;
        c.y           = 20;
        c.w           = 4;
        c.h           = 4;
        const Rect cr = cursor_rect(c);
        CHECK(cr.x0 == 10 && cr.y0 == 20 && cr.x1 == 14 && cr.y1 == 24, "cursor_rect wrong");
    }

    // 4. fill the stack to the cap, then the next add is dropped
    {
        Scene  sc{};
        Window w{};
        w.w = 1;
        w.h = 1;
        for (uint32_t i = 0u; i < kSceneMaxWindows; i++) {
            CHECK(scene_add_window(sc, w), "add %u should succeed", i);
        }
        CHECK(sc.window_count == kSceneMaxWindows, "count=%u", sc.window_count);
        CHECK(!scene_add_window(sc, w), "add over cap should fail");
        CHECK(sc.window_count == kSceneMaxWindows, "count changed after overflow=%u",
              sc.window_count);
    }

    // 5. degenerate windows (zero area) are rejected, count unchanged
    {
        Scene  sc{};
        Window zw{};
        zw.w = 0;
        zw.h = 10;
        Window hw{};
        hw.w = 10;
        hw.h = 0;
        CHECK(!scene_add_window(sc, zw), "zero-w window should be rejected");
        CHECK(!scene_add_window(sc, hw), "zero-h window should be rejected");
        CHECK(sc.window_count == 0u, "count=%u after degenerate adds", sc.window_count);
    }

    // 6. text copy: short copies whole, nullptr clears, overlong truncates with
    //    a guaranteed NUL at the last slot (no overrun), newlines preserved
    {
        Window w{};
        window_set_title(w, "Cinux");
        CHECK(w.title[0] == 'C', "title[0]='%c'", w.title[0]);
        CHECK(w.title[5] == '\0', "title not NUL-terminated at 5");

        window_set_body(w, "Hello\nCinux-GUI");
        CHECK(w.body[0] == 'H' && w.body[5] == '\n', "body copy/newline wrong");
        CHECK(w.body[15] == '\0', "body not NUL-terminated at 15");

        window_set_title(w, nullptr);
        CHECK(w.title[0] == '\0', "nullptr title should clear");

        // overlong title: last slot forced NUL, second-to-last holds a real char
        char overlong[kWindowTitleLen + 8];
        for (uint32_t i = 0u; i < kWindowTitleLen + 7u; i++) {
            overlong[i] = 'A';
        }
        overlong[kWindowTitleLen + 7u] = '\0';
        window_set_title(w, overlong);
        CHECK(w.title[kWindowTitleLen - 1u] == '\0', "last title slot not NUL after truncate");
        CHECK(w.title[kWindowTitleLen - 2u] == 'A', "title truncate lost a char at %u",
              kWindowTitleLen - 2u);
    }

    // 7. scene_clear empties the stack
    {
        Scene  sc{};
        Window w{};
        w.w = 1;
        w.h = 1;
        scene_add_window(sc, w);
        scene_add_window(sc, w);
        CHECK(sc.window_count == 2u, "pre-clear count=%u", sc.window_count);
        scene_clear(sc);
        CHECK(sc.window_count == 0u, "scene_clear left count=%u", sc.window_count);
    }

    // 8. z-order == array order (later index paints over earlier)
    {
        Scene  sc{};
        Window a{};
        a.x          = 0;
        a.w          = 10;
        a.h          = 10;
        a.face_color = 1u;
        Window b{};
        b.x          = 5;
        b.w          = 10;
        b.h          = 10;
        b.face_color = 2u;
        scene_add_window(sc, a);
        scene_add_window(sc, b);
        CHECK(sc.windows[0].face_color == 1u && sc.windows[1].face_color == 2u,
              "z-order not preserved (%u, %u)", sc.windows[0].face_color, sc.windows[1].face_color);
    }

    std::printf(
        "scene-test: OK "
        "(empty/add/geometry/capacity/degenerate/text/clear/z-order)\n");
    return 0;
}

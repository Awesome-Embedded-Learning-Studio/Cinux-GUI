/**
 * @file core/compositor.cpp
 * @brief cinux::gui scene compositor -- Scene -> staging pixels via swraster
 *
 * Lifts the three probe hosts' (offscreen/replay/fbdev) hand-written
 * paint_scene into one host-neutral implementation. The paint order and text
 * positions match the hosts verbatim so compose() output is pixel-identical to
 * the old hand-written paint (zero regression -- asserted by test_compositor).
 *
 * Host-neutral, ZERO host includes.
 *
 * Compile condition: CINUX_GUI.
 */

#include "compositor.hpp"

#include <stdint.h>

#include "swraster.hpp"  // ClipRect, fill_rect, draw_line, glyph_blit

namespace cinux::gui {
namespace {

/* 1px border outline via four Bresenham lines. Mirrors the hosts' hand-written
 * draw_rect_outline verbatim; kept local (decision B: do NOT promote to
 * swraster -- swraster stays a pure-primitive layer, no font dependency). */
void draw_rect_outline(Surface& s, int32_t x, int32_t y, uint32_t w, uint32_t h,
                       uint32_t color, const ClipRect* clip) {
    const int32_t x1 = x + static_cast<int32_t>(w) - 1;
    const int32_t y1 = y + static_cast<int32_t>(h) - 1;
    draw_line(s, x, y, x1, y, color, clip);
    draw_line(s, x, y1, x1, y1, color, clip);
    draw_line(s, x, y, x, y1, color, clip);
    draw_line(s, x1, y, x1, y1, color, clip);
}

/* Render a NUL-terminated string with glyph_blit; '\n' wraps to the next row
 * (one font.height()). Mirrors the hosts' draw_text. nullptr/empty is a no-op. */
void draw_text(Surface& s, const PsfFont& font, const char* str, int32_t x, int32_t y,
               uint32_t color, const ClipRect* clip) {
    if (str == nullptr) {
        return;
    }
    int32_t cx = x;
    int32_t cy = y;
    for (const char* p = str; *p != '\0'; p++) {
        if (*p == '\n') {
            cx = x;
            cy += static_cast<int32_t>(font.height());
            continue;
        }
        glyph_blit(s, cx, cy, font.glyph(static_cast<uint8_t>(*p)), font.width(), font.height(),
                   color, clip);
        cx += static_cast<int32_t>(font.width());
    }
}

/* Paint one window: face fill -> titlebar band -> edge outline -> title text
 * -> body text. The order and text positions are exactly what the three hosts
 * used, so compose() is pixel-identical to the old hand-written paint. */
void compose_window(Surface& s, const Window& win, const PsfFont& font) {
    fill_rect(s, win.x, win.y, win.w, win.h, win.face_color, nullptr);
    if (win.titlebar_height != 0u) {
        fill_rect(s, win.x, win.y, win.w, win.titlebar_height, win.titlebar_color, nullptr);
    }
    draw_rect_outline(s, win.x, win.y, win.w, win.h, win.edge_color, nullptr);
    draw_text(s, font, win.title, win.x + 8, win.y, win.title_text_color, nullptr);
    const int32_t body_y = win.y + static_cast<int32_t>(win.titlebar_height) + 8;
    draw_text(s, font, win.body, win.x + 8, body_y, win.body_text_color, nullptr);
}

}  // namespace

void compose(Surface& staging, const Scene& scene, const PsfFont& font) {
    fill_rect(staging, 0, 0, staging.width, staging.height, scene.bg_color, nullptr);
    for (uint32_t i = 0u; i < scene.window_count; i++) {
        compose_window(staging, scene.windows[i], font);
    }
    fill_rect(staging, scene.cursor.x, scene.cursor.y, scene.cursor.w, scene.cursor.h,
              scene.cursor.color, nullptr);
}

}  // namespace cinux::gui

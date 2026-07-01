/**
 * @file core/compositor.cpp
 * @brief cinux::gui scene compositor -- Scene -> staging pixels via swraster
 *
 * P2-b: the stateless full-scene paint that converges the three probe hosts'
 * hand-written paint_scene. P2-c: the stateful Compositor class that diffs the
 * previous frame and repaints only the changed region (saves the composite
 * itself, not just the flush).
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
void draw_rect_outline(Surface& s, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color,
                       const ClipRect* clip) {
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

/* Like draw_text but each glyph is glyph_blit_scaled at @p scale (P5-a). '\n'
 * wraps to the next row (one font.height() * scale). nullptr/empty is a no-op. */
void draw_text_scaled(Surface& s, const PsfFont& font, const char* str, int32_t x, int32_t y,
                      uint32_t color, uint32_t scale, const ClipRect* clip) {
    if (str == nullptr) {
        return;
    }
    int32_t cx = x;
    int32_t cy = y;
    for (const char* p = str; *p != '\0'; p++) {
        if (*p == '\n') {
            cx = x;
            cy += static_cast<int32_t>(font.height() * scale);
            continue;
        }
        glyph_blit_scaled(s, cx, cy, font.glyph(static_cast<uint8_t>(*p)), font.width(),
                          font.height(), scale, color, clip);
        cx += static_cast<int32_t>(font.width() * scale);
    }
}

/* Paint one window: face fill -> titlebar band -> edge outline -> title text
 * -> body text. The order and text positions are exactly what the three hosts
 * used, so output is pixel-identical to the old hand-written paint. @p clip
 * restricts every primitive (P2-c paints window-by-window inside dirty rects). */
void compose_window(Surface& s, const Window& win, const PsfFont& font, const ClipRect* clip) {
    fill_rect(s, win.x, win.y, win.w, win.h, win.face_color, clip);
    if (win.titlebar_height != 0u) {
        fill_rect(s, win.x, win.y, win.w, win.titlebar_height, win.titlebar_color, clip);
    }
    draw_rect_outline(s, win.x, win.y, win.w, win.h, win.edge_color, clip);
    draw_text(s, font, win.title, win.x + 8, win.y, win.title_text_color, clip);
    const int32_t body_y = win.y + static_cast<int32_t>(win.titlebar_height) + 8;
    draw_text(s, font, win.body, win.x + 8, body_y, win.body_text_color, clip);
}

/* Paint bg + all windows (z order) + cursor, every primitive clipped to @p clip
 * (nullptr = full screen). The shared core both entry points use: stateless
 * compose() passes nullptr; Compositor passes each dirty rect in turn. Because
 * bg is painted first, then windows, then cursor, a dirty rect covering a moved
 * window's OLD footprint correctly repaints the exposed background. */
void paint_scene_clipped(Surface& s, const Scene& scene, const PsfFont& font,
                         const ClipRect* clip) {
    fill_rect(s, 0, 0, s.width, s.height, scene.bg_color, clip);
    for (uint32_t i = 0u; i < scene.window_count; i++) {
        compose_window(s, scene.windows[i], font, clip);
    }
    fill_rect(s, scene.cursor.x, scene.cursor.y, scene.cursor.w, scene.cursor.h, scene.cursor.color,
              clip);
}

/* NUL-terminated compare up to @p cap (no <cstring> in core/). Equal iff every
 * byte matches through the first NUL of either side. */
bool str_eq(const char* a, const char* b, uint32_t cap) {
    for (uint32_t i = 0u; i < cap; i++) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
    return true;
}

bool window_equals(const Window& a, const Window& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.face_color == b.face_color &&
           a.edge_color == b.edge_color && a.titlebar_color == b.titlebar_color &&
           a.title_text_color == b.title_text_color && a.body_text_color == b.body_text_color &&
           a.titlebar_height == b.titlebar_height && str_eq(a.title, b.title, kWindowTitleLen) &&
           str_eq(a.body, b.body, kWindowBodyLen);
}

bool cursor_equals(const Cursor& a, const Cursor& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.color == b.color;
}

/* Diff prev vs cur into @p dirty: each changed window/cursor contributes BOTH
 * its old and new footprint, so the region never under-covers (a stale pixel
 * from the old position is repainted). bg change -> full screen. */
void diff_scene(const Scene& prev, const Scene& cur, const Rect& full, Region* dirty) {
    if (prev.bg_color != cur.bg_color) {
        dirty->add(full);  // whole-screen repaint
        return;
    }
    const uint32_t n = prev.window_count > cur.window_count ? prev.window_count : cur.window_count;
    for (uint32_t i = 0u; i < n; i++) {
        if (i >= prev.window_count) {
            dirty->add(window_rect(cur.windows[i]));  // new window
        } else if (i >= cur.window_count) {
            dirty->add(window_rect(prev.windows[i]));  // window removed
        } else if (!window_equals(prev.windows[i], cur.windows[i])) {
            dirty->add(window_rect(prev.windows[i]));  // moved/changed: old + new
            dirty->add(window_rect(cur.windows[i]));
        }
    }
    if (!cursor_equals(prev.cursor, cur.cursor)) {
        dirty->add(cursor_rect(prev.cursor));
        dirty->add(cursor_rect(cur.cursor));
    }
}

}  // namespace

void compose(Surface& staging, const Scene& scene, const PsfFont& font) {
    paint_scene_clipped(staging, scene, font, nullptr);
}

void Compositor::compose(Surface& staging, const Scene& scene, const PsfFont& font, Region* dirty) {
    if (dirty != nullptr) {
        dirty->clear();
    }
    Region  local;  // sink used when caller passes nullptr
    Region* d = (dirty != nullptr) ? dirty : &local;

    const Rect full{0, 0, static_cast<int32_t>(staging.width),
                    static_cast<int32_t>(staging.height)};
    if (first_) {
        d->add(full);
    } else {
        diff_scene(prev_, scene, full, d);
    }

    /* Repaint each dirty rect with the whole scene clipped to it. z-ordering
     * plus idempotent pixels make this correct where rects overlap. */
    const uint32_t dn = d->count();
    for (uint32_t i = 0u; i < dn; i++) {
        const Rect&    r = d->rects()[i];
        const ClipRect clip{r.x0, r.y0, r.x1, r.y1};
        paint_scene_clipped(staging, scene, font, &clip);
    }

    prev_  = scene;
    first_ = false;
}

void execute(Surface& staging, const PaintList& list, const PsfFont& font) {
    /* Clip stack: each kClipPush intersects with its parent, so a widget can
     * never paint outside its ancestors' rects. top == -1 means unclipped
     * (primitives clip to the surface bounds only, via nullptr). */
    constexpr uint32_t kMaxClip = 32;
    ClipRect           stack[kMaxClip];
    int32_t            top = -1;
    const auto         cur = [&]() -> const ClipRect* { return top >= 0 ? &stack[top] : nullptr; };

    const uint32_t n = list.count();
    for (uint32_t i = 0u; i < n; i++) {
        const PaintCmd& c = list.at(i);
        switch (c.kind) {
            case CmdKind::kFillRect:
                fill_rect(staging, c.fill.x, c.fill.y, c.fill.w, c.fill.h, c.fill.color, cur());
                break;
            case CmdKind::kFillRoundRect:
                fill_rounded_rect(staging, c.rfill.x, c.rfill.y, c.rfill.w, c.rfill.h,
                                  c.rfill.color, c.rfill.radius, cur(), c.rfill.corners);
                break;
            case CmdKind::kText:
                draw_text(staging, font, c.text.text, c.text.x, c.text.y, c.text.color, cur());
                break;
            case CmdKind::kTextGlyph: {
                /* Single inlined char -- no borrowed pointer (terminal cells). */
                const uint8_t* bits = font.glyph(static_cast<uint8_t>(c.glyph.ch));
                if (bits != nullptr) {
                    glyph_blit(staging, c.glyph.x, c.glyph.y, bits, font.width(), font.height(),
                               c.glyph.color, cur());
                }
                break;
            }
            case CmdKind::kTextScaled:
                draw_text_scaled(staging, font, c.scaled.text, c.scaled.x, c.scaled.y,
                                 c.scaled.color, c.scaled.scale, cur());
                break;
            case CmdKind::kClipPush: {
                if (top + 1 < static_cast<int32_t>(kMaxClip)) {
                    ClipRect pushed{c.clip.x0, c.clip.y0, c.clip.x1, c.clip.y1};
                    if (top >= 0) {  // intersect with the current top clip
                        const ClipRect& p = stack[top];
                        pushed            = ClipRect{pushed.x0 > p.x0 ? pushed.x0 : p.x0,
                                                     pushed.y0 > p.y0 ? pushed.y0 : p.y0,
                                                     pushed.x1 < p.x1 ? pushed.x1 : p.x1,
                                                     pushed.y1 < p.y1 ? pushed.y1 : p.y1};
                    }
                    stack[++top] = pushed;
                }
                break;
            }
            case CmdKind::kClipPop:
                if (top >= 0) {
                    top--;
                }
                break;
        }
    }
}

}  // namespace cinux::gui

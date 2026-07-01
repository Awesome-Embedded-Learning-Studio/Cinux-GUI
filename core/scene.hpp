/**
 * @file core/scene.hpp
 * @brief cinux::gui host-neutral scene model -- the DATA a Compositor paints
 *
 * A Scene is a pure-data description of one desktop frame: a background colour,
 * an ordered stack of Windows (array order == z order; later windows paint over
 * earlier ones), and a Cursor. It owns NO pixels and calls NO rasteriser -- it
 * is what the host fills and hands to the Compositor (P2-b), which paints it
 * into the core-owned staging Surface via swraster + PsfFont.
 *
 * This is the convergence target for the three probe hosts (offscreen / replay
 * / fbdev), which today each hand-write the same window+titlebar+text+cursor
 * paint in their own render_frame. P2 lifts that paint into a host-neutral
 * Compositor driven by a Scene, so swapping a host no longer rewrites the
 * scene.
 *
 * Pure C++17 (stdint/stddef only), ZERO host includes -- same boundary as the
 * rest of core/. No <vector>/<string>: text is fixed NUL-terminated char arrays
 * and the window stack is a bounded array, matching the event-ABI discipline.
 *
 * Compile condition: CINUX_GUI.
 *
 * Namespace: cinux::gui
 */
#pragma once

#include <stdint.h>

#include "region.hpp"  // Rect (half-open geometry, shared with region algebra)

namespace cinux::gui {

/* Text capacity per window. Fixed (no dynamic strings in core/). */
inline constexpr uint32_t kWindowTitleLen = 32;
inline constexpr uint32_t kWindowBodyLen  = 128;

/* Cap on the window stack. Overflow drops the window (scene_add_window returns
 * false) -- keep it generous; a dropped window is a real loss. P3/P4
 * widget/desktop layers raise this if needed. */
inline constexpr uint32_t kSceneMaxWindows = 16;

/**
 * @brief One rectangular window: geometry + palette + two text runs
 *
 * POD aggregate. Geometry is position+size (matches the swraster primitive
 * signatures x/y/w/h); window_rect() lifts it to the half-open Rect the region
 * algebra uses. title/body are NUL-terminated; an empty first byte means "no
 * text". titlebar_height rows at the top are filled with titlebar_color before
 * the title glyph is drawn (0 = no title band).
 */
struct Window {
    int32_t  x                      = 0;
    int32_t  y                      = 0;
    uint32_t w                      = 0;
    uint32_t h                      = 0;
    uint32_t face_color             = 0;  // window body fill
    uint32_t edge_color             = 0;  // 1px border outline
    uint32_t titlebar_color         = 0;  // title band fill (top titlebar_height rows)
    uint32_t title_text_color       = 0;
    uint32_t body_text_color        = 0;
    uint32_t titlebar_height        = 0;  // 0 = no title bar band
    char     title[kWindowTitleLen] = {};
    char     body[kWindowBodyLen]   = {};
};

/**
 * @brief The cursor: a small solid rectangle (the current probe cursor)
 *
 * POD aggregate, same x/y/w/h geometry convention as Window.
 */
struct Cursor {
    int32_t  x     = 0;
    int32_t  y     = 0;
    uint32_t w     = 0;
    uint32_t h     = 0;
    uint32_t color = 0;
};

/**
 * @brief A host-neutral scene: background + ordered window stack + cursor
 *
 * POD aggregate. windows[] is the z stack: index 0 is bottom, window_count-1 is
 * top. The Compositor paints background, then windows in ascending index order,
 * then the cursor last (always on top).
 */
struct Scene {
    uint32_t bg_color                  = 0;
    Window   windows[kSceneMaxWindows] = {};
    uint32_t window_count              = 0;
    Cursor   cursor                    = {};
};

/**
 * @brief Half-open bounding rect [x,x+w) x [y,y+h) of a window
 *
 * Degenerate (empty) when w==0 || h==0. This is the shape that enters region
 * algebra for dirty tracking.
 */
constexpr Rect window_rect(const Window& win) {
    return Rect{win.x, win.y, win.x + static_cast<int32_t>(win.w),
                win.y + static_cast<int32_t>(win.h)};
}

/** Half-open bounding rect [x,x+w) x [y,y+h) of the cursor. */
constexpr Rect cursor_rect(const Cursor& cur) {
    return Rect{cur.x, cur.y, cur.x + static_cast<int32_t>(cur.w),
                cur.y + static_cast<int32_t>(cur.h)};
}

/** True if the window has non-zero area (w>0 && h>0). */
constexpr bool window_visible(const Window& win) { return win.w != 0u && win.h != 0u; }

/** Reset to an empty scene (background only, no windows). */
inline void scene_clear(Scene& sc) { sc.window_count = 0u; }

/**
 * @brief Append a window to the top of the scene's z stack
 *
 * The window is copied into the fixed stack; it will paint over all prior
 * windows. Degenerate windows (zero area) are rejected.
 *
 * @return true on success; false if the stack is full or the window is
 *         degenerate (in either case the window is dropped -- count unchanged)
 */
bool scene_add_window(Scene& sc, const Window& win);

/**
 * @brief Copy @p text into the window's title, NUL-terminating at the cap
 *
 * A nullptr text clears the title. A too-long text is truncated so that
 * title[kWindowTitleLen-1] is always NUL -- no overrun.
 */
void window_set_title(Window& win, const char* text);

/**
 * @brief Copy @p text into the window's body, NUL-terminating at the cap
 *
 * Same truncation/NUL contract as window_set_title(). Newlines in the body are
 * preserved (the Compositor wraps on '\n').
 */
void window_set_body(Window& win, const char* text);

}  // namespace cinux::gui

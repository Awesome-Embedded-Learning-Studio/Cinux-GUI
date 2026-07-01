/**
 * @file core/compositor.hpp
 * @brief cinux::gui host-neutral scene compositor -- Scene -> staging pixels
 *
 * Two entry points share one paint core:
 *
 *   - compose()            [P2-b] stateless: paint the WHOLE scene every call.
 *                          A drop-in replacement for the three hosts' hand-
 *                          written paint_scene. Used by tests and simple hosts.
 *
 *   - Compositor class     [P2-c] stateful: holds the previous frame's Scene
 *                          snapshot and paints only what CHANGED. compose()
 *                          diffs prev vs cur into a dirty Region (each mover's
 *                          old ∪ new footprint -- never under-covers), then
 *                          repaints each dirty rect clipped to the staging
 *                          buffer. Identical frames are idle (empty dirty, no
 *                          paint). This saves the composite itself, not just
 *                          the flush -- the whole point of P2.
 *
 * Paint order in both: background, then windows in z order (face -> titlebar
 * band -> edge outline -> title text -> body text), then the cursor on top.
 *
 * Zero host includes; depends only on scene/swraster/font/region (all core/).
 *
 * Compile condition: CINUX_GUI.
 * Namespace: cinux::gui
 */
#pragma once

#include "font.hpp"        // PsfFont
#include "paint_list.hpp"  // PaintList (P3-a execute)
#include "region.hpp"      // Region (Compositor dirty output)
#include "scene.hpp"       // Scene
#include "swraster.hpp"    // Surface

namespace cinux::gui {

/**
 * @brief [P2-b] Paint a whole Scene into @p staging (stateless full repaint)
 *
 * Background, then windows in z order, then cursor. The whole staging surface
 * is repainted every call. Use the Compositor class when dirty tracking matters.
 *
 * @param staging  core-owned staging Surface
 * @param scene    the scene to paint
 * @param font     PSF2 font for the windows' title/body text
 */
void compose(Surface& staging, const Scene& scene, const PsfFont& font);

/**
 * @brief [P3-a] Paint a PaintList (widget tree's flattened output) into staging
 *
 * Walks @p list in order: fill_rect / fill_round_rect (P3-b paints real rounded
 * corners; until then falls back to a plain rect so the list stays paintable) /
 * text / clip push-pop. A clip stack intersects each kClipPush with its parent,
 * so a widget can never paint outside its ancestors' rects.
 *
 * @param staging  core-owned staging Surface
 * @param list     flattened draw cmds (from Widget::flatten)
 * @param font     PSF2 font for text cmds
 */
void execute(Surface& staging, const PaintList& list, const PsfFont& font,
             const ClipRect* outer = nullptr);

/**
 * @brief [P2-c] Stateful compositor with frame-to-frame dirty diff
 *
 * Holds the previous frame's Scene; compose() paints only the changed region
 * and reports it via @p dirty, so a host's render_frame can forward exactly
 * those rects through the pump flush loop (no whole-scene repaint per frame).
 */
class Compositor {
public:
    Compositor() = default;

    /**
     * @brief Paint @p scene into @p staging, reporting the changed Region
     *
     * @p dirty is cleared first, then filled with the rects that changed this
     * frame. Cases:
     *   - First call (or after invalidate()) -> full screen.
     *   - Background colour changed           -> full screen.
     *   - A window moved/changed/gone/added   -> that window's old ∪ new rect.
     *   - The cursor moved/changed            -> old ∪ new cursor rect.
     *   - Scene identical to previous         -> empty dirty, paints nothing.
     *
     * Each dirty rect is repainted with the WHOLE scene clipped to that rect;
     * z-ordering plus idempotent pixels make this correct even where a mover's
     * old and new footprints overlap, and it automatically repaints background
     * exposed by a moved window. The dirty Region never under-covers (moved
     * elements dirty both old and new positions; Region collapse on overflow
     * is an over-approximation).
     *
     * @param staging  core-owned staging Surface
     * @param scene    the scene to paint
     * @param font     PSF2 font for the windows' title/body text
     * @param dirty    out: the changed Region (may be nullptr -> no report)
     */
    void compose(Surface& staging, const Scene& scene, const PsfFont& font, Region* dirty);

    /** Force a full repaint on the next compose() (e.g. after staging resize). */
    void invalidate() { first_ = true; }

private:
    Scene prev_{};  // previous frame's scene snapshot
    bool  first_ = true;
};

}  // namespace cinux::gui

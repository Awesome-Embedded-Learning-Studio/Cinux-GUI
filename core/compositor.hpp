/**
 * @file core/compositor.hpp
 * @brief cinux::gui host-neutral scene compositor -- Scene -> staging pixels
 *
 * The convergence target for the three probe hosts' hand-written paint_scene:
 * given a Scene (P2-a) and the core-owned staging Surface, compose() paints the
 * whole scene via swraster in the exact order the hosts used -- background, then
 * each window in z order (face -> titlebar band -> edge outline -> title text ->
 * body text), then the cursor on top. A host's render_frame now reads "update
 * the Scene, then compose(staging, scene, font)" instead of re-deriving paint.
 *
 * P2-b paints the FULL scene every call (a drop-in replacement for today's
 * whole-staging repaint). Frame-to-frame dirty-region diff (paint only what
 * changed, return the dirty Region) arrives in P2-c.
 *
 * Zero host includes; depends only on scene/swraster/font (all core/).
 *
 * Compile condition: CINUX_GUI.
 * Namespace: cinux::gui
 */
#pragma once

#include "font.hpp"      // PsfFont
#include "scene.hpp"     // Scene
#include "swraster.hpp"  // Surface

namespace cinux::gui {

/**
 * @brief Paint a whole Scene into @p staging via swraster
 *
 * Order: background fill, then windows in ascending index (z) order -- face
 * fill, titlebar band (if titlebar_height > 0), 1px edge outline, title text,
 * body text -- then the cursor last (always on top). The whole staging surface
 * is repainted; callers that need dirty tracking wrap this in P2-c.
 *
 * @param staging  core-owned staging Surface (the host paints here)
 * @param scene    the scene to paint
 * @param font     PSF2 font for the windows' title/body text
 */
void compose(Surface& staging, const Scene& scene, const PsfFont& font);

}  // namespace cinux::gui

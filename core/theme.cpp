/**
 * @file core/theme.cpp
 * @brief cinux::gui Material theme -- light + dark palettes (P3-b)
 *
 * Colours are the Material baseline (XRGB8888 = 0x00RRGGBB). Flat subset: no
 * elevation ramp, no shadow -- callers pick surface vs a tinted variant.
 *
 * Compile condition: CINUX_GUI.
 */

#include "theme.hpp"

namespace cinux::gui {

Theme material_light() {
    return Theme{
        0x006200EEu,  // primary                (#6200EE)
        0x00FFFFFFu,  // on_primary             (white)
        0x00EADDFFu,  // primary_container      (#EADDFF, M3 light)
        0x0021005Du,  // on_primary_container   (#21005D)
        0x00FFFFFFu,  // surface                (white)
        0x001A1A1Au,  // on_surface             (near-black)
        0x00E7E0ECu,  // surface_variant        (#E7E0EC, muted)
        0x00F5F5F5u,  // background             (light grey)
        0x00B00020u,  // error                  (Material red)
        0x00BDBDBDu,  // outline                (grey)
        kRadiusMedium, kRadiusLarge,
    };
}

Theme material_dark() {
    return Theme{
        0x00BB86FCu,  // primary                (#BB86FC)
        0x00000000u,  // on_primary             (black)
        0x004F378Bu,  // primary_container      (#4F378B, M3 dark)
        0x00EADDFFu,  // on_primary_container   (#EADDFF)
        0x00121212u,  // surface                (dark grey)
        0x00FFFFFFu,  // on_surface             (white)
        0x0049454Fu,  // surface_variant        (#49454F, muted)
        0x002A1C12u,  // background             (warm dark coffee -- was pure black)
        0x00CF6679u,  // error
        0x00424242u,  // outline
        kRadiusMedium, kRadiusLarge,
    };
}

}  // namespace cinux::gui

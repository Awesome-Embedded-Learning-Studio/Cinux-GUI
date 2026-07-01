/**
 * @file core/scene.cpp
 * @brief cinux::gui scene model -- bounded window stack + NUL-terminated text
 *
 * Host-neutral, ZERO host includes. The Scene is pure data the Compositor
 * (P2-b) reads; this TU only manages the fixed-capacity stack and the byte-copy
 * of text into the window arrays (no <cstring> -- core stays stdint/stddef).
 *
 * Compile condition: CINUX_GUI.
 */

#include "scene.hpp"

#include <stdint.h>

namespace cinux::gui {

bool scene_add_window(Scene& sc, const Window& win) {
    if (!window_visible(win)) {
        return false;  // degenerate window -- dropped
    }
    if (sc.window_count >= kSceneMaxWindows) {
        return false;  // stack full -- dropped (raise kSceneMaxWindows if hit)
    }
    sc.windows[sc.window_count] = win;
    sc.window_count++;
    return true;
}

void window_set_title(Window& win, const char* text) {
    if (text == nullptr) {
        win.title[0] = '\0';
        return;
    }
    // Copy up to cap-1 bytes; always leave room for a trailing NUL. Hand-rolled
    // (no <cstring>) so core/ stays stdint/stddef only.
    uint32_t i = 0u;
    while (i + 1u < kWindowTitleLen && text[i] != '\0') {
        win.title[i] = text[i];
        i++;
    }
    win.title[i] = '\0';
}

void window_set_body(Window& win, const char* text) {
    if (text == nullptr) {
        win.body[0] = '\0';
        return;
    }
    uint32_t i = 0u;
    while (i + 1u < kWindowBodyLen && text[i] != '\0') {
        win.body[i] = text[i];
        i++;
    }
    win.body[i] = '\0';
}

}  // namespace cinux::gui

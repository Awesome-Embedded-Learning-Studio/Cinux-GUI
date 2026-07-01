/**
 * @file core/widget/window_manager.cpp
 * @brief cinux::gui WindowManager -- Z-order / raise / close / cursor (P4-b)
 *
 * Compile condition: CINUX_GUI.
 */

#include "window_manager.hpp"

#include <stdint.h>

namespace cinux::gui {

int32_t WindowManager::index_of_(Window* w) const {
    for (uint32_t i = 0; i < count_; ++i) {
        if (windows_[i] == w) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void WindowManager::close_cb_(void* ctx, Window* w) {
    static_cast<WindowManager*>(ctx)->remove_window(w);
}

void WindowManager::add_window(Window* w) {
    if (w == nullptr || count_ >= kMaxWindows) {
        return;
    }
    if (index_of_(w) >= 0) {
        return;  // already managed
    }
    windows_[count_++] = w;
    w->set_on_close(close_cb_, this);
    invalidate();  // P5-c: a new window appeared
}

void WindowManager::remove_window(Window* w) {
    const int32_t idx = index_of_(w);
    if (idx < 0) {
        return;
    }
    for (uint32_t i = static_cast<uint32_t>(idx); i + 1 < count_; ++i) {
        windows_[i] = windows_[i + 1];
    }
    windows_[count_ - 1] = nullptr;
    --count_;
    if (press_target_ == w) {
        press_target_ = nullptr;
    }
}

void WindowManager::raise(Window* w) {
    const int32_t idx = index_of_(w);
    if (idx < 0 || static_cast<uint32_t>(idx) == count_ - 1) {
        return;  // absent or already topmost
    }
    for (uint32_t i = static_cast<uint32_t>(idx); i + 1 < count_; ++i) {
        windows_[i] = windows_[i + 1];
    }
    windows_[count_ - 1] = w;
}

Widget* WindowManager::hit_test(int32_t x, int32_t y) {
    if (!visible_ || !rect_.contains(x, y)) {
        return nullptr;
    }
    /* topmost first: the last window in the list paints on top. Return the
     * whole window (not its content) so the WM drives raise/drag/close; window
     * contents in P4 have no pointer-interactive children. */
    for (uint32_t i = count_; i > 0; --i) {
        Window* w = windows_[i - 1];
        if (w->visible() && w->rect().contains(x, y)) {
            return w;
        }
    }
    return nullptr;  // desktop background
}

void WindowManager::process_pointer(const PointerPayload& p) {
    cursor_x_ = p.x;
    cursor_y_ = p.y;
    invalidate();  // P5-c: cursor moved and/or Z-order changed -> repaint

    if (p.kind == kPointerKindDown) {
        press_target_ = static_cast<Window*>(hit_test(p.x, p.y));
        if (press_target_ != nullptr) {
            raise(press_target_);          // click-to-raise
            press_target_->on_pointer(p);  // arm close / begin drag
        }
    } else if (p.kind == kPointerKindUp) {
        if (press_target_ != nullptr) {
            press_target_->on_pointer(p);  // may fire on_close -> remove_window
        }
        press_target_ = nullptr;
    } else {  // move: deliver to the capture target (drag), if any
        if (press_target_ != nullptr) {
            press_target_->on_pointer(p);
        }
    }
}

void WindowManager::paint_to_list(PaintList& list) const {
    /* 1. Desktop background. */
    list.fill_rect(rect_.x0, rect_.y0, rect_.width(), rect_.height(), bg_);

    /* 2. Windows bottom -> top (each flattens itself + its content). */
    for (uint32_t i = 0; i < count_; ++i) {
        windows_[i]->flatten(list);
    }

    /* 3. Software cursor on top of everything. */
    if (cursor_visible_) {
        list.fill_rect(cursor_x_, cursor_y_, kCursorSize, kCursorSize, 0x00FFFFFFu);
    }
}

}  // namespace cinux::gui

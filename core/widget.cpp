/**
 * @file core/widget.cpp
 * @brief cinux::gui widget tree -- flatten / hit-test / dispatch / render
 *
 * Host-neutral, ZERO host includes. render() borrows the Compositor's execute()
 * to paint the flattened PaintList.
 *
 * Compile condition: CINUX_GUI.
 */

#include "widget.hpp"

#include <stdint.h>

#include "compositor.hpp"  // execute
#include "font.hpp"        // PsfFont
#include "swraster.hpp"    // Surface

namespace cinux::gui {

void Widget::set_rect(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    rect_ = Rect{x, y, x + static_cast<int32_t>(w), y + static_cast<int32_t>(h)};
}

void Widget::add_child(Widget* w) {
    if (w == nullptr || child_count_ >= kMaxChildren) {
        return;  // full or null -> drop (caller keeps kMaxChildren generous)
    }
    children_[child_count_++] = w;
}

void Widget::flatten(PaintList& list) const {
    if (!visible_) {
        return;
    }
    list.clip_push(rect_.x0, rect_.y0, rect_.x1, rect_.y1);
    paint_to_list(list);  // virtual -- subclass drawing
    for (uint32_t i = 0u; i < child_count_; i++) {
        children_[i]->flatten(list);
    }
    list.clip_pop();
}

Widget* Widget::hit_test(int32_t x, int32_t y) {
    if (!visible_ || !rect_.contains(x, y)) {
        return nullptr;
    }
    // children last-to-first: later children paint on top, so they hit first
    for (uint32_t i = child_count_; i > 0u; i--) {
        if (Widget* hit = children_[i - 1u]->hit_test(x, y); hit != nullptr) {
            return hit;
        }
    }
    return this;
}

void Desktop::dispatch_pointer(const PointerPayload& p) {
    if (root_ == nullptr) {
        return;
    }
    if (p.kind == kPointerKindDown) {
        /* Press capture: the widget that got the down keeps receiving move/up,
         * so a drag stays tracked even when the pointer leaves it (Slider). */
        press_target_ = root_->hit_test(p.x, p.y);
        if (press_target_ != nullptr) {
            press_target_->on_pointer(p);
        }
    } else if (p.kind == kPointerKindUp) {
        if (press_target_ != nullptr) {
            press_target_->on_pointer(p);
        }
        press_target_ = nullptr;  // release capture
    } else if (press_target_ != nullptr) {
        /* Move while pressed: deliver to the capture target (not re-hit-tested,
         * so dragging off the thumb still moves it). */
        press_target_->on_pointer(p);
    }
    /* Move with no press_target is ignored -- P3 has no hover. */
}

void Desktop::render(Surface& staging, const PsfFont& font, Region* dirty) {
    if (dirty != nullptr) {
        dirty->clear();
        dirty->add(
            Rect{0, 0, static_cast<int32_t>(staging.width), static_cast<int32_t>(staging.height)});
    }
    if (root_ == nullptr) {
        return;
    }
    root_->layout();  // position children within their parent's rect
    PaintList list;
    root_->flatten(list);
    execute(staging, list, font);
}

}  // namespace cinux::gui

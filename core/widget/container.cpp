/**
 * @file core/widget/container.cpp
 * @brief Container + HBox + VBox -- bg fill + linear layout (P3-c)
 *
 * Compile condition: CINUX_GUI.
 */

#include "container.hpp"

#include <stdint.h>

namespace cinux::gui {

void Container::paint_to_list(PaintList& list) const {
    if (bg_ != 0u) {
        list.fill_rect(rect_.x0, rect_.y0, rect_.width(), rect_.height(), bg_);
    }
}

void HBox::layout() {
    const uint32_t n = child_count();
    if (n == 0u) {
        return;
    }
    const int32_t  pad  = static_cast<int32_t>(padding_);
    const int32_t  cx0  = rect_.x0 + pad;
    const int32_t  cy0  = rect_.y0 + pad;
    const uint32_t cw   = (rect_.width() > 2u * padding_) ? rect_.width() - 2u * padding_ : 0u;
    const uint32_t ch   = (rect_.height() > 2u * padding_) ? rect_.height() - 2u * padding_ : 0u;
    const uint32_t gaps = spacing_ * (n - 1u);
    const uint32_t cwid = (cw > gaps ? cw - gaps : 0u) / n;
    for (uint32_t i = 0u; i < n; i++) {
        const int32_t x = cx0 + static_cast<int32_t>(i * (cwid + spacing_));
        Widget*       c = child(i);
        c->set_rect(x, cy0, cwid, ch);
        c->layout();  // settle nested containers
    }
}

void VBox::layout() {
    const uint32_t n = child_count();
    if (n == 0u) {
        return;
    }
    const int32_t  pad  = static_cast<int32_t>(padding_);
    const int32_t  cx0  = rect_.x0 + pad;
    const int32_t  cy0  = rect_.y0 + pad;
    const uint32_t cw   = (rect_.width() > 2u * padding_) ? rect_.width() - 2u * padding_ : 0u;
    const uint32_t ch   = (rect_.height() > 2u * padding_) ? rect_.height() - 2u * padding_ : 0u;
    const uint32_t gaps = spacing_ * (n - 1u);
    const uint32_t chgt = (ch > gaps ? ch - gaps : 0u) / n;
    for (uint32_t i = 0u; i < n; i++) {
        const int32_t y = cy0 + static_cast<int32_t>(i * (chgt + spacing_));
        Widget*       c = child(i);
        c->set_rect(cx0, y, cw, chgt);
        c->layout();
    }
}

}  // namespace cinux::gui

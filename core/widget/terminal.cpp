/**
 * @file core/widget/terminal.cpp
 * @brief cinux::gui TerminalWidget -- write/scroll/paint (P4-c)
 *
 * Compile condition: CINUX_GUI.
 */

#include "terminal.hpp"

#include <stdint.h>

namespace cinux::gui {

void TerminalWidget::set_cols_rows(uint32_t cols, uint32_t rows) {
    cols_ = (cols > kMaxCols) ? kMaxCols : cols;
    rows_ = (rows > kMaxRows) ? kMaxRows : rows;
    clear();
}

void TerminalWidget::clear() {
    for (uint32_t i = 0; i < kMaxCols * kMaxRows; ++i) {
        cells_[i] = 0;
    }
    cur_col_    = 0;
    cur_row_    = 0;
    ansi_state_ = AnsiState::kNormal;
    invalidate();  // P5-c: grid cleared
}

char TerminalWidget::cell_at(uint32_t col, uint32_t row) const {
    if (col >= kMaxCols || row >= kMaxRows) {
        return 0;
    }
    return cells_[row * kMaxCols + col];
}

void TerminalWidget::scroll_up_() {
    for (uint32_t r = 1; r < rows_; ++r) {
        for (uint32_t c = 0; c < kMaxCols; ++c) {
            cells_[(r - 1) * kMaxCols + c] = cells_[r * kMaxCols + c];
        }
    }
    for (uint32_t c = 0; c < kMaxCols; ++c) {
        cells_[(rows_ - 1) * kMaxCols + c] = 0;
    }
}

void TerminalWidget::newline_() {
    cur_col_ = 0;
    ++cur_row_;
    if (cur_row_ >= rows_) {
        scroll_up_();
        cur_row_ = rows_ - 1;
    }
}

void TerminalWidget::put_char_(char ch) {
    /* ANSI/VT100 escape consumer: swallow escape sequences whole so they never
     * land in the cell grid. Only kNormal falls through to byte handling. */
    switch (ansi_state_) {
        case AnsiState::kEsc:
            if (ch == '[') {
                ansi_state_ = AnsiState::kCsi;
            } else if (ch == ']') {
                ansi_state_ = AnsiState::kOsc;
            } else {
                ansi_state_ = AnsiState::kNormal;  // ESC + single byte: consumed
            }
            return;
        case AnsiState::kCsi:
            if (ch >= 0x40 && ch <= 0x7E) {  // final byte ends CSI
                ansi_state_ = AnsiState::kNormal;
            }
            return;
        case AnsiState::kOsc:
            if (ch == 0x07) {  // BEL ends OSC (ST/ESC\ not handled -- rare)
                ansi_state_ = AnsiState::kNormal;
            }
            return;
        case AnsiState::kNormal:
        default:
            if (ch == 0x1B) {  // ESC starts an escape
                ansi_state_ = AnsiState::kEsc;
                return;
            }
            break;  // fall through to normal byte handling
    }

    switch (ch) {
        case '\n':
            newline_();
            return;
        case '\r':
            cur_col_ = 0;
            return;
        case '\b':
            if (cur_col_ > 0) {
                --cur_col_;
            }
            return;
        case '\t':
            cur_col_ = (cur_col_ / 8u + 1u) * 8u;
            if (cur_col_ >= cols_) {
                newline_();
            }
            return;
        default:
            break;
    }
    if (static_cast<uint8_t>(ch) < 0x20u) {
        return;  // other control bytes: drop
    }
    cells_[cur_row_ * kMaxCols + cur_col_] = ch;
    ++cur_col_;
    if (cur_col_ >= cols_) {
        newline_();  // auto-wrap at the right edge
    }
}

void TerminalWidget::write(const char* data, uint32_t len) {
    if (data == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < len; ++i) {
        put_char_(data[i]);
    }
    invalidate();  // P5-c: cells changed
}

void TerminalWidget::write(const char* str) {
    if (str == nullptr) {
        return;
    }
    for (const char* p = str; *p != '\0'; ++p) {
        put_char_(*p);
    }
    invalidate();  // P5-c: cells changed
}

void TerminalWidget::paint_to_list(PaintList& list) const {
    const uint32_t bg = (theme_ != nullptr) ? theme_->surface : 0x00000000u;     // black
    const uint32_t fg = (theme_ != nullptr) ? theme_->on_surface : 0x00FFFFFFu;  // white

    list.fill_rect(rect_.x0, rect_.y0, rect_.width(), rect_.height(), bg);
    for (uint32_t r = 0; r < rows_; ++r) {
        for (uint32_t c = 0; c < cols_; ++c) {
            const char ch = cells_[r * kMaxCols + c];
            if (ch == 0) {
                continue;  // empty cell -- nothing to draw
            }
            list.text_glyph(rect_.x0 + static_cast<int32_t>(c * kGlyphW),
                            rect_.y0 + static_cast<int32_t>(r * kGlyphH), fg, ch);
        }
    }
}

}  // namespace cinux::gui

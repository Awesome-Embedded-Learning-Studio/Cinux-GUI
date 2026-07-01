/**
 * @file core/widget/terminal.cpp
 * @brief cinux::gui TerminalWidget -- write/scroll/paint + ANSI SGR/cursor/clear (P4-c, P5-e)
 *
 * Compile condition: CINUX_GUI.
 */

#include "terminal.hpp"

#include <stdint.h>

namespace cinux::gui {

namespace {
/* Standard 16-colour ANSI palette (XRGB8888). Index 0-7 normal, 8-15 bright.
 * Used by paint_to_list to turn a cell's SGR fg index into a pixel colour. */
const uint32_t kAnsiPalette[16] = {
    0x00000000u, 0x00800000u, 0x00008000u, 0x00808000u,  // black red green yellow
    0x00000080u, 0x00800080u, 0x00008080u, 0x00C0C0C0u,  // blue magenta cyan white
    0x00808080u, 0x00FF0000u, 0x0000FF00u, 0x00FFFF00u,  // bright black/red/green/yellow
    0x000000FFu, 0x00FF00FFu, 0x0000FFFFu, 0x00FFFFFFu,  // bright blue/magenta/cyan/white
};
}  // namespace

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
    cur_fg_     = 7u;  // P5-e: default white
    ansi_state_ = AnsiState::kNormal;
    csi_len_    = 0u;
    invalidate();  // P5-c: grid cleared
}

char TerminalWidget::cell_at(uint32_t col, uint32_t row) const {
    if (col >= kMaxCols || row >= kMaxRows) {
        return 0;
    }
    return cells_[row * kMaxCols + col];
}

uint8_t TerminalWidget::fg_at(uint32_t col, uint32_t row) const {
    if (col >= kMaxCols || row >= kMaxRows) {
        return 0u;
    }
    return fg_colors_[row * kMaxCols + col];  // P5-e
}

void TerminalWidget::scroll_up_() {
    for (uint32_t r = 1; r < rows_; ++r) {
        for (uint32_t c = 0; c < kMaxCols; ++c) {
            const uint32_t src = r * kMaxCols + c;
            const uint32_t dst = (r - 1) * kMaxCols + c;
            cells_[dst]        = cells_[src];
            fg_colors_[dst]    = fg_colors_[src];  // P5-e: colour scrolls with the char
        }
    }
    for (uint32_t c = 0; c < kMaxCols; ++c) {
        cells_[(rows_ - 1) * kMaxCols + c]     = 0;
        fg_colors_[(rows_ - 1) * kMaxCols + c] = 0;
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

void TerminalWidget::apply_sgr_() {
    /* Parse ;-separated decimal codes from csi_param_ (empty param = 0 = reset). */
    uint32_t   code  = 0;
    bool       has   = false;
    const auto apply = [this](uint32_t c) {
        if (c == 0u || c == 39u) {
            cur_fg_ = 7u;  // reset / default fg = white
        } else if (c >= 30u && c <= 37u) {
            cur_fg_ = static_cast<uint8_t>(c - 30u);
        } else if (c >= 90u && c <= 97u) {
            cur_fg_ = static_cast<uint8_t>(c - 90u + 8u);  // bright
        }
        // bold(1) / bg(40-47) / etc. ignored for now
    };
    for (uint8_t i = 0; i < csi_len_; ++i) {
        const char b = csi_param_[i];
        if (b >= '0' && b <= '9') {
            code = code * 10u + static_cast<uint32_t>(b - '0');
            has  = true;
        } else if (b == ';') {
            apply(has ? code : 0u);
            code = 0;
            has  = false;
        }
    }
    apply(has ? code : 0u);
}

void TerminalWidget::dispatch_csi_(char final_byte) {
    switch (final_byte) {
        case 'm':
            apply_sgr_();
            break;
        case 'H':
        case 'f': {  // cursor position [row;colH (1-based; 0/empty treated as 1)
            uint32_t r    = 1;
            uint32_t c    = 1;
            uint32_t cur  = 0;
            bool     has  = false;
            uint8_t  slot = 0;
            for (uint8_t i = 0; i < csi_len_; ++i) {
                const char b = csi_param_[i];
                if (b >= '0' && b <= '9') {
                    cur = cur * 10u + static_cast<uint32_t>(b - '0');
                    has = true;
                } else if (b == ';') {
                    if (slot == 0u) {
                        r = has ? cur : 1u;
                    } else if (slot == 1u) {
                        c = has ? cur : 1u;
                    }
                    ++slot;
                    cur = 0;
                    has = false;
                }
            }
            if (slot == 0u) {
                r = has ? cur : 1u;
            } else if (slot == 1u) {
                c = has ? cur : 1u;
            }
            if (r == 0u) {
                r = 1u;
            }
            if (c == 0u) {
                c = 1u;
            }
            cur_row_ = (r - 1u < rows_) ? r - 1u : rows_ - 1u;
            cur_col_ = (c - 1u < cols_) ? c - 1u : cols_ - 1u;
            break;
        }
        case 'J': {  // [2J = clear whole grid (other modes ignored)
            uint32_t n   = 0;
            bool     has = false;
            for (uint8_t i = 0; i < csi_len_; ++i) {
                if (csi_param_[i] >= '0' && csi_param_[i] <= '9') {
                    n   = n * 10u + static_cast<uint32_t>(csi_param_[i] - '0');
                    has = true;
                }
            }
            if ((has ? n : 0u) == 2u) {
                for (uint32_t i = 0; i < kMaxCols * kMaxRows; ++i) {
                    cells_[i] = 0;
                }
                invalidate();
            }
            break;
        }
        default:
            break;  // other CSI: ignored
    }
    csi_len_ = 0;  // reset param buffer for the next CSI
}

void TerminalWidget::put_char_(char ch) {
    switch (ansi_state_) {
        case AnsiState::kEsc:
            if (ch == '[') {
                csi_len_    = 0u;
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
                dispatch_csi_(ch);
            } else if (ch >= 0x20 && ch <= 0x3F && csi_len_ < sizeof(csi_param_)) {
                csi_param_[csi_len_++] = ch;  // collect param/intermediate bytes
            }
            return;
        case AnsiState::kOsc:
            if (ch == 0x07) {  // BEL ends OSC
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
    const uint32_t idx = cur_row_ * kMaxCols + cur_col_;
    cells_[idx]        = ch;
    fg_colors_[idx]    = cur_fg_;  // P5-e: stamp the current SGR fg on this cell
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
    const uint32_t bg = (theme_ != nullptr) ? theme_->surface : 0x00000000u;  // black

    list.fill_rect(rect_.x0, rect_.y0, rect_.width(), rect_.height(), bg);
    for (uint32_t r = 0; r < rows_; ++r) {
        for (uint32_t c = 0; c < cols_; ++c) {
            const uint32_t idx = r * kMaxCols + c;
            const char     ch  = cells_[idx];
            if (ch == 0) {
                continue;  // empty cell
            }
            const uint32_t cfg = kAnsiPalette[fg_colors_[idx] & 0xFu];  // P5-e: per-cell colour
            list.text_glyph(rect_.x0 + static_cast<int32_t>(c * kGlyphW),
                            rect_.y0 + static_cast<int32_t>(r * kGlyphH), cfg, ch);
        }
    }
}

}  // namespace cinux::gui

/**
 * @file test/test_terminal.cpp
 * @brief P4-c TerminalWidget -- write, cursor, newline/wrap, scroll, paint
 *
 * Feeds bytes via write() and asserts:
 *   - printable chars land in cells and advance the cursor
 *   - \n moves to col 0 of the next row
 *   - \r returns to col 0 (same row)
 *   - \b steps the cursor back one
 *   - writing past the bottom scrolls the grid up one line
 *   - flatten yields a bg fill + one kTextGlyph per non-empty cell
 *
 * Standalone ctest. Pure logic (no host, no framebuffer).
 *
 * Compile condition: CINUX_GUI (via the core lib).
 */
#include <stdint.h>

#include <cassert>
#include <cstdio>

#include "paint_list.hpp"       // PaintList, CmdKind
#include "widget.hpp"           // Widget
#include "widget/terminal.hpp"  // TerminalWidget

using namespace cinux::gui;

int main() {
    /* --- 1. printable chars land + advance cursor --- */
    {
        TerminalWidget t;
        t.write("Hi");
        assert(t.cell_at(0, 0) == 'H');
        assert(t.cell_at(1, 0) == 'i');
        assert(t.cursor_col() == 2 && t.cursor_row() == 0);
    }

    /* --- 2. \n moves to col 0 of the next row --- */
    {
        TerminalWidget t;
        t.write("AB\nCD");
        assert(t.cell_at(0, 0) == 'A');
        assert(t.cell_at(1, 0) == 'B');
        assert(t.cell_at(0, 1) == 'C');
        assert(t.cell_at(1, 1) == 'D');
        assert(t.cursor_col() == 2 && t.cursor_row() == 1);
    }

    /* --- 3. \r returns to col 0 (overwrite) --- */
    {
        TerminalWidget t;
        t.write("AB\rC");
        assert(t.cell_at(0, 0) == 'C');  // overwrote 'A'
        assert(t.cell_at(1, 0) == 'B');
        assert(t.cursor_col() == 1);
    }

    /* --- 4. writing past the bottom scrolls up --- */
    {
        TerminalWidget t;
        t.set_cols_rows(10, 3);
        t.write("1\n2\n3\n4");  // 4 lines into 3 rows -> scroll once
        assert(t.cell_at(0, 0) == '2');
        assert(t.cell_at(0, 1) == '3');
        assert(t.cell_at(0, 2) == '4');
        assert(t.cursor_row() == 2);
    }

    /* --- 5. \b steps the cursor back --- */
    {
        TerminalWidget t;
        t.write("AB\bC");
        assert(t.cell_at(0, 0) == 'A');
        assert(t.cell_at(1, 0) == 'C');  // overwrote 'B' after backspace
        assert(t.cursor_col() == 2);
    }

    /* --- 6. flatten: bg fill + one kTextGlyph per non-empty cell --- */
    {
        TerminalWidget t;
        t.set_cols_rows(10, 3);
        t.set_rect(0, 0, 80, 48);
        t.write("X");

        PaintList list;
        t.flatten(list);

        bool has_fill  = false;
        bool has_glyph = false;
        for (uint32_t i = 0; i < list.count(); ++i) {
            const PaintCmd& c = list.at(i);
            if (c.kind == CmdKind::kFillRect) {
                has_fill = true;
            }
            if (c.kind == CmdKind::kTextGlyph) {
                has_glyph = true;
            }
        }
        assert(has_fill && has_glyph);
    }

    std::printf("test_terminal: OK\n");
    return 0;
}

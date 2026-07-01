/**
 * @file core/paint_list.hpp
 * @brief cinux::gui PaintList -- ordered draw cmds (widget tree -> Compositor)
 *
 * P3's scene source. Each frame the widget tree flattens into a PaintList
 * (fill_rect / fill_round_rect / text / text_glyph / clip push-pop); execute()
 * walks it and paints into the staging Surface. This REPLACES P2's Scene as the
 * scene source -- a widget tree produces a richer list than a fixed Scene.
 *
 * Two text cmds: `text` (NUL-terminated const char*, borrowed for the frame --
 * the owning widget keeps its buffer alive until execute()) and `text_glyph`
 * (a single char inlined in the cmd, no borrowing). text_glyph is for
 * char-dense widgets like a terminal, whose per-cell characters cannot each
 * borrow a stable buffer.
 *
 * Fixed-capacity (no <vector>): overflow drops cmds (kMaxCmds is sized for a
 * full 80x25 terminal plus the rest of the tree; raise if ever hit).
 *
 * Pure C++17 (stdint/stddef only), ZERO host includes.
 *
 * Compile condition: CINUX_GUI.
 * Namespace: cinux::gui
 */
#pragma once

#include <stdint.h>

namespace cinux::gui {

/* Draw-command kinds. kFillRoundRect executes the fill_rounded_rect swraster
 * primitive; kText renders a NUL-terminated string; kTextGlyph renders one
 * inlined character (no borrowed pointer). */
enum class CmdKind : uint8_t {
    kFillRect,
    kFillRoundRect,
    kText,
    kTextGlyph,
    kClipPush,
    kClipPop,
};

struct FillRectCmd {
    int32_t  x;
    int32_t  y;
    uint32_t w;
    uint32_t h;
    uint32_t color;
};

struct FillRoundRectCmd {
    int32_t  x;
    int32_t  y;
    uint32_t w;
    uint32_t h;
    uint32_t color;
    uint32_t radius;
};

struct TextCmd {
    int32_t     x;
    int32_t     y;
    uint32_t    color;
    const char* text;  // borrowed for the frame (caller-owned, NUL-terminated)
};

/* Single inlined character -- no borrowed pointer, so per-cell chars in a
 * terminal can render without a stable per-cell buffer. */
struct TextGlyphCmd {
    int32_t  x;
    int32_t  y;
    uint32_t color;
    char     ch;
};

struct ClipPushCmd {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
};

/* One command. Union of POD payloads (anonymous union; access via .fill etc). */
struct PaintCmd {
    CmdKind kind;
    union {
        FillRectCmd      fill;
        FillRoundRectCmd rfill;
        TextCmd          text;
        TextGlyphCmd     glyph;
        ClipPushCmd      clip;
    };
};

/**
 * @brief Fixed-capacity ordered list of PaintCmds
 *
 * A widget tree flattens into one PaintList per frame; execute() paints it.
 * Overflow drops further cmds (no abort -- DIRECTIVES "core never aborts").
 */
class PaintList {
public:
    /* Sized for a char-dense widget (a full 80x25 terminal = 2000 glyph cmds)
     * plus the surrounding tree, without overflowing the stack (~128 KB at
     * sizeof(PaintCmd) ~= 32). */
    static constexpr uint32_t kMaxCmds = 4096;

    void            clear() { count_ = 0u; }
    uint32_t        count() const { return count_; }
    const PaintCmd* cmds() const { return cmds_; }
    const PaintCmd& at(uint32_t i) const { return cmds_[i]; }

    void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
    void fill_round_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color,
                         uint32_t radius);
    void text(int32_t x, int32_t y, uint32_t color, const char* str);
    /** Inlined single-char draw (no borrowed pointer) -- for char-dense widgets. */
    void text_glyph(int32_t x, int32_t y, uint32_t color, char ch);
    void clip_push(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void clip_pop();

private:
    PaintCmd cmds_[kMaxCmds];
    uint32_t count_ = 0u;
};

}  // namespace cinux::gui

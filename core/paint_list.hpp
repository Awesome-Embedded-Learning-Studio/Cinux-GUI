/**
 * @file core/paint_list.hpp
 * @brief cinux::gui PaintList -- ordered draw cmds (widget tree -> Compositor)
 *
 * P3's scene source. Each frame the widget tree flattens into a PaintList
 * (fill_rect / fill_round_rect / text / clip push-pop); execute() walks it and
 * paints into the staging Surface. This REPLACES P2's Scene as the scene source
 * -- a widget tree produces a richer list than a fixed Scene of windows.
 *
 * Fixed-capacity (no <vector>): overflow drops cmds (kMaxCmds is generous; raise
 * if hit). Text cmds hold a const char* borrowed for the frame -- the owning
 * widget keeps its text buffer alive until execute() runs that frame.
 *
 * Pure C++17 (stdint/stddef only), ZERO host includes.
 *
 * Compile condition: CINUX_GUI.
 * Namespace: cinux::gui
 */
#pragma once

#include <stdint.h>

namespace cinux::gui {

/* Draw-command kinds. kFillRoundRect executes fully once P3-b adds the
 * fill_rounded_rect swraster primitive; until then execute() falls back to a
 * plain rect so the list stays paintable. */
enum class CmdKind : uint8_t {
    kFillRect,
    kFillRoundRect,
    kText,
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
    static constexpr uint32_t kMaxCmds = 256;

    void            clear() { count_ = 0u; }
    uint32_t        count() const { return count_; }
    const PaintCmd* cmds() const { return cmds_; }
    const PaintCmd& at(uint32_t i) const { return cmds_[i]; }

    void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
    void fill_round_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color,
                         uint32_t radius);
    void text(int32_t x, int32_t y, uint32_t color, const char* str);
    void clip_push(int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void clip_pop();

private:
    PaintCmd cmds_[kMaxCmds];
    uint32_t count_ = 0u;
};

}  // namespace cinux::gui

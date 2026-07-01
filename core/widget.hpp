/**
 * @file core/widget.hpp
 * @brief cinux::gui widget tree -- host-neutral control layer (P3)
 *
 * A Widget owns a rectangle, optional children, and three virtual hooks:
 *   - paint_to_list: append this widget's draw cmds (clip already set to rect)
 *   - hit_test:      which widget a pointer at (x,y) targets (default: rect)
 *   - on_pointer:    receive a pointer event routed to this widget
 *
 * Desktop drives the tree: dispatch_pointer hit-tests + delivers (no bubbling,
 * per P3 decision C); render flattens to a PaintList + executes it. P3-a is the
 * skeleton + routing; Label/Button/Container/Slider arrive in P3-c/d.
 *
 * Pure C++17 (stdint/stddef + core/ only), ZERO host includes. Virtual dispatch
 * is fine (not RTTI, which is banned); children are a fixed array (no <vector>).
 *
 * Compile condition: CINUX_GUI.
 * Namespace: cinux::gui
 */
#pragma once

#include <stdint.h>

#include "event_payload.hpp"  // PointerPayload
#include "paint_list.hpp"     // PaintList
#include "region.hpp"         // Rect

namespace cinux::gui {

struct Surface;  // forward (swraster.hpp) -- render takes a reference
class PsfFont;   // forward (font.hpp)

/**
 * @brief Base widget: rectangle + children + virtual paint/hit/event hooks
 *
 * Subclass to make a real control (Label/Button/...). The framework (flatten,
 * hit_test) handles clip + recursion; subclasses only implement the protected
 * paint_to_list and the public on_pointer / hit_test overrides they need.
 */
class Widget {
public:
    static constexpr uint32_t kMaxChildren = 16;

    virtual ~Widget() = default;

    void set_rect(int32_t x, int32_t y, uint32_t w, uint32_t h);
    Rect rect() const { return rect_; }
    void set_visible(bool v) { visible_ = v; }
    bool visible() const { return visible_; }

    /** Flex weight for HBox/VBox layout (P5-d). Default 1 = equal share. */
    void     set_flex(uint32_t f) { flex_ = f; }
    uint32_t flex() const { return flex_; }

    void     add_child(Widget* w);
    uint32_t child_count() const { return child_count_; }
    Widget*  child(uint32_t i) const { return i < child_count_ ? children_[i] : nullptr; }

    /**
     * @brief Framework paint: clip to this rect, paint self, recurse children
     *
     * Non-virtual -- the framework entry. Subclasses override the protected
     * paint_to_list for their own drawing; flatten handles clip push/pop and
     * recursion so a widget can never paint outside its ancestors' rects.
     */
    void flatten(PaintList& list) const;

    /**
     * @brief Find the widget a pointer at (x,y) targets (topmost first)
     *
     * Default recurses children last-to-first (later children paint on top),
     * then this widget if visible and contains the point. Override for
     * non-rectangular hit shapes (e.g. a round button).
     */
    virtual Widget* hit_test(int32_t x, int32_t y);

    /** Receive a pointer event routed to this widget. Default: noop. */
    virtual void on_pointer(const PointerPayload& p) { (void)p; }

    /**
     * @brief Recompute children's rects (containers override; P3-c)
     *
     * Default noop. Containers (HBox/VBox) position their children within this
     * rect and call layout() on each so nested containers settle. Desktop::render
     * calls root->layout() before paint.
     */
    virtual void layout() {}

    /** Mark this widget (and ancestors) as needing repaint. P5-c: only call on
     * visible state change (press/value/text/cursor/move) -- set_rect does NOT
     * invalidate (it is a layout primitive called every frame by containers). */
    void invalidate() {
        dirty_ = true;
        if (parent_ != nullptr) {
            parent_->invalidate();
        }
    }
    /** True if this widget asked for a repaint since the last clear_dirty(). */
    bool is_dirty() const { return dirty_; }
    /** Clear dirty on this widget and all descendants. */
    void clear_dirty() {
        dirty_ = false;
        for (uint32_t i = 0u; i < child_count_; ++i) {
            children_[i]->clear_dirty();
        }
    }

protected:
    /** Subclass drawing (clip already set to this widget's rect). Default: noop. */
    virtual void paint_to_list(PaintList& list) const { (void)list; }

    Rect     rect_{};
    bool     visible_                = true;
    Widget*  children_[kMaxChildren] = {};
    uint32_t child_count_            = 0u;
    Widget*  parent_                 = nullptr;  // P5-c: set by add_child
    bool     dirty_                  = true;     // P5-c: repaint requested (start dirty)
    uint32_t flex_                   = 1u;       // P5-d: HBox/VBox weight (1 = equal)
};

/**
 * @brief Desktop: owns the widget tree root, drives dispatch + render
 *
 * dispatch_pointer hit-tests from the root and delivers the event to the
 * target (no bubbling). render flattens the tree to a PaintList and executes
 * it into the staging Surface. P3-a always reports a full-screen dirty Region
 * (the tree repaints wholesale each frame); per-widget dirty flags arrive
 * later.
 */
class Desktop {
public:
    void    set_root(Widget* root) { root_ = root; }
    Widget* root() const { return root_; }

    /** Hit-test + deliver @p p to the target widget (no event bubbling). */
    void dispatch_pointer(const PointerPayload& p);

    /**
     * @brief Flatten the tree + paint into @p staging
     *
     * @p dirty (optional) receives the changed Region. P3-a reports the full
     * screen every frame; tighter dirty tracking arrives with widget dirty flags.
     */
    void render(Surface& staging, const PsfFont& font, Region* dirty);

private:
    Widget* root_         = nullptr;
    Widget* press_target_ = nullptr;  // P3-d: press capture (drag tracks the press widget)
};

}  // namespace cinux::gui

#ifndef RELLU_TOOLS_LAYOUT_HANDLES_HPP
#define RELLU_TOOLS_LAYOUT_HANDLES_HPP
#include <optional>
#include <vector>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Edits a repeat grid selected alone and the spacing of stacks on the
   * canvas as Adobe XD does. A repeat grid shows a rounded white handle
   * outlined in green across the middle of its right and bottom edges,
   * where get_grid_handle_rect places them, filled green while hovered or
   * dragged, which is dragged to add or remove columns and rows. The gap
   * under the pointer between a repeat grid's columns or rows is filled in
   * pink, lined along its sides and dragged to change that spacing, and
   * while one is dragged every gap between its columns, or between its
   * rows, is filled and labelled with the spacing above the grid or left
   * of it. Only the first gap between columns or rows is shown when they
   * lie too close together for its label. The gaps between the items of
   * the stack entered, and of a stack selected alone while an activation
   * key is held, are dragged to change the spacing after the item before
   * the gap, or after every item while Shift is held, labelled Stack
   * Spacing. The gap under the pointer is filled in pink and labelled with
   * its size, a selected stack's in a deeper pink lined along its sides,
   * and while Shift is held the stack's other gaps are filled in pale pink.
   * Each drag is one undoable step.
   */
  class LayoutHandles {
    public:

      /** The radius in logical units rounding a repeat grid's handle. */
      static constexpr auto HANDLE_RADIUS = 4.5;

      /** The least width in logical units within which a gap is hit. */
      static constexpr auto GAP_REACH = 4.0;

      /** The largest spacing a stack's gap is dragged to. */
      static constexpr auto MAXIMUM_SPACING = 10000.0;

      /** Constructs a LayoutHandles without a gesture in progress. */
      LayoutHandles();

      /**
       * Returns the repeat grid selected alone and unlocked, or nil if the
       * selection is anything else.
       * @param context The context worked in.
       */
      NodeId get_grid(const ToolContext& context) const;

      /**
       * Returns the stacks whose gaps are dragged: the stack selected alone
       * and unlocked while an activation key is held, then the stack
       * entered.
       * @param context The context worked in.
       */
      std::vector<NodeId> get_stacks(const ToolContext& context) const;

      /**
       * Records a key pressed, the S, backtick and tilde keys activating
       * the gaps of a stack selected alone.
       * @param event The key pressed.
       * @return <code>true</code> iff the gaps became active.
       */
      bool key_press(const KeyEvent& event);

      /**
       * Records a key released.
       * @param event The key released.
       * @return <code>true</code> iff the gaps stopped being active.
       */
      bool key_release(const KeyEvent& event);

      /**
       * Highlights what lies under a position and returns the pointer to
       * show there.
       * @param context The context worked in.
       * @param position The position in logical units.
       * @param modifiers The modifier keys held.
       * @param is_handle_hit Whether a handle of the selection lies at the
       *        <i>position</i>, above the gaps of the stack entered.
       * @return The pointer, or <code>std::nullopt</code> if no handle or
       *         gap is there.
       */
      std::optional<ToolCursor> hover(const ToolContext& context,
        Point position, Qt::KeyboardModifiers modifiers = {},
        bool is_handle_hit = false);

      /**
       * Starts dragging what lies under a press.
       * @param context The context worked in.
       * @param event The press.
       * @param is_handle_hit Whether a handle of the selection lies under
       *        the press, above the gaps of the stack entered.
       * @return <code>true</code> iff a drag started.
       */
      bool press(ToolContext& context, const PointerEvent& event,
        bool is_handle_hit = false);

      /**
       * Continues the drag.
       * @param context The context worked in.
       * @param event The pointer move.
       */
      void move(ToolContext& context, const PointerEvent& event);

      /**
       * Ends the drag.
       * @param context The context worked in.
       */
      void release(ToolContext& context);

      /**
       * Abandons the drag, reverting what it changed.
       * @param context The context worked in.
       */
      void cancel(ToolContext& context);

      /** Returns <code>true</code> iff a drag is in progress. */
      bool is_active() const;

      /**
       * Draws the grid's outline and handles and the highlighted gap.
       * @param context The context worked in.
       * @param overlay The overlay to draw into.
       */
      void draw(const ToolContext& context, CanvasOverlay& overlay) const;

    private:
      enum class Part : unsigned char {
        NONE,
        RIGHT,
        BOTTOM,
        COLUMN_GAP,
        ROW_GAP,
        STACK_GAP
      };
      struct Target {
        Part m_part;
        int m_index;
        NodeId m_node;
      };
      NodeId m_node;
      NodeId m_item;
      Target m_target;
      Target m_hover;
      Point m_press;
      double m_initial;
      double m_spacing;
      bool m_is_applied;
      bool m_is_all;
      bool m_is_activated;
      bool m_is_shift;

      std::optional<Target> find(const ToolContext& context, Point position,
        bool is_handle_hit) const;
      bool is_entered(const ToolContext& context, const NodeId& id) const;
      std::optional<Rect> get_gap(const ToolContext& context,
        const Target& target, bool is_widened = true) const;
      void draw_stack(const ToolContext& context, CanvasOverlay& overlay,
        const Target& shown) const;
  };
}

#endif

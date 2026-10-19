#ifndef RELLU_TOOLS_LAYOUT_HANDLES_HPP
#define RELLU_TOOLS_LAYOUT_HANDLES_HPP
#include <optional>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Edits a repeat grid or stack selected alone on the canvas as Adobe XD
   * does. A repeat grid's bounds are outlined in green with a handle at the
   * middle of the right and bottom edges, dragged to add or remove columns
   * and rows. The gap under the pointer between a repeat grid's columns or
   * rows, or between two items of a stack, is highlighted in pink and
   * dragged to change that spacing. Each drag is one undoable step.
   */
  class LayoutHandles {
    public:

      /** The length in logical units of a handle along its edge. */
      static constexpr auto HANDLE_LENGTH = 18.0;

      /** The thickness in logical units of a handle. */
      static constexpr auto HANDLE_THICKNESS = 6.0;

      /** The distance in logical units within which a handle is hit. */
      static constexpr auto HANDLE_REACH = 6.0;

      /** The least width in logical units within which a gap is hit. */
      static constexpr auto GAP_REACH = 6.0;

      /** Constructs a LayoutHandles without a gesture in progress. */
      LayoutHandles();

      /**
       * Returns the repeat grid selected alone and unlocked, or nil if the
       * selection is anything else.
       * @param context The context worked in.
       */
      NodeId get_grid(const ToolContext& context) const;

      /**
       * Returns the stack selected alone and unlocked, or nil if the
       * selection is anything else.
       * @param context The context worked in.
       */
      NodeId get_stack(const ToolContext& context) const;

      /**
       * Returns the pointer to show at a position, or
       * <code>std::nullopt</code> if no handle or gap is there.
       * @param context The context worked in.
       * @param position The position in logical units.
       */
      std::optional<ToolCursor> hover(
        const ToolContext& context, Point position);

      /**
       * Starts dragging what lies under a press.
       * @param context The context worked in.
       * @param event The press.
       * @return <code>true</code> iff a drag started.
       */
      bool press(ToolContext& context, const PointerEvent& event);

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
      };
      NodeId m_node;
      NodeId m_item;
      Target m_target;
      Target m_hover;
      Point m_press;
      double m_initial;
      bool m_is_applied;

      NodeId get_subject(const ToolContext& context) const;
      std::optional<Target> find(
        const ToolContext& context, Point position) const;
      std::optional<Rect> get_gap(const ToolContext& context,
        const NodeId& id, const Target& target) const;
  };
}

#endif

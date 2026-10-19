#ifndef RELLU_TOOLS_DRAW_TOOL_HPP
#define RELLU_TOOLS_DRAW_TOOL_HPP
#include <memory>
#include <optional>
#include <string>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Draws a new node by dragging, as Adobe XD's drawing tools do. The node
   * appears as soon as the drag spans it, follows the pointer and the
   * modifier keys, and is selected; the whole drag is one undoable step.
   * Escape abandons a drag and otherwise returns to the select tool. The
   * tool stays active once a node is drawn.
   */
  class DrawTool : public Tool {
    public:

      /** The distance in logical units a press moves before it draws. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      void press(ToolContext& context, const PointerEvent& event) override;
      void move(ToolContext& context, const PointerEvent& event) override;
      void release(ToolContext& context, const PointerEvent& event) override;
      bool key_press(ToolContext& context, const KeyEvent& event) override;
      void cancel(ToolContext& context) override;
      bool is_busy() const override;
      ToolCursor get_cursor() const override;
      void draw(
        const ToolContext& context, CanvasOverlay& overlay) const override;

    protected:

      /**
       * Constructs a DrawTool.
       * @param prefix The prefix of the names of the nodes drawn, such as
       *        "Rectangle ".
       */
      explicit DrawTool(std::string prefix);

      /**
       * Makes the command inserting or reshaping the node drawn.
       * @param context The context worked in.
       * @param header The node's header, holding its id and name.
       * @param start The point in document space the drag started at.
       * @param end The point in document space the drag reached.
       * @param modifiers The modifier keys held.
       * @return The command, or <code>nullptr</code> if the drag spans
       *         nothing.
       */
      virtual std::unique_ptr<Command> make_command(ToolContext& context,
        NodeHeader header, Point start, Point end,
        Qt::KeyboardModifiers modifiers) = 0;

      /**
       * Handles a press released without dragging.
       * @param context The context worked in.
       * @param point The point in document space.
       */
      virtual void click(ToolContext& context, Point point);

    private:
      bool m_is_pressed;
      bool m_is_applied;
      std::string m_prefix;
      Point m_press;
      Point m_start;
      std::optional<NodeHeader> m_header;

      void update(ToolContext& context, const PointerEvent& event);
      void finish(ToolContext& context);
  };

  /**
   * Returns the box Adobe XD draws from a drag: from the start to the end in
   * whole document units, made square while Shift is held and centered on
   * the start while Alt is held.
   * @param start The point in document space the drag started at.
   * @param end The point in document space the drag reached.
   * @param modifiers The modifier keys held.
   */
  Rect make_drawn_box(Point start, Point end, Qt::KeyboardModifiers modifiers);

  /**
   * Finds the artboard a node drawn from a point belongs to.
   * @param document The document drawn in.
   * @param point The point in document space.
   * @return The topmost visible artboard whose frame holds the
   *         <i>point</i>, or nil for the pasteboard.
   */
  NodeId find_drawing_parent(const Document& document, Point point);
}

#endif

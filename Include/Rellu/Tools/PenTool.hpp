#ifndef RELLU_TOOLS_PEN_TOOL_HPP
#define RELLU_TOOLS_PEN_TOOL_HPP
#include <cstddef>
#include <optional>
#include <string>
#include "Rellu/Geometry/Contour.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Tools/PathPoints.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /**
   * Draws paths as Adobe XD's pen tool does, in the artboard under the first
   * point or on the pasteboard, named Path followed by the next number free
   * and styled by make_default_path_style. A click adds a corner point and a
   * drag adds a smooth point with symmetric handles, of which only the
   * outgoing one follows the pointer while Alt is held; Shift constrains
   * points from the previous point as constrain_drag does and turns
   * handles about their point, keeping their length, to the nearest
   * multiple of fifteen degrees, or to the direction opposite the point's
   * other handle within six degrees of it. Clicking the first point closes
   * the path and Enter or Escape finishes an open one, after which Escape
   * returns to the select tool. A double click on a point toggles it
   * between corner and smooth. Points placed snap through the snap
   * service's snap_line, as the end of a line from the previous point that
   * keeps its angle while Shift is held, and a path started while a group is
   * entered goes in that group, as find_drawing_parent finds. With a single
   * path selected, clicking one of
   * its open ends continues it and clicking a segment adds a point. Every
   * point is one undoable step, labelled as Adobe XD labels it: Draw Path,
   * Add Anchor Point, Close Path or Convert Point Type, continuing a path
   * is one labelled Extend Path, and abandoning the tool keeps the points
   * placed.
   */
  class PenTool : public Tool {
    public:

      /** The distance in logical units within which a point is hit. */
      static constexpr auto ANCHOR_REACH = 6.0;

      /** The distance in logical units within which a segment is hit. */
      static constexpr auto SEGMENT_REACH = 4.0;

      /** The distance in logical units a press moves before it drags. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      /** Constructs a PenTool. */
      PenTool() noexcept;

      /**
       * Returns the path being drawn, or nil if none is.
       * @param context The context worked in.
       */
      NodeId get_path(const ToolContext& context) const;

      void press(ToolContext& context, const PointerEvent& event) override;
      void move(ToolContext& context, const PointerEvent& event) override;
      void release(ToolContext& context, const PointerEvent& event) override;
      void double_click(
        ToolContext& context, const PointerEvent& event) override;
      void leave(ToolContext& context) override;
      bool key_press(ToolContext& context, const KeyEvent& event) override;
      void cancel(ToolContext& context) override;
      bool is_busy() const override;
      ToolCursor get_cursor() const override;
      void draw(
        const ToolContext& context, CanvasOverlay& overlay) const override;

    private:
      bool m_is_drawing;
      NodeId m_node;
      std::size_t m_contour;
      bool m_is_reversed;
      std::optional<Anchor> m_tail;
      bool m_is_dragging;
      bool m_is_dragged;
      bool m_is_closing;
      std::size_t m_anchor;
      Point m_press;
      std::optional<Point> m_hover;
      Qt::KeyboardModifiers m_modifiers;

      std::optional<EditablePath> load_drawing(
        const ToolContext& context) const;
      std::optional<EditablePath> load_selected(
        const ToolContext& context) const;
      void apply(ToolContext& context, const EditablePath& path,
        const std::string& label);
      void start(ToolContext& context, Point point);
      void drag(ToolContext& context, const PointerEvent& event);
      void finish();
  };
}

#endif

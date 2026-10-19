#ifndef RELLU_TOOLS_SHAPE_TOOL_HPP
#define RELLU_TOOLS_SHAPE_TOOL_HPP
#include <memory>
#include "Rellu/Tools/DrawTool.hpp"

namespace Rellu {

  /** Lists the kinds of shape the ShapeTool draws. */
  enum class ShapeKind : unsigned char {

    /** A rectangle, named Rectangle. */
    RECTANGLE,

    /** An ellipse, named Ellipse. */
    ELLIPSE,

    /** A polygon of three corners, named Polygon. */
    POLYGON,

    /** A straight line, named Line. */
    LINE
  };

  /**
   * Draws shapes as Adobe XD's rectangle, ellipse, polygon and line tools
   * do, in the artboard under the start of the drag or on the pasteboard,
   * styled as XD styles them and named after their kind with the next
   * number free, such as Rectangle 1. Shift draws a square, circle or
   * regular polygon, or constrains a line to multiples of 45 degrees, and
   * Alt draws from the center.
   */
  class ShapeTool : public DrawTool {
    public:

      /** The number of corners of a polygon drawn. */
      static constexpr auto POLYGON_CORNER_COUNT = 3;

      /**
       * Constructs a ShapeTool.
       * @param kind The kind of shape drawn.
       */
      explicit ShapeTool(ShapeKind kind);

      /** Returns the kind of shape drawn. */
      ShapeKind get_kind() const;

    protected:
      std::unique_ptr<Command> make_command(ToolContext& context,
        NodeHeader header, Point start, Point end,
        Qt::KeyboardModifiers modifiers) override;

    private:
      ShapeKind m_kind;
  };
}

#endif

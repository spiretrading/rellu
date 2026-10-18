#ifndef RELLU_MODEL_SHAPE_GEOMETRY_HPP
#define RELLU_MODEL_SHAPE_GEOMETRY_HPP
#include <variant>
#include <vector>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Shared.hpp"

namespace Rellu {

  /** A rectangle with optionally rounded corners. */
  struct RectangleShape {

    /** The rectangle in the node's local space. */
    Rect m_bounds = Rect(0, 0, 0, 0);

    /** The requested radius of each corner. */
    CornerRadii m_radii = CornerRadii(0, 0, 0, 0);

    bool operator ==(const RectangleShape&) const = default;
  };

  /** An ellipse, which is a circle when both radii are equal. */
  struct EllipseShape {

    /** The center in the node's local space. */
    Point m_center = Point(0, 0);

    /** The horizontal radius. */
    double m_x_radius = 0;

    /** The vertical radius. */
    double m_y_radius = 0;

    bool operator ==(const EllipseShape& ellipse) const;
  };

  /** A straight line segment. */
  struct LineShape {

    /** The start of the line in the node's local space. */
    Point m_start = Point(0, 0);

    /** The end of the line in the node's local space. */
    Point m_end = Point(0, 0);

    bool operator ==(const LineShape&) const = default;
  };

  /** A polygon with optionally rounded corners. */
  struct PolygonShape {

    /** The number of corners. */
    int m_corner_count = 3;

    /** The size of the box the polygon spans, from the local origin. */
    Size m_size = Size(0, 0);

    /** The radius rounding each corner. */
    double m_corner_radius = 0;

    /**
     * The vertices in the node's local space, or empty to derive them from
     * the corner count and size.
     */
    std::vector<Point> m_vertices;

    bool operator ==(const PolygonShape& polygon) const;
  };

  /** An arbitrary outline. */
  struct PathShape {

    /** The outline in the node's local space. */
    Shared<Path> m_path;

    /** The rule deciding which regions the outline fills. */
    FillRule m_fill_rule = FillRule::NON_ZERO;

    bool operator ==(const PathShape&) const = default;
  };

  /** Lists the boolean operations combining a compound shape's operands. */
  enum class CompoundOperation : unsigned char {

    /** The region covered by any operand. */
    UNION,

    /** The first operand's region less the other operands' regions. */
    SUBTRACT,

    /** The region covered by every operand. */
    INTERSECT,

    /** The region covered by an odd number of operands. */
    EXCLUDE
  };

  /**
   * The result of a boolean operation over operand shapes, which are the
   * compound shape's child nodes.
   */
  struct CompoundShape {

    /** The operation combining the operands. */
    CompoundOperation m_operation = CompoundOperation::UNION;

    /** The result of the operation in the node's local space. */
    Shared<Path> m_path;

    bool operator ==(const CompoundShape&) const = default;
  };

  /** The geometry of a shape node. */
  using ShapeGeometry = std::variant<RectangleShape, EllipseShape, LineShape,
    PolygonShape, PathShape, CompoundShape>;

  /**
   * Converts a shape's geometry to an outline in the node's local space.
   * @param geometry The geometry to convert.
   * @return The outline, which for a CompoundShape is its stored result.
   */
  Path to_path(const ShapeGeometry& geometry);

  inline bool EllipseShape::operator ==(const EllipseShape& ellipse) const {
    return m_center == ellipse.m_center &&
      equals(m_x_radius, ellipse.m_x_radius) &&
      equals(m_y_radius, ellipse.m_y_radius);
  }

  inline bool PolygonShape::operator ==(const PolygonShape& polygon) const {
    return m_corner_count == polygon.m_corner_count &&
      m_size == polygon.m_size &&
      equals(m_corner_radius, polygon.m_corner_radius) &&
      m_vertices == polygon.m_vertices;
  }
}

#endif

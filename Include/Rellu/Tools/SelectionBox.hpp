#ifndef RELLU_TOOLS_SELECTION_BOX_HPP
#define RELLU_TOOLS_SELECTION_BOX_HPP
#include <array>
#include <optional>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** Lists the handles on the edges of a selection box, clockwise. */
  enum class Handle : unsigned char {

    /** The top left corner. */
    TOP_LEFT,

    /** The middle of the top edge. */
    TOP,

    /** The top right corner. */
    TOP_RIGHT,

    /** The middle of the right edge. */
    RIGHT,

    /** The bottom right corner. */
    BOTTOM_RIGHT,

    /** The middle of the bottom edge. */
    BOTTOM,

    /** The bottom left corner. */
    BOTTOM_LEFT,

    /** The middle of the left edge. */
    LEFT
  };

  /** Every handle, clockwise from the top left corner. */
  inline constexpr auto HANDLES = std::array<Handle, 8>({Handle::TOP_LEFT,
    Handle::TOP, Handle::TOP_RIGHT, Handle::RIGHT, Handle::BOTTOM_RIGHT,
    Handle::BOTTOM, Handle::BOTTOM_LEFT, Handle::LEFT});

  /**
   * The box framing a selection, as Adobe XD draws it: a single node's
   * bounds turned with the node, or the bounds in document space of several
   * nodes together.
   */
  struct SelectionBox {

    /** The rect in the box's own space. */
    Rect m_rect = Rect(0, 0, 0, 0);

    /** The transform from the box's space to document space. */
    Matrix m_transform;

    /**
     * Returns the point a handle lies at in the box's space.
     * @param handle The handle.
     */
    Point get_point(Handle handle) const;

    /** Returns the four corners in document space, clockwise. */
    std::array<Point, 4> get_corners() const;

    /** Returns the center in document space. */
    Point get_center() const;

    bool operator ==(const SelectionBox&) const = default;
  };

  /** Returns <code>true</code> iff a handle lies at a corner. */
  bool is_corner(Handle handle);

  /**
   * Returns the handle on the opposite side of the box.
   * @param handle The handle.
   */
  Handle get_opposite(Handle handle);

  /**
   * Computes the box framing nodes.
   * @param document The document holding the nodes.
   * @param nodes The nodes.
   * @param measurer The function measuring text.
   * @return The box framing the geometry of the <i>nodes</i>, or
   *         <code>std::nullopt</code> if none of them draws anything.
   */
  std::optional<SelectionBox> get_selection_box(const Document& document,
    const std::vector<NodeId>& nodes,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Computes the rect a box's rect is resized to by dragging a handle.
   * @param rect The rect before resizing.
   * @param handle The handle dragged.
   * @param point The point in the box's space the handle is dragged to.
   * @param is_proportional Whether the aspect ratio is kept.
   * @param is_centered Whether the rect is resized about its center.
   * @param minimum The smallest width and height resized to, unless the
   *        <i>rect</i> is already smaller along that axis.
   * @return The resized rect.
   */
  Rect resize(const Rect& rect, Handle handle, Point point,
    bool is_proportional, bool is_centered, double minimum);
}

#endif

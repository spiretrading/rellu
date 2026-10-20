#ifndef RELLU_TOOLS_SELECTION_BOX_HPP
#define RELLU_TOOLS_SELECTION_BOX_HPP
#include <array>
#include <optional>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
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

  /** Lists the kinds of handles Adobe XD shows on a selection box. */
  enum class HandleStyle : unsigned char {

    /** Round resize handles and round rotate handles. */
    BOX,

    /** A scroll group's pill shaped viewport handles. */
    SCROLL_GROUP,

    /** A repeat grid's rotate handles beside its own handles. */
    REPEAT_GRID
  };

  /** Lists what dragging a handle of a selection box does. */
  enum class HandleAction : unsigned char {

    /** Resizes the box. */
    RESIZE,

    /** Rotates the box. */
    ROTATE
  };

  /** A handle of a selection box found under a point. */
  struct HandleHit {

    /** What dragging the handle does. */
    HandleAction m_action;

    /** The handle. */
    Handle m_handle;

    bool operator ==(const HandleHit&) const = default;
  };

  /** The handles Adobe XD shows on a selection box. */
  struct HandleLayout {

    /** The radius in logical units of a resize handle. */
    static constexpr auto HANDLE_RADIUS = 4.0;

    /** The distance in logical units within which a resize handle is hit. */
    static constexpr auto HANDLE_REACH = 6.0;

    /** The radius in logical units of a rotate handle. */
    static constexpr auto ROTATION_RADIUS = 7.0;

    /**
     * The distance in logical units outside the box's sides of the center
     * of a rotate handle.
     */
    static constexpr auto ROTATION_OFFSET = 2.5;

    /** The kind of handles shown. */
    HandleStyle m_style = HandleStyle::BOX;

    /** Whether each handle, in the order of HANDLES, is shown to resize. */
    std::array<bool, 8> m_resizers = {};

    /**
     * Whether each handle, in the order of HANDLES, has a rotate handle
     * beside it, lying under any resize handle.
     */
    std::array<bool, 8> m_rotators = {};

    /**
     * Returns <code>true</code> iff a handle is shown to resize.
     * @param handle The handle.
     */
    bool is_resizer(Handle handle) const;

    /**
     * Returns <code>true</code> iff a handle has a rotate handle.
     * @param handle The handle.
     */
    bool is_rotator(Handle handle) const;

    /**
     * Finds the handle under a point, the topmost as Adobe XD stacks them.
     * @param size The width and height of the box in logical units.
     * @param zoom The number of logical units per document unit.
     * @param point The point in logical units, measured from the box's top
     *        left corner along its top and left sides.
     */
    std::optional<HandleHit> find(Size size, double zoom, Point point) const;

    bool operator ==(const HandleLayout&) const = default;
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
   * Returns the handles Adobe XD shows on the box of a selection.
   * @param document The document holding the nodes.
   * @param nodes The nodes selected that are not locked, in the order
   *        selected.
   * @param size The width and height of the box in logical units.
   * @param zoom The number of logical units per document unit.
   * @param dragged The scroll group handle dragged last, shown over the
   *        handles it overlaps.
   */
  HandleLayout get_handle_layout(const Document& document,
    const std::vector<NodeId>& nodes, Size size, double zoom,
    std::optional<Handle> dragged = std::nullopt);

  /**
   * Returns the rect of a scroll group's viewport handle.
   * @param handle The handle, at the middle of a side.
   * @param size The width and height of the box in logical units.
   * @param zoom The number of logical units per document unit.
   * @return The rect in logical units, measured from the box's top left
   *         corner along its top and left sides.
   */
  Rect get_scroll_handle_rect(Handle handle, Size size, double zoom);

  /**
   * Returns the handles Adobe XD shows on the box of a locked selection.
   * @param size The width and height of the box in logical units.
   */
  HandleLayout get_locked_handle_layout(Size size);

  /**
   * Computes the rect a box's rect is resized to by dragging a handle.
   * @param rect The rect before resizing.
   * @param handle The handle dragged.
   * @param point The point in the box's space the handle is dragged to.
   * @param is_proportional Whether the aspect ratio is kept.
   * @param is_centered Whether the rect is resized about its center.
   * @param is_flippable Whether dragging past the opposite side flips the
   *        rect rather than stopping at the smallest size.
   * @param minimum The smallest width and height resized to, unless the
   *        <i>rect</i> is already smaller along that axis.
   * @return The resized rect, whose width or height is negative where it is
   *         flipped, so that make_rect_mapping from the <i>rect</i> to it
   *         is the mapping resizing it.
   */
  Rect resize(const Rect& rect, Handle handle, Point point,
    bool is_proportional, bool is_centered, bool is_flippable,
    double minimum);
}

#endif

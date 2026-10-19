#ifndef RELLU_TOOLS_PATH_POINTS_HPP
#define RELLU_TOOLS_PATH_POINTS_HPP
#include <compare>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Geometry/Contour.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"
#include "Rellu/Tools/ToolContext.hpp"

namespace Rellu {

  /** Identifies an anchor of a path by its contour and its place in it. */
  struct AnchorIndex {

    /** The index of the contour. */
    std::size_t m_contour;

    /** The index of the anchor in the contour. */
    std::size_t m_anchor;

    auto operator <=>(const AnchorIndex&) const = default;
  };

  /** Lists the handles of an anchor. */
  enum class HandleSide : unsigned char {

    /** The handle of the segment arriving at the anchor. */
    IN,

    /** The handle of the segment leaving the anchor. */
    OUT
  };

  /** Identifies a handle of an anchor. */
  struct HandleIndex {

    /** The anchor. */
    AnchorIndex m_anchor;

    /** The side of the anchor the handle lies on. */
    HandleSide m_side;

    bool operator ==(const HandleIndex&) const = default;
  };

  /** A point on a segment of a path. */
  struct SegmentPosition {

    /** The index of the contour. */
    std::size_t m_contour;

    /** The position on the contour. */
    ContourPosition m_position;
  };

  /** The points of a shape node as its outline is edited. */
  struct EditablePath {

    /** The node. */
    NodeId m_node;

    /** The contours in the node's local space. */
    std::vector<Contour> m_contours;

    /** The fill rule of the outline. */
    FillRule m_fill_rule = FillRule::NON_ZERO;

    /** The transform from the node's local space to document space. */
    Matrix m_transform;
  };

  /**
   * Tests whether the points of a node can be edited: a visible, unlocked
   * shape other than a compound shape.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_point_editable(const Document& document, const NodeId& id);

  /**
   * Loads the points of a node.
   * @param document The document holding the node.
   * @param id The node's id.
   * @return The points, or <code>std::nullopt</code> if the node's points
   *         can not be edited.
   */
  std::optional<EditablePath> load_path(
    const Document& document, const NodeId& id);

  /**
   * Makes the command giving a node the outline of edited points.
   * @param label The label naming the operation.
   * @param path The edited points.
   */
  std::unique_ptr<SetPathCommand> make_path_command(
    std::string label, const EditablePath& path);

  /**
   * Returns the anchor at an index in document space.
   * @param path The points.
   * @param index The anchor's index.
   */
  Anchor get_anchor(const EditablePath& path, const AnchorIndex& index);

  /**
   * Finds the anchor nearest a point.
   * @param context The context worked in.
   * @param path The points to search.
   * @param position The point in logical units.
   * @param reach The distance in logical units within which an anchor is
   *        found.
   * @return The nearest anchor within the <i>reach</i>, if any.
   */
  std::optional<AnchorIndex> find_anchor(const ToolContext& context,
    const EditablePath& path, Point position, double reach);

  /**
   * Finds the handle of shown anchors nearest a point.
   * @param context The context worked in.
   * @param path The points to search.
   * @param anchors The anchors whose handles are shown.
   * @param position The point in logical units.
   * @param reach The distance in logical units within which a handle is
   *        found.
   * @return The nearest handle within the <i>reach</i>, if any.
   */
  std::optional<HandleIndex> find_handle(const ToolContext& context,
    const EditablePath& path, const std::vector<AnchorIndex>& anchors,
    Point position, double reach);

  /**
   * Finds the point of a path's segments nearest a point.
   * @param context The context worked in.
   * @param path The points to search.
   * @param position The point in logical units.
   * @param reach The distance in logical units within which a segment is
   *        found.
   * @return The nearest point within the <i>reach</i>, if any.
   */
  std::optional<SegmentPosition> find_segment(const ToolContext& context,
    const EditablePath& path, Point position, double reach);

  /**
   * Constrains a displacement to the nearest multiple of 45 degrees,
   * keeping its length along that direction.
   * @param delta The displacement.
   */
  Point constrain_to_45_degrees(Point delta);

  /**
   * Draws a path's outline, its anchors and the handles of some of them.
   * @param context The context worked in.
   * @param path The points to draw.
   * @param selection The anchors drawn as selected.
   * @param handles The anchors whose handles are drawn.
   * @param overlay The overlay to draw into.
   */
  void draw_path(const ToolContext& context, const EditablePath& path,
    const std::vector<AnchorIndex>& selection,
    const std::vector<AnchorIndex>& handles, CanvasOverlay& overlay);

  /**
   * Draws a line of the color paths are drawn with.
   * @param start The start of the line in logical units.
   * @param end The end of the line in logical units.
   * @param overlay The overlay to draw into.
   */
  void draw_path_line(Point start, Point end, CanvasOverlay& overlay);

  /**
   * Draws a cubic Bezier curve of the color paths are drawn with.
   * @param anchor The anchor the curve leaves, in logical units.
   * @param end The anchor the curve arrives at, in logical units.
   * @param overlay The overlay to draw into.
   */
  void draw_path_curve(
    const Anchor& anchor, const Anchor& end, CanvasOverlay& overlay);
}

#endif

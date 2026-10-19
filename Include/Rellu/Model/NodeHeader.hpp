#ifndef RELLU_MODEL_NODE_HEADER_HPP
#define RELLU_MODEL_NODE_HEADER_HPP
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include "Rellu/Geometry/BlendMode.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Projection.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/OpaqueData.hpp"

namespace Rellu {

  /**
   * Specifies how a node responds when its parent is resized, as Adobe XD's
   * responsive resize constraints.
   */
  struct Constraints {

    /** Whether the constraints apply at all. */
    bool m_is_enabled = true;

    /** Whether the constraints were set explicitly rather than inferred. */
    bool m_is_custom = false;

    /** Whether the distance to the parent's left edge stays fixed. */
    bool m_left = false;

    /** Whether the distance to the parent's right edge stays fixed. */
    bool m_right = false;

    /** Whether the distance to the parent's top edge stays fixed. */
    bool m_top = false;

    /** Whether the distance to the parent's bottom edge stays fixed. */
    bool m_bottom = false;

    /** Whether the width stays fixed. */
    bool m_width = false;

    /** Whether the height stays fixed. */
    bool m_height = false;

    bool operator ==(const Constraints&) const = default;
  };

  /**
   * Rotates a node out of its parent's plane and draws it in perspective, as
   * Adobe XD's 3D transforms.
   */
  struct Transform3d {

    /**
     * The 4x4 matrix in row major order mapping a point (x, y, z, 1) of the
     * node to its parent's coordinates, whose entries mapping x and y onto
     * the parent's x and y match the header's transform, which supersedes
     * them.
     */
    std::array<double, 16> m_matrix = std::array<double, 16>(
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});

    /** The rotation about the x axis in degrees. */
    double m_rotation_x = 0;

    /** The rotation about the y axis in degrees. */
    double m_rotation_y = 0;

    /** The point in the node's coordinates the perspective centers on. */
    Point m_perspective_center = Point(0, 0);

    bool operator ==(const Transform3d& transform) const;
  };

  /**
   * The distance in document units between the viewer and the plane of a
   * node's parent when Adobe XD draws a 3D transform in perspective.
   */
  inline constexpr auto PERSPECTIVE_DISTANCE = 800.0;

  /**
   * Returns the projection of a node's 3D transform onto its parent's plane,
   * viewed in perspective from the node's perspective center.
   * @param transform The node's 3D transform.
   * @param planar The node's transform, mapping x and y onto the parent's
   *        plane.
   * @return The transform from the node's coordinates to its parent's.
   */
  Projection to_projection(
    const Transform3d& transform, const Matrix& planar);

  /** The properties shared by every kind of node. */
  struct NodeHeader {

    /** The node's identifier, unique within its document. */
    NodeId m_id;

    /** The name shown in the layers panel. */
    std::string m_name;

    /** Whether the node is drawn. */
    bool m_is_visible = true;

    /** Whether the node is protected from selection and editing. */
    bool m_is_locked = false;

    /** The transform from the node's local space to its parent's. */
    Matrix m_transform;

    /**
     * The transform rotating the node out of its parent's plane, if any, in
     * which case it draws through the projection of this transform rather
     * than through m_transform alone.
     */
    std::optional<Transform3d> m_transform_3d;

    /** The opacity in the range [0, 1]. */
    double m_opacity = 1;

    /** How the node blends with what lies beneath it. */
    BlendMode m_blend_mode = BlendMode::NORMAL;

    /** Whether the node's content blends in isolation from its backdrop. */
    bool m_is_isolated = false;

    /** How the node responds when its parent is resized. */
    Constraints m_constraints;

    /** Whether the node is marked for batch export. */
    bool m_is_marked_for_export = false;

    /** Whether the node keeps its place while its artboard scrolls. */
    bool m_is_fixed_position = false;

    /**
     * The size whose proportions the node keeps when resized, or none if
     * its aspect ratio is not locked.
     */
    std::optional<Size> m_aspect_lock;

    /** The data a document format keeps for the node. */
    OpaqueData m_opaque;

    bool operator ==(const NodeHeader& header) const;
  };

  inline bool Transform3d::operator ==(const Transform3d& transform) const {
    return std::ranges::equal(m_matrix, transform.m_matrix,
      [] (auto left, auto right) {
        return equals(left, right);
      }) && equals(m_rotation_x, transform.m_rotation_x) &&
      equals(m_rotation_y, transform.m_rotation_y) &&
      m_perspective_center == transform.m_perspective_center;
  }

  inline Projection to_projection(const Transform3d& transform,
      const Matrix& planar) {
    auto& matrix = transform.m_matrix;
    auto center = planar.map(transform.m_perspective_center);
    auto x = -matrix[8] / PERSPECTIVE_DISTANCE;
    auto y = -matrix[9] / PERSPECTIVE_DISTANCE;
    auto depth = -matrix[11] / PERSPECTIVE_DISTANCE;
    return Projection(planar.m_a + center.m_x * x,
      planar.m_b + center.m_y * x, planar.m_c + center.m_x * y,
      planar.m_d + center.m_y * y, planar.m_tx + center.m_x * depth,
      planar.m_ty + center.m_y * depth, x, y, 1 + depth);
  }

  inline bool NodeHeader::operator ==(const NodeHeader& header) const {
    return m_id == header.m_id && m_name == header.m_name &&
      m_is_visible == header.m_is_visible &&
      m_is_locked == header.m_is_locked &&
      m_transform == header.m_transform &&
      m_transform_3d == header.m_transform_3d &&
      equals(m_opacity, header.m_opacity) &&
      m_blend_mode == header.m_blend_mode &&
      m_is_isolated == header.m_is_isolated &&
      m_constraints == header.m_constraints &&
      m_is_marked_for_export == header.m_is_marked_for_export &&
      m_is_fixed_position == header.m_is_fixed_position &&
      m_aspect_lock == header.m_aspect_lock && m_opaque == header.m_opaque;
  }
}

#endif

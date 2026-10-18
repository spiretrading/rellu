#ifndef RELLU_MODEL_NODE_HEADER_HPP
#define RELLU_MODEL_NODE_HEADER_HPP
#include <string>
#include "Rellu/Geometry/BlendMode.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Specifies how a node responds when its parent is resized, as Adobe XD's
   * responsive resize constraints.
   */
  struct Constraints {

    /** Whether the constraints apply at all. */
    bool m_is_enabled = false;

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

    bool operator ==(const NodeHeader&) const = default;
  };
}

#endif

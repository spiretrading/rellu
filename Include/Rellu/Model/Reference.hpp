#ifndef RELLU_MODEL_REFERENCE_HPP
#define RELLU_MODEL_REFERENCE_HPP
#include <optional>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * The payload of a reference node, which renders as another node's subtree
   * with overrides applied.
   */
  struct Reference {

    /** The node rendered, inside a component's master or state tree. */
    NodeId m_source;

    /** The transform replacing the source's, if overridden. */
    std::optional<Matrix> m_transform;

    /**
     * Whether the reference's own children replace the source's children,
     * rather than the source's children being rendered.
     */
    bool m_has_children = false;

    bool operator ==(const Reference&) const = default;
  };
}

#endif

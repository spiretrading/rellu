#ifndef RELLU_MODEL_REFERENCE_RESOLUTION_HPP
#define RELLU_MODEL_REFERENCE_RESOLUTION_HPP
#include <optional>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/NodeStore.hpp"

namespace Rellu {

  /**
   * What a reference node renders as. A reference stands for its source
   * unchanged except for the overrides it records, so apart from its id its
   * header plays no part in rendering.
   */
  struct ResolvedReference {

    /**
     * The node, not itself a reference, whose header and payload are
     * rendered in place of the reference.
     */
    NodeId m_source;

    /** The transform rendered with, relative to the reference's parent. */
    Matrix m_transform;

    /** The node whose children are rendered as the reference's children. */
    NodeId m_child_source;

    bool operator ==(const ResolvedReference&) const = default;
  };

  /**
   * Resolves a reference by following its chain of sources to a node that
   * is not a reference. The nearest reference along the chain that
   * overrides the transform, or that has its own children, decides that
   * part; otherwise the final source does.
   * @param nodes The nodes to resolve within.
   * @param reference The id of the reference node.
   * @return The resolution, or <code>std::nullopt</code> if the node is not
   *         a reference or the chain is broken or cyclic.
   */
  std::optional<ResolvedReference> resolve_reference(
    const NodeStore& nodes, const NodeId& reference);
}

#endif

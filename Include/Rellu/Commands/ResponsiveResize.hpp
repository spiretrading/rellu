#ifndef RELLU_COMMANDS_RESPONSIVE_RESIZE_HPP
#define RELLU_COMMANDS_RESPONSIVE_RESIZE_HPP
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu {

  /** Lists the ways the contents of containers follow a resize. */
  enum class ResizeRule : unsigned char {

    /** The contents scale along with their containers. */
    PROPORTIONAL,

    /**
     * The constraints of the contents are recorded and the contents are laid
     * out by them, as Adobe XD's responsive resize does.
     */
    RESPONSIVE,

    /**
     * The constraints of the contents are recorded as RESPONSIVE does, but
     * the contents of groups scale and those of artboards stay, as Adobe XD
     * resizes with Shift held.
     */
    SUPPRESSED
  };

  /**
   * Returns the constraints a node is laid out by when its parent is
   * resized: its own if they are custom, otherwise those Adobe XD infers
   * from where it lies within its parent, along with the fixed sizes its
   * kind requires.
   * @param document The document holding the node.
   * @param id The node's id.
   * @param measurer The function measuring text.
   */
  Constraints infer_constraints(const Document& document, const NodeId& id,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Records the constraints of a node's descendants as Adobe XD does before
   * resizing the node.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param measurer The function measuring text.
   */
  void record_constraints(const Document& document, Transaction& transaction,
    const NodeId& id, const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Resizes nodes by a mapping in document space as Adobe XD does. A single
   * node is resized along its own axes; several are resized as the children
   * of a group spanning them. Mirroring mappings mirror the contents of
   * plain and mask groups and the pins of their custom constraints, but
   * only reposition the contents of components.
   * @param document The document holding the nodes.
   * @param transaction The open transaction on the <i>document</i>.
   * @param nodes The nodes to resize, none an ancestor of another.
   * @param mapping The mapping in document space.
   * @param rule The way the contents of containers follow.
   * @param is_pixel_aligned Whether the contents of containers that lay on
   *        whole pixels are kept on whole pixels, as Adobe XD does while
   *        handles are dragged.
   * @param measurer The function measuring text.
   */
  void resize_nodes(const Document& document, Transaction& transaction,
    const std::vector<NodeId>& nodes, const Matrix& mapping, ResizeRule rule,
    bool is_pixel_aligned,
    const TextMeasurer& measurer = estimate_text_bounds);
}

#endif

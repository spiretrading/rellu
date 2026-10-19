#ifndef RELLU_COMMANDS_HIERARCHY_HPP
#define RELLU_COMMANDS_HIERARCHY_HPP
#include <cstddef>
#include <vector>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Tests whether one node lies strictly within another's subtree.
   * @param document The document holding the nodes.
   * @param ancestor The possible ancestor.
   * @param node The possible descendant.
   * @return <code>true</code> iff the <i>ancestor</i> is a proper ancestor of
   *         the <i>node</i>.
   */
  bool is_ancestor(
    const Document& document, const NodeId& ancestor, const NodeId& node);

  /**
   * Finds the artboard a node belongs to.
   * @param document The document holding the node.
   * @param id The node's id.
   * @return The node itself if it is an artboard, otherwise its nearest
   *         artboard ancestor, or nil if there is none.
   */
  NodeId find_artboard(const Document& document, const NodeId& id);

  /**
   * Tests whether a node lies within a compound shape, which Adobe XD never
   * lets the user hide.
   * @param document The document holding the node.
   * @param id The node's id.
   * @return <code>true</code> iff an ancestor of the node is a compound
   *         shape.
   */
  bool is_inside_compound(const Document& document, const NodeId& id);

  /**
   * Returns the position of a node among its parent's children, or among the
   * canvas nodes for a top-level node.
   * @param document The document holding the node.
   * @param id The node's id.
   * @return The position, or the number of canvas nodes for the root of a
   *         component tree.
   */
  std::size_t get_index(const Document& document, const NodeId& id);

  /**
   * Reduces a list of nodes to those an operation on the whole list applies
   * to directly.
   * @param document The document holding the nodes.
   * @param nodes The nodes.
   * @return The <i>nodes</i> in their given order, without unknown ids,
   *         repeats and nodes whose ancestor is listed.
   */
  std::vector<NodeId> get_top_level(
    const Document& document, const std::vector<NodeId>& nodes);

  /**
   * Sorts nodes into the order they are painted in, the bottom-most first,
   * where a node precedes its descendants and the canvas precedes component
   * trees.
   * @param document The document holding the nodes.
   * @param nodes The existing nodes to sort.
   */
  void sort_by_z_order(const Document& document, std::vector<NodeId>& nodes);
}

#endif

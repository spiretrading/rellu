#ifndef RELLU_UI_NODE_DESCRIPTION_HPP
#define RELLU_UI_NODE_DESCRIPTION_HPP
#include <QString>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Node.hpp"

namespace Rellu {

  /**
   * Returns the node a node is displayed as: the final source of a
   * reference, otherwise the node itself.
   * @param document The document containing the node.
   * @param node The node.
   * @return The node displayed, or <code>nullptr</code> if <i>node</i> is a
   *         reference that does not resolve.
   */
  const Node* get_displayed_node(const Document& document, const Node& node);

  /**
   * Returns the name a node is listed under: its own name, else the name of
   * the node it is displayed as, else a description of its kind or content.
   * @param document The document containing the node.
   * @param node The node.
   */
  QString get_display_name(const Document& document, const Node& node);

  /**
   * Returns the name of a kind of node, such as <code>Artboard</code>.
   * @param kind The kind of node.
   */
  QString get_kind_name(NodeKind kind);

  /**
   * Returns the name of the kind of a node's geometry or role, such as
   * <code>Rectangle</code> or <code>Repeat Grid</code>, falling back to the
   * name of the node's kind.
   * @param node The node.
   */
  QString get_type_name(const Node& node);
}

#endif

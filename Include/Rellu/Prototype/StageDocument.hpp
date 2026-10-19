#ifndef RELLU_PROTOTYPE_STAGE_DOCUMENT_HPP
#define RELLU_PROTOTYPE_STAGE_DOCUMENT_HPP
#include <memory>
#include <unordered_set>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Copies a document with the same ids: every node, the resources and the
   * prototype, without its history.
   * @param document The document to copy.
   * @return The copy.
   */
  std::unique_ptr<Document> copy_document(const Document& document);

  /**
   * Finds the nodes that Adobe XD keeps in place when their artboard
   * scrolls, which an Adobe XD file marks with Fix Position When Scrolling.
   * @param document The document, as read from an Adobe XD file.
   * @return The ids of the nodes marked.
   */
  std::unordered_set<NodeId> find_fixed_nodes(const Document& document);

  /**
   * Returns <code>true</code> iff an Adobe XD file marks a node with Fix
   * Position When Scrolling.
   * @param document The document, as read from an Adobe XD file.
   * @param id The node's id.
   */
  bool is_fixed(const Document& document, const NodeId& id);
}

#endif

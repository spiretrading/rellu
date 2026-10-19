#ifndef RELLU_COMMANDS_COPY_NAMES_HPP
#define RELLU_COMMANDS_COPY_NAMES_HPP
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu {

  /**
   * Names a copy of a node and its descendants as Adobe XD names what it
   * duplicates or pastes.
   * @param document The document holding the copy.
   * @param transaction The transaction the copy was inserted by.
   * @param id The copy's id.
   * @param is_kept Whether every name is kept except those of component
   *        instances and unnamed nodes, as when pasting into another
   *        artboard.
   */
  void name_copy(const Document& document, Transaction& transaction,
    const NodeId& id, bool is_kept);
}

#endif

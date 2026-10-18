#ifndef RELLU_COMMANDS_COMMAND_HPP
#define RELLU_COMMANDS_COMMAND_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu {

  /**
   * A named, user-level operation on a document, applied through a single
   * transaction so that it forms one undoable step. A command may be applied
   * again after the changes of its previous application were undone, so its
   * effect must depend only on its parameters and the document it applies
   * to.
   */
  class Command {
    public:
      virtual ~Command() = default;

      /** Returns the label naming the operation, such as Move. */
      virtual std::string get_label() const = 0;

      /**
       * Applies the operation, throwing std::invalid_argument if it does not
       * apply to the document.
       * @param document The document to change.
       * @param transaction The open transaction on the <i>document</i>
       *        through which every change is made.
       */
      virtual void apply(
        const Document& document, Transaction& transaction) = 0;

      /**
       * Combines a command that follows this one into this one, so that
       * applying this command has the effect of applying both in turn.
       * @param command The command following this one.
       * @return <code>true</code> iff the <i>command</i> was combined.
       */
      virtual bool merge(const Command& command);

      /**
       * Returns the nodes to select once the command has applied, or
       * <code>std::nullopt</code> to keep the selection.
       */
      virtual std::optional<std::vector<NodeId>> get_selection() const;
  };
}

#endif

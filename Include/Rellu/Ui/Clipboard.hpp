#ifndef RELLU_UI_CLIPBOARD_HPP
#define RELLU_UI_CLIPBOARD_HPP
#include <memory>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Holds copies of node subtrees to paste back into the document they were
   * copied from, along with the state trees of the component instances among
   * them and the interactions they own.
   */
  class Clipboard {
    public:

      /** Constructs an empty Clipboard. */
      Clipboard();

      ~Clipboard();

      /** Returns <code>true</code> iff nothing is held. */
      bool is_empty() const;

      /**
       * Tests whether copying nodes would copy anything: those on the canvas
       * or in a container that are not references.
       * @param document The document holding the nodes.
       * @param nodes The nodes.
       */
      static bool is_copyable(
        const Document& document, const std::vector<NodeId>& nodes);

      /**
       * Replaces what is held with copies of nodes, keeping the clipboard
       * unchanged if none of them can be copied.
       * @param document The document holding the nodes.
       * @param nodes The nodes to copy.
       * @return <code>true</code> iff anything was copied.
       */
      bool copy(const Document& document, const std::vector<NodeId>& nodes);

      /** Discards what is held. */
      void clear();

      /**
       * Makes a command labelled Paste inserting what is held with fresh ids,
       * every id referring within the copied set replaced by its copy's. Each
       * copy is placed at the top of the container its original was copied
       * from, or on the canvas if the container no longer exists, where it
       * appears just as its original did.
       * @return The command, or <code>nullptr</code> if nothing is held.
       */
      std::unique_ptr<Command> make_paste_command() const;

    private:
      struct Content;
      class PasteCommand;
      std::shared_ptr<const Content> m_content;

      Clipboard(const Clipboard&) = delete;
      Clipboard& operator =(const Clipboard&) = delete;
  };
}

#endif

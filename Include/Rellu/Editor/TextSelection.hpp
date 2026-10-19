#ifndef RELLU_EDITOR_TEXT_SELECTION_HPP
#define RELLU_EDITOR_TEXT_SELECTION_HPP
#include <cstddef>
#include <functional>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * The text node of a document being edited in place, if any, with its
   * caret and the anchor of the range selected from it, as UTF-16 indices.
   * Editing ends when the node is removed, and the indices are kept within
   * its content as it changes.
   */
  class TextSelection {
    public:

      /**
       * Constructs a TextSelection editing no text.
       * @param document The document whose text is edited.
       */
      explicit TextSelection(Document& document);

      /** Returns <code>true</code> iff a text node is being edited. */
      bool is_editing() const;

      /** Returns the text node being edited, or nil. */
      const NodeId& get_node() const;

      /** Returns the index the selection extends from. */
      std::size_t get_anchor() const;

      /** Returns the index of the caret. */
      std::size_t get_caret() const;

      /** Returns the range selected, from its lower to its upper index. */
      TextRange get_range() const;

      /**
       * Edits a text node, selecting the range between two indices, clamped
       * to its content.
       * @param node The text node, or nil to end editing.
       * @param anchor The index the selection extends from.
       * @param caret The index of the caret.
       */
      void set(const NodeId& node, std::size_t anchor, std::size_t caret);

      /** Ends editing. */
      void clear();

      /**
       * Subscribes an observer to every change of the node, anchor or caret.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

    private:
      Document* m_document;
      NodeId m_node;
      std::size_t m_anchor;
      std::size_t m_caret;
      Signal m_signal;
      Subscription m_subscription;

      TextSelection(const TextSelection&) = delete;
      TextSelection& operator =(const TextSelection&) = delete;
      void on_change(const ChangeSet& changes);
  };
}

#endif

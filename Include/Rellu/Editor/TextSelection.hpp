#ifndef RELLU_EDITOR_TEXT_SELECTION_HPP
#define RELLU_EDITOR_TEXT_SELECTION_HPP
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * The text node of a document being edited in place, if any, with its
   * caret and the anchor of the range selected from it, as UTF-16 indices,
   * the style and hyperlink set for the text typed at the caret, and the
   * styles of the text edited or styled most recently, which new text
   * takes. Editing ends when the node is removed, and the indices are kept
   * within its content as it changes.
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
       * Returns the style set for the text typed at the caret, or none to
       * type in the style of the text before it.
       */
      const std::optional<TextStyle>& get_pending_style() const;

      /**
       * Sets the style of the text typed at the caret until the node, anchor
       * or caret changes.
       * @param style The style, or none to type in the style of the text
       *        before the caret.
       */
      void set_pending_style(std::optional<TextStyle> style);

      /**
       * Returns the URL set for the text typed at the caret to link to, or
       * none to link as the text before it does.
       */
      const std::optional<std::string>& get_pending_hyperlink() const;

      /**
       * Sets the URL the text typed at the caret links to until the node,
       * anchor or caret changes.
       * @param hyperlink The URL, empty to link nowhere, or none to link as
       *        the text before the caret does.
       */
      void set_pending_hyperlink(std::optional<std::string> hyperlink);

      /**
       * Returns the character style of the text edited or styled most
       * recently, whose font, character spacing and color new text takes,
       * Adobe XD's default before any.
       */
      const TextStyle& get_last_style() const;

      /**
       * Returns the paragraph style of the text edited or styled most
       * recently, which new text takes.
       */
      const ParagraphStyle& get_last_paragraph_style() const;

      /**
       * Records the styles of the text edited or styled most recently.
       * @param style The character style.
       * @param paragraph The paragraph style.
       */
      void set_last_style(TextStyle style, ParagraphStyle paragraph);

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
       * Subscribes an observer to every change of the node, anchor, caret,
       * pending style or pending hyperlink.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

    private:
      Document* m_document;
      NodeId m_node;
      std::size_t m_anchor;
      std::size_t m_caret;
      std::optional<TextStyle> m_pending_style;
      std::optional<std::string> m_pending_hyperlink;
      TextStyle m_last_style;
      ParagraphStyle m_last_paragraph_style;
      Signal m_signal;
      Subscription m_subscription;

      TextSelection(const TextSelection&) = delete;
      TextSelection& operator =(const TextSelection&) = delete;
      void on_change(const ChangeSet& changes);
  };
}

#endif

#ifndef RELLU_COMMANDS_HISTORY_HPP
#define RELLU_COMMANDS_HISTORY_HPP
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * The labelled undo and redo history of a document. Each step is one
   * command, or one transaction committed on the document by other means.
   * The most recent step stays open to merging until it is sealed, undone
   * or followed by another step. Once a History is attached, the document
   * must be undone, redone and cleared only through it.
   */
  class History {
    public:

      /**
       * Constructs a History of the changes to a document from now on, the
       * document's current state being clean.
       * @param document The document to track.
       */
      explicit History(Document& document);

      /** Returns the document tracked. */
      Document& get_document() const;

      /**
       * Applies a command as a new step, propagating any exception it throws
       * after reverting its changes.
       * @param command The command to apply.
       * @return The command recorded, or <code>nullptr</code> if it changed
       *         nothing.
       */
      const Command* execute(std::unique_ptr<Command> command);

      /**
       * Applies a command, merging it into the most recent step when that
       * step is open and its command combines with it, and otherwise as a
       * new step. A merged step that changes nothing is dropped.
       * @param command The command to apply.
       * @return The command recorded, or <code>nullptr</code> if no step
       *         records the change.
       */
      const Command* merge(std::unique_ptr<Command> command);

      /** Closes the most recent step to merging. */
      void seal();

      /** Returns <code>true</code> iff there is a step to undo. */
      bool can_undo() const;

      /** Returns <code>true</code> iff there is an undone step to redo. */
      bool can_redo() const;

      /** Returns the number of steps that can be undone. */
      std::size_t get_undo_count() const;

      /** Returns the number of steps that can be redone. */
      std::size_t get_redo_count() const;

      /** Returns the label of the step to undo, or empty if unlabelled. */
      std::string get_undo_label() const;

      /** Returns the label of the step to redo, or empty if unlabelled. */
      std::string get_redo_label() const;

      /**
       * Returns the data attached to the step to undo, or
       * <code>nullptr</code> if there is no such step.
       */
      std::any* get_undo_annotation();

      /**
       * Returns the data attached to the step to redo, or
       * <code>nullptr</code> if there is no such step.
       */
      std::any* get_redo_annotation();

      /**
       * Reverts the most recent step.
       * @return <code>true</code> iff there was a step to undo.
       */
      bool undo();

      /**
       * Reapplies the most recently undone step.
       * @return <code>true</code> iff there was a step to redo.
       */
      bool redo();

      /**
       * Returns <code>true</code> iff the document differs from its state
       * when last marked clean.
       */
      bool is_dirty() const;

      /** Marks the document's current state as clean, as when saved. */
      void mark_clean();

      /** Discards every step, along with the document's own history. */
      void clear();

      /**
       * Subscribes an observer to every change of the steps, labels or clean
       * state.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

    private:
      struct Step {
        std::string m_label;
        std::unique_ptr<Command> m_command;
        std::any m_annotation;
      };
      Document* m_document;
      std::vector<Step> m_undo;
      std::vector<Step> m_redo;
      std::optional<std::size_t> m_clean;
      std::size_t m_base;
      bool m_is_open;
      bool m_is_applying;
      Signal m_signal;
      Subscription m_subscription;

      History(const History&) = delete;
      History& operator =(const History&) = delete;
      void push(Step step);
      void synchronize();
      void on_change();
  };
}

#endif

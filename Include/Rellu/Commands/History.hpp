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
#include "Rellu/Commands/Compounds.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * The labelled undo and redo history of a document. Each step is one
   * command, or one transaction committed on the document by other means.
   * A command's step also holds the changes it causes in components, as
   * refresh_components makes them, the recomputed results of the compound
   * shapes whose operands it changed, as refresh_compounds makes them, and
   * the recomputed layouts it called for, as refresh_layouts makes them. The
   * most recent step stays open to merging until it is sealed, undone or
   * followed by another step. Once a History is attached, the document must
   * be undone, redone and cleared only through it.
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
       * Sets the test of the compound shapes whose results commands'
       * steps recompute, by default accepting all of them.
       * @param filter The test, or empty to accept every compound shape.
       */
      void set_compound_filter(CompoundFilter filter);

      /**
       * Sets the function measuring text when commands' steps recompute
       * layouts, by default estimate_text_bounds.
       * @param measurer The function measuring text.
       */
      void set_text_measurer(TextMeasurer measurer);

      /**
       * Keeps stack items where they are while they are dragged, the other
       * items of their stacks making room for them, until released.
       * @param nodes The items dragged.
       */
      void hold_layout(std::vector<NodeId> nodes);

      /**
       * Releases the items held, placing them in their stacks as part of the
       * most recent step if that step was made while they were held.
       */
      void release_layout();

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
      bool m_is_recording;
      CompoundFilter m_filter;
      LayoutOptions m_layout;
      std::size_t m_hold;
      ChangeSet m_changes;
      Signal m_signal;
      Subscription m_subscription;

      History(const History&) = delete;
      History& operator =(const History&) = delete;
      void push(Step step);
      void synchronize();
      void refresh();
      void on_change(const ChangeSet& changes);
  };
}

#endif

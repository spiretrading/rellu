#ifndef RELLU_EDITOR_EDITOR_HPP
#define RELLU_EDITOR_EDITOR_HPP
#include <memory>
#include <string_view>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Editor/EditContext.hpp"
#include "Rellu/Editor/Selection.hpp"
#include "Rellu/Editor/TextSelection.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Subscription.hpp"

namespace Rellu {

  /**
   * The editing state of a document: its history, selection and context.
   * Commands executed through the Editor select what they produce, and
   * undoing or redoing them restores the selection from before or after.
   * A command editing selected nodes inside component instances first makes
   * them editable, as make_instance_edit prepares it.
   * Changing the selection seals the most recent step to merging.
   */
  class Editor {
    public:

      /**
       * Constructs an Editor with an empty selection at the top level.
       * @param document The document to edit.
       */
      explicit Editor(Document& document);

      /** Returns the document edited. */
      Document& get_document() const;

      /** Returns the history. */
      auto& get_history(this auto& self);

      /** Returns the selection. */
      auto& get_selection(this auto& self);

      /** Returns the edit context. */
      auto& get_context(this auto& self);

      /** Returns the text being edited in place. */
      auto& get_text_selection(this auto& self);

      /**
       * Applies a command as a new step, then selects what it produces.
       * @param command The command to apply.
       * @return The command recorded, or <code>nullptr</code> if it changed
       *         nothing.
       */
      const Command* execute(std::unique_ptr<Command> command);

      /**
       * Applies a command, merging it into the most recent step where
       * possible, then selects what it produces.
       * @param command The command to apply.
       * @return The command recorded, or <code>nullptr</code> if no step
       *         records the change.
       */
      const Command* merge(std::unique_ptr<Command> command);

      /**
       * Applies a named command to the selection as a new step.
       * @param registry The registry naming the command.
       * @param name The command's name.
       * @return <code>true</code> iff the command applied to the selection
       *         and changed the document.
       */
      bool execute(const CommandRegistry& registry, std::string_view name);

      /**
       * Undoes the most recent step, restoring the selection it started
       * with.
       * @return <code>true</code> iff there was a step to undo.
       */
      bool undo();

      /**
       * Redoes the most recently undone step, restoring the selection it
       * ended with.
       * @return <code>true</code> iff there was a step to redo.
       */
      bool redo();

      /**
       * Selects every node of the entered container, or, when none is
       * entered, every child of every artboard and every node on the
       * pasteboard, locked ones included, as Adobe XD's Select All does.
       */
      void select_all();

    private:
      struct Record;
      Document* m_document;
      History m_history;
      Selection m_selection;
      EditContext m_context;
      TextSelection m_text_selection;
      bool m_is_updating;
      Subscription m_subscription;

      Editor(const Editor&) = delete;
      Editor& operator =(const Editor&) = delete;
      const Command* capture(auto apply);
      void on_selection();
  };

  auto& Editor::get_history(this auto& self) {
    return self.m_history;
  }

  auto& Editor::get_selection(this auto& self) {
    return self.m_selection;
  }

  auto& Editor::get_context(this auto& self) {
    return self.m_context;
  }

  auto& Editor::get_text_selection(this auto& self) {
    return self.m_text_selection;
  }
}

#endif

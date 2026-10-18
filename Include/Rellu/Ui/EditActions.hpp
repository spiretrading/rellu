#ifndef RELLU_UI_EDIT_ACTIONS_HPP
#define RELLU_UI_EDIT_ACTIONS_HPP
#include <functional>
#include <string>
#include <vector>
#include <QString>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/Clipboard.hpp"

namespace Rellu {

  /**
   * The edit, object and arrange actions of an ActionRegistry, applied to the
   * selection of an editor through commands. An action backed by a command
   * is enabled only while the command applies to the selection.
   */
  class EditActions {
    public:

      /**
       * The type of function reporting an operation that failed.
       * @param message The description of the failure.
       */
      using ErrorHandler = std::function<void (const QString& message)>;

      /**
       * Adds the actions, along with the object menu and its arrange
       * submenu, disabled until an editor is set. The registry must already
       * have the edit menu.
       * @param actions The registry to add the actions to.
       * @param commands The registry naming the commands applied, which must
       *        outlive this object.
       */
      EditActions(ActionRegistry& actions, const CommandRegistry& commands);

      ~EditActions();

      /** Returns the editor acted on, if any. */
      Editor* get_editor() const;

      /**
       * Sets the editor acted on, emptying the clipboard when the editor
       * changes.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this object or be replaced first.
       */
      void set_editor(Editor* editor);

      /** Returns the clipboard cut, copied and pasted through. */
      Clipboard& get_clipboard();

      /**
       * Sets the function reporting an operation that failed.
       * @param handler The function to call.
       */
      void set_error_handler(ErrorHandler handler);

      /** Updates the text and enabled state of every action. */
      void update();

    private:
      ActionRegistry* m_actions;
      const CommandRegistry* m_commands;
      Editor* m_editor;
      Clipboard m_clipboard;
      ErrorHandler m_error_handler;
      Subscription m_selection_subscription;
      Subscription m_history_subscription;
      std::vector<std::string> m_command_actions;

      EditActions(const EditActions&) = delete;
      EditActions& operator =(const EditActions&) = delete;
      void add_command(const std::string& name, const QString& text,
        const QKeySequence& shortcut, const std::string& menu,
        const std::string& section);
      void add_action(const std::string& name, const QString& text,
        const QKeySequence& shortcut, const std::string& menu,
        const std::string& section, std::function<void ()> handler);
      void run(auto operation);
      void cut();
      void copy();
      void paste();
  };
}

#endif

#ifndef RELLU_UI_EDIT_ACTIONS_HPP
#define RELLU_UI_EDIT_ACTIONS_HPP
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <QMetaObject>
#include <QMimeData>
#include <QString>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/Clipboard.hpp"

namespace Rellu {

  /**
   * Handles the edit, object and arrange actions of an ActionRegistry,
   * applied to the selection of an editor: the clipboard, history and
   * selection actions, and every action named after a command of a
   * CommandRegistry. An action backed by a command is enabled only while
   * the command applies to the selection, Reset to Main State only while no
   * text is edited in place, as in Adobe XD.
   */
  class EditActions {
    public:

      /**
       * The type of function reporting an operation that failed.
       * @param message The description of the failure.
       */
      using ErrorHandler = std::function<void (const QString& message)>;

      /**
       * The type of function returning the region of the document in view,
       * or none when it is unknown.
       */
      using ViewProvider = std::function<std::optional<Rect> ()>;

      /**
       * The type of function bringing a region of the document into view.
       * @param region The region in document coordinates.
       */
      using RevealHandler = std::function<void (const Rect& region)>;

      /**
       * The type of function pasting system clipboard data ahead of the
       * clipboard, such as copied guides.
       * @param data The clipboard data.
       * @param is_applied Whether to paste rather than only test.
       * @return <code>true</code> iff the function pastes the data.
       */
      using PasteHandler =
        std::function<bool (const QMimeData& data, bool is_applied)>;

      /**
       * Sets the handlers of the actions, which are disabled until an
       * editor is set. The registry must already have Adobe XD's menus.
       * @param actions The registry holding the actions.
       * @param commands The registry naming the commands applied, which must
       *        outlive this object.
       */
      EditActions(ActionRegistry& actions, const CommandRegistry& commands);

      ~EditActions();

      /** Returns the editor acted on, if any. */
      Editor* get_editor() const;

      /**
       * Sets the editor acted on. What the clipboard holds stays available
       * to paste into the next editor's document.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this object or be replaced first.
       */
      void set_editor(Editor* editor);

      /**
       * Returns the clipboard cut, copied and pasted through, which also
       * exchanges what it holds with the system clipboard.
       */
      Clipboard& get_clipboard();

      /**
       * Sets the function returning the region of the document in view,
       * which decides where content is pasted.
       * @param provider The function.
       */
      void set_view_provider(ViewProvider provider);

      /**
       * Sets the function bringing into view the artboards duplicated or
       * pasted, as Adobe XD scrolls to them.
       * @param handler The function.
       */
      void set_reveal_handler(RevealHandler handler);

      /**
       * Sets the workspace shown, which decides whether duplicating and
       * pasting copy interactions.
       * @param workspace The workspace.
       */
      void set_workspace(Workspace workspace);

      /**
       * Sets the function measuring text when what is copied is measured.
       * @param measurer The function.
       */
      void set_text_measurer(TextMeasurer measurer);

      /**
       * Sets the function reporting an operation that failed.
       * @param handler The function to call.
       */
      void set_error_handler(ErrorHandler handler);

      /**
       * Sets the function pasting system clipboard data ahead of the
       * clipboard.
       * @param handler The function.
       */
      void set_paste_handler(PasteHandler handler);

      /** Updates the text and enabled state of every action. */
      void update();

    private:
      ActionRegistry* m_actions;
      const CommandRegistry* m_commands;
      Editor* m_editor;
      Clipboard m_clipboard;
      ErrorHandler m_error_handler;
      PasteHandler m_paste_handler;
      ViewProvider m_view_provider;
      RevealHandler m_reveal_handler;
      Workspace m_workspace;
      TextMeasurer m_measurer;
      QMetaObject::Connection m_clipboard_connection;
      Subscription m_selection_subscription;
      Subscription m_history_subscription;
      Subscription m_text_selection_subscription;
      std::vector<std::string> m_names;
      std::vector<std::string> m_command_actions;

      EditActions(const EditActions&) = delete;
      EditActions& operator =(const EditActions&) = delete;
      void set_action(
        const std::string& name, std::function<void ()> handler);
      void run(auto operation);
      bool is_available(const std::string& name) const;
      bool copy_selection();
      void cut();
      void copy();
      void paste();
      void paste_appearance();
      void duplicate();
      std::optional<Rect> get_view() const;
      void reveal_artboards();
  };
}

#endif

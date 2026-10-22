#ifndef RELLU_UI_EXPORT_ACTIONS_HPP
#define RELLU_UI_EXPORT_ACTIONS_HPP
#include <functional>
#include <vector>
#include <QString>
#include <QWidget>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Io/ExportFormat.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AssetExport.hpp"
#include "Rellu/Ui/ExportDialog.hpp"
#include "Rellu/Ui/XdAlert.hpp"

namespace Rellu {

  /**
   * Handles Adobe XD's export actions, file.export.batch,
   * file.export.selected and file.export.all, enabling each while it
   * applies: showing the Export Assets dialog, confirming the replacement
   * of existing files, writing the files and reporting the assets that
   * failed. The options and folder of the latest export are remembered for
   * the next.
   */
  class ExportActions {
    public:

      /**
       * Shows the Export Assets dialog modally and returns when it closes.
       * @param dialog The dialog.
       */
      using DialogHandler = std::function<void (ExportDialog& dialog)>;

      /**
       * Shows an alert modally.
       * @param content The alert's content.
       * @return The index of the button pressed.
       */
      using AlertHandler = std::function<int (const XdAlertContent& content)>;

      /** Returns the path of the document's file, empty if untitled. */
      using PathProvider = std::function<QString ()>;

      /**
       * Constructs ExportActions without an editor.
       * @param actions The registry holding the actions.
       * @param window The widget whose window the dialogs cover.
       */
      ExportActions(ActionRegistry& actions, QWidget& window);

      ~ExportActions();

      /** Returns the editor acted on, or <code>nullptr</code>. */
      Editor* get_editor() const;

      /**
       * Sets the editor acted on.
       * @param editor The editor, or <code>nullptr</code> for none.
       */
      void set_editor(Editor* editor);

      /**
       * Sets the function measuring text.
       * @param measurer The function.
       */
      void set_text_measurer(TextMeasurer measurer);

      /**
       * Sets the function returning the path of the document's file.
       * @param provider The function.
       */
      void set_path_provider(PathProvider provider);

      /**
       * Sets the function showing the Export Assets dialog, by default its
       * exec.
       * @param handler The function.
       */
      void set_dialog_handler(DialogHandler handler);

      /**
       * Sets the function showing alerts, by default show_xd_alert.
       * @param handler The function.
       */
      void set_alert_handler(AlertHandler handler);

      /** Returns the options of the latest export. */
      const ExportOptions& get_options() const;

      /**
       * Returns the size typed into the Export size field of the latest
       * export, empty if one of its choices was chosen.
       */
      const QString& get_scale_text() const;

      /** Returns the folder of the latest export, empty if none. */
      const QString& get_folder() const;

      /**
       * Exports nodes through the Export Assets dialog.
       * @param nodes The nodes exported.
       * @param is_all_artboards Whether All Artboards are exported.
       */
      void export_nodes(const std::vector<NodeId>& nodes,
        bool is_all_artboards);

      /** Enables the actions that apply to the editor's document. */
      void update();

    private:
      ActionRegistry* m_actions;
      QWidget* m_window;
      Editor* m_editor;
      TextMeasurer m_measurer;
      PathProvider m_path_provider;
      DialogHandler m_dialog_handler;
      AlertHandler m_alert_handler;
      ExportOptions m_options;
      QString m_scale_text;
      QString m_folder;
      Subscription m_selection_subscription;
      Subscription m_history_subscription;

      ExportActions(const ExportActions&) = delete;
      ExportActions& operator =(const ExportActions&) = delete;
      void run(ExportDialog& dialog, const std::vector<NodeId>& nodes);
      void report(const ExportResult& result);
  };

  /**
   * Returns the alert Adobe XD shows to confirm replacing existing files.
   * @param names The names of the existing files.
   * @param folder The folder exported to.
   */
  XdAlertContent make_overwrite_alert(
    const std::vector<QString>& names, const QString& folder);

  /**
   * Returns the alert Adobe XD shows when assets fail to export.
   * @param failure How the assets failed.
   * @param count The number of assets that failed.
   * @param paths The paths of the files that failed.
   */
  XdAlertContent make_failure_alert(ExportFailure failure, int count,
    const std::vector<QString>& paths);
}

#endif

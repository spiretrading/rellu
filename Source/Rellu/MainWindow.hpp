#ifndef RELLU_MAIN_WINDOW_HPP
#define RELLU_MAIN_WINDOW_HPP
#include <memory>
#include <string>
#include <QMainWindow>
#include <QMenuBar>
#include <QStackedWidget>
#include <QString>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "Rellu/Ui/ComponentActions.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/PluginsPanel.hpp"
#include "Rellu/Ui/PreviewWindow.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/PrototypeInspector.hpp"
#include "Rellu/Ui/RecentFiles.hpp"
#include "Rellu/Ui/Toolbar.hpp"
#include "ComponentDrop.hpp"
#include "ComponentThumbnails.hpp"
#include "DocumentView.hpp"

namespace Rellu {

  /**
   * The application's top-level window editing one document, laid out as
   * Adobe XD's editor: the menu row, the app bar, the toolbar, the left
   * panel, the canvas and the property inspector.
   */
  class MainWindow : public QMainWindow {
    public:

      /**
       * Constructs a MainWindow without a document, restoring the geometry
       * and panel saved when the last MainWindow closed.
       * @param parent The parent widget.
       */
      explicit MainWindow(QWidget* parent = nullptr);

      ~MainWindow() override;

      /** Returns the registry of the window's actions. */
      ActionRegistry& get_actions();

      /**
       * Opens an Adobe XD file in place of the current document, asking
       * first what to do with the current document's unsaved changes and
       * reporting any failure to the user.
       * @param path The path of the file to open.
       * @return <code>true</code> iff the file was opened.
       */
      bool open(const QString& path);

      /**
       * Replaces the current document with a new, empty one, asking first
       * what to do with its unsaved changes.
       * @return <code>true</code> iff the new document was made.
       */
      bool make_new();

      /**
       * Saves the current document to its file, asking for a path if it has
       * none.
       * @return <code>true</code> iff the document was saved.
       */
      bool save();

      /**
       * Saves the current document to a file chosen by the user.
       * @return <code>true</code> iff the document was saved.
       */
      bool save_as();

    protected:
      void closeEvent(QCloseEvent* event) override;
      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      DocumentView* m_view;
      ActionRegistry m_actions;
      CommandRegistry m_commands;
      std::unique_ptr<EditActions> m_edit_actions;
      std::unique_ptr<ComponentActions> m_component_actions;
      ComponentThumbnails m_thumbnails;
      std::unique_ptr<ComponentDrop> m_component_drop;
      AppBar* m_app_bar;
      Toolbar* m_toolbar;
      QStackedWidget* m_left_panel;
      AssetsPanel* m_assets;
      LayersPanel* m_layers;
      PluginsPanel* m_plugins;
      PropertyInspector* m_inspector;
      PrototypeInspector* m_prototype_inspector;
      PreviewWindow* m_preview;
      QWidget* m_inspector_frame;
      RecentFiles m_recent_files;
      TextMeasurer m_measurer;
      std::unique_ptr<DocumentSession> m_session;
      Subscription m_history_subscription;
      Subscription m_selection_subscription;
      Subscription m_tool_subscription;

      QMenuBar* make_layout();
      void add_actions();
      void add_component_actions();
      void add_tool_actions();
      void select_tool(const std::string& name);
      void update_tool();
      void show_panel(const std::string& name);
      void set_workspace(Workspace workspace);
      void toggle_preview();
      void restore_settings();
      void save_settings() const;
      void update_recent_files();
      void prompt_open();
      bool save_to(const QString& path);
      bool confirm_close();
      UnsavedChoice prompt_unsaved();
      void install(std::unique_ptr<Document> document, const QString& path);
      void detach();
      void update_title();
      void update_selection_actions();
      void reveal(const NodeId& id);
      void zoom_to_selection();
      double get_zoom() const;
      void update_zoom();
  };
}

#endif

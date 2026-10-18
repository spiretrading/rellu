#ifndef RELLU_MAIN_WINDOW_HPP
#define RELLU_MAIN_WINDOW_HPP
#include <memory>
#include <string>
#include <QByteArray>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPointF>
#include <QString>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/RecentFiles.hpp"
#include "DocumentView.hpp"

namespace Rellu {

  /** The application's top-level window editing one document. */
  class MainWindow : public QMainWindow {
    public:

      /**
       * Constructs a MainWindow without a document, restoring the layout
       * saved when the last MainWindow closed.
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
      LayersPanel* m_layers;
      PropertyInspector* m_inspector;
      QDockWidget* m_layers_dock;
      QDockWidget* m_inspector_dock;
      QLabel* m_message;
      QLabel* m_cursor;
      QLabel* m_zoom;
      RecentFiles m_recent_files;
      QByteArray m_default_layout;
      TextMeasurer m_measurer;
      std::unique_ptr<DocumentSession> m_session;
      Subscription m_history_subscription;

      QDockWidget* make_dock(
        const QString& title, const QString& name, QWidget* widget);
      void add_actions();
      void add_panel_action(const std::string& name, const QString& text,
        QDockWidget& dock);
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
      void reveal(const NodeId& id);
      double get_zoom() const;
      void update_zoom();
      void update_cursor(const QPointF& position);
  };
}

#endif

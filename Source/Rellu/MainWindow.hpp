#ifndef RELLU_MAIN_WINDOW_HPP
#define RELLU_MAIN_WINDOW_HPP
#include <string>
#include <QByteArray>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPointF>
#include <QString>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/RecentFiles.hpp"
#include "DocumentView.hpp"

namespace Rellu {

  /** The application's top-level window showing one document. */
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
       * Opens an Adobe XD file, reporting any failure to the user.
       * @param path The path of the file to open.
       * @return <code>true</code> iff the file was opened.
       */
      bool open(const QString& path);

    protected:
      void closeEvent(QCloseEvent* event) override;
      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      DocumentView* m_view;
      ActionRegistry m_actions;
      LayersPanel* m_layers;
      PropertyInspector* m_inspector;
      QDockWidget* m_layers_dock;
      QDockWidget* m_inspector_dock;
      QLabel* m_message;
      QLabel* m_cursor;
      QLabel* m_zoom;
      RecentFiles m_recent_files;
      QByteArray m_default_layout;
      Document* m_document;

      QDockWidget* make_dock(
        const QString& title, const QString& name, QWidget* widget);
      void add_actions();
      void add_panel_action(const std::string& name, const QString& text,
        QDockWidget& dock);
      void restore_settings();
      void save_settings() const;
      void update_recent_files();
      void prompt_open();
      double get_zoom() const;
      void update_zoom();
      void update_cursor(const QPointF& position);
  };
}

#endif

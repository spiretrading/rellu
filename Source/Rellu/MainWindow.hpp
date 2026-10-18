#ifndef RELLU_MAIN_WINDOW_HPP
#define RELLU_MAIN_WINDOW_HPP
#include <QMainWindow>
#include <QString>
#include "DocumentView.hpp"

namespace Rellu {

  /** The application's top-level window showing one document. */
  class MainWindow : public QMainWindow {
    public:

      /**
       * Constructs a MainWindow without a document.
       * @param parent The parent widget.
       */
      explicit MainWindow(QWidget* parent = nullptr);

      /**
       * Opens an Adobe XD file, reporting any failure to the user.
       * @param path The path of the file to open.
       * @return <code>true</code> iff the file was opened.
       */
      bool open(const QString& path);

    private:
      DocumentView* m_view;

      void prompt_open();
  };
}

#endif

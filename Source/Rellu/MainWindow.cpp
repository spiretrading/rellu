#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include "Rellu/Io/XdImporters.hpp"
#include "MainWindow.hpp"

using namespace Rellu;

namespace {
  double to_milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }
}

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent),
    m_view(new DocumentView()) {
  setWindowTitle("Rellu");
  setCentralWidget(m_view);
  auto file = menuBar()->addMenu("&File");
  auto open = file->addAction("&Open...");
  open->setShortcut(QKeySequence::Open);
  connect(open, &QAction::triggered, this, [this] { prompt_open(); });
  file->addSeparator();
  auto quit = file->addAction("E&xit");
  quit->setShortcut(QKeySequence::Quit);
  connect(quit, &QAction::triggered, this, [this] { close(); });
  auto view = menuBar()->addMenu("&View");
  auto fit = view->addAction("Zoom to &Fit");
  fit->setShortcut(QKeySequence("Ctrl+0"));
  connect(fit, &QAction::triggered, this, [this] { m_view->fit(); });
  statusBar();
}

bool MainWindow::open(const QString& path) {
  QApplication::setOverrideCursor(Qt::WaitCursor);
  auto document = std::make_unique<Document>();
  auto start = std::chrono::steady_clock::now();
  try {
    import_xd(std::filesystem::path(path.toStdU16String()), *document);
  } catch(const std::exception& error) {
    QApplication::restoreOverrideCursor();
    QMessageBox::critical(this, "Rellu",
      QString("Unable to open %1:\n%2").arg(path, error.what()));
    return false;
  }
  auto imported = std::chrono::steady_clock::now();
  auto node_count = document->get_nodes().get_size();
  m_view->set_document(std::move(document));
  auto shown = std::chrono::steady_clock::now();
  QApplication::restoreOverrideCursor();
  setWindowTitle(
    QString("%1 - Rellu").arg(QFileInfo(path).completeBaseName()));
  statusBar()->showMessage(
    QString("%1 nodes, imported in %2 ms, scene built in %3 ms").arg(
      node_count).arg(to_milliseconds(imported - start), 0, 'f', 1).arg(
      to_milliseconds(shown - imported), 0, 'f', 1));
  m_view->setFocus();
  return true;
}

void MainWindow::prompt_open() {
  auto path = QFileDialog::getOpenFileName(
    this, "Open", QString(), "Adobe XD Files (*.xd);;All Files (*)");
  if(!path.isEmpty()) {
    open(path);
  }
}

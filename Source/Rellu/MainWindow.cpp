#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QStatusBar>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "MainWindow.hpp"

using namespace Rellu;

namespace {
  constexpr auto LAYOUT_VERSION = 1;
  constexpr auto ZOOM_STEP = 2.0;
  constexpr auto MAXIMUM_RECENT_FILES = RecentFiles::DEFAULT_CAPACITY;

  double to_milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  std::string get_recent_name(int index) {
    return "file.recent." + std::to_string(index);
  }
}

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent),
    m_view(new DocumentView()),
    m_actions(*this),
    m_layers(new LayersPanel()),
    m_inspector(new PropertyInspector()),
    m_message(new QLabel()),
    m_cursor(new QLabel()),
    m_zoom(new QLabel()),
    m_document(nullptr) {
  setWindowTitle("Rellu");
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
  setCentralWidget(m_view);
  m_view->setMouseTracking(true);
  m_view->installEventFilter(this);
  m_layers_dock = make_dock("Layers", "layers_dock", m_layers);
  addDockWidget(Qt::LeftDockWidgetArea, m_layers_dock);
  m_inspector_dock = make_dock("Properties", "properties_dock", m_inspector);
  addDockWidget(Qt::RightDockWidgetArea, m_inspector_dock);
  resizeDocks({m_layers_dock, m_inspector_dock}, {260, 280}, Qt::Horizontal);
  m_layers->set_current_handler([this] (const auto& current) {
    m_inspector->set_current(current);
  });
  statusBar()->addWidget(m_message, 1);
  statusBar()->addPermanentWidget(m_cursor);
  statusBar()->addPermanentWidget(m_zoom);
  add_actions();
  m_actions.build(*menuBar());
  m_default_layout = saveState(LAYOUT_VERSION);
  restore_settings();
  update_zoom();
}

MainWindow::~MainWindow() {
  m_layers->set_document(nullptr);
  m_inspector->set_document(nullptr);
}

ActionRegistry& MainWindow::get_actions() {
  return m_actions;
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
    if(!QFileInfo::exists(path)) {
      m_recent_files.remove(path);
      update_recent_files();
    }
    return false;
  }
  auto imported = std::chrono::steady_clock::now();
  auto node_count = document->get_nodes().get_size();
  m_layers->set_document(nullptr);
  m_inspector->set_document(nullptr);
  m_document = document.get();
  m_view->set_document(std::move(document));
  auto shown = std::chrono::steady_clock::now();
  m_layers->set_document(m_document);
  m_inspector->set_document(m_document);
  auto listed = std::chrono::steady_clock::now();
  QApplication::restoreOverrideCursor();
  setWindowTitle(
    QString("%1 - Rellu").arg(QFileInfo(path).completeBaseName()));
  m_message->setText(QString(
    "%1 nodes  |  imported in %2 ms  |  scene built in %3 ms  |  "
    "layers listed in %4 ms").arg(node_count).arg(
      to_milliseconds(imported - start), 0, 'f', 1).arg(
      to_milliseconds(shown - imported), 0, 'f', 1).arg(
      to_milliseconds(listed - shown), 0, 'f', 1));
  m_recent_files.add(path);
  update_recent_files();
  save_settings();
  update_zoom();
  m_view->setFocus();
  return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
  save_settings();
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_view) {
    if(event->type() == QEvent::MouseMove) {
      update_cursor(static_cast<QMouseEvent*>(event)->position());
    } else if(event->type() == QEvent::Leave) {
      m_cursor->clear();
    } else if(event->type() == QEvent::Paint) {
      update_zoom();
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

QDockWidget* MainWindow::make_dock(
    const QString& title, const QString& name, QWidget* widget) {
  auto dock = new QDockWidget(title);
  dock->setObjectName(name);
  dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  dock->setFeatures(QDockWidget::DockWidgetClosable |
    QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  dock->setWidget(widget);
  return dock;
}

void MainWindow::add_actions() {
  m_actions.add(MenuDefinition("file", "&File"));
  m_actions.add(MenuDefinition("edit", "&Edit"));
  m_actions.add(MenuDefinition("view", "&View"));
  m_actions.add(ActionDefinition(
    "file.open", "&Open...", QKeySequence::Open, "file", "open"));
  m_actions.set_handler("file.open", [this] { prompt_open(); });
  m_actions.add(MenuDefinition("file.recent", "Open &Recent", "file", "open"));
  m_actions.add(ActionDefinition("file.recent.clear", "&Clear Recent Files",
    QKeySequence(), "file.recent", "clear"));
  m_actions.set_handler("file.recent.clear", [this] {
    m_recent_files.clear();
    update_recent_files();
    save_settings();
  });
  m_actions.add(ActionDefinition(
    "file.exit", "E&xit", QKeySequence::Quit, "file", "exit"));
  m_actions.set_handler("file.exit", [this] { close(); });
  m_actions.add(ActionDefinition(
    "edit.undo", "&Undo", QKeySequence::Undo, "edit", "history"));
  m_actions.add(ActionDefinition(
    "edit.redo", "&Redo", QKeySequence::Redo, "edit", "history"));
  m_actions.add(ActionDefinition(
    "edit.cut", "Cu&t", QKeySequence::Cut, "edit", "clipboard"));
  m_actions.add(ActionDefinition(
    "edit.copy", "&Copy", QKeySequence::Copy, "edit", "clipboard"));
  m_actions.add(ActionDefinition(
    "edit.paste", "&Paste", QKeySequence::Paste, "edit", "clipboard"));
  m_actions.add(ActionDefinition(
    "edit.delete", "&Delete", QKeySequence::Delete, "edit", "clipboard"));
  m_actions.add(ActionDefinition("edit.select_all", "Select &All",
    QKeySequence::SelectAll, "edit", "selection"));
  m_actions.add(ActionDefinition(
    "view.zoom_in", "Zoom &In", QKeySequence("Ctrl+="), "view", "zoom"));
  m_actions.set_handler("view.zoom_in", [this] {
    m_view->set_zoom(get_zoom() * ZOOM_STEP);
  });
  m_actions.add(ActionDefinition(
    "view.zoom_out", "Zoom &Out", QKeySequence("Ctrl+-"), "view", "zoom"));
  m_actions.set_handler("view.zoom_out", [this] {
    m_view->set_zoom(get_zoom() / ZOOM_STEP);
  });
  m_actions.add(ActionDefinition(
    "view.zoom_fit", "Zoom to &Fit", QKeySequence("Ctrl+0"), "view", "zoom"));
  m_actions.set_handler("view.zoom_fit", [this] { m_view->fit(); });
  m_actions.add(ActionDefinition("view.zoom_actual", "&Actual Size",
    QKeySequence("Ctrl+1"), "view", "zoom"));
  m_actions.set_handler("view.zoom_actual", [this] { m_view->set_zoom(1); });
  m_actions.add(MenuDefinition("view.panels", "&Panels", "view", "panels"));
  add_panel_action("view.panels.layers", "&Layers", *m_layers_dock);
  add_panel_action("view.panels.properties", "&Properties", *m_inspector_dock);
  m_actions.add(ActionDefinition("view.panels.reset", "&Reset Layout",
    QKeySequence(), "view.panels", "reset"));
  m_actions.set_handler("view.panels.reset", [this] {
    restoreState(m_default_layout, LAYOUT_VERSION);
  });
}

void MainWindow::add_panel_action(
    const std::string& name, const QString& text, QDockWidget& dock) {
  auto& action = m_actions.add(ActionDefinition(
    name, text, QKeySequence(), "view.panels", "panels", true));
  action.setChecked(true);
  m_actions.set_handler(name, [&action, &dock] {
    dock.setVisible(action.isChecked());
  });
  connect(dock.toggleViewAction(), &QAction::toggled, &action,
    &QAction::setChecked);
}

void MainWindow::restore_settings() {
  auto settings = QSettings();
  m_recent_files.set_paths(settings.value("recent_files").toStringList());
  update_recent_files();
  if(!restoreGeometry(settings.value("main_window/geometry").toByteArray())) {
    resize(1280, 800);
  }
  restoreState(
    settings.value("main_window/layout").toByteArray(), LAYOUT_VERSION);
}

void MainWindow::save_settings() const {
  auto settings = QSettings();
  settings.setValue("recent_files", m_recent_files.get_paths());
  settings.setValue("main_window/geometry", saveGeometry());
  settings.setValue("main_window/layout", saveState(LAYOUT_VERSION));
}

void MainWindow::update_recent_files() {
  for(auto i = 0; i != MAXIMUM_RECENT_FILES; ++i) {
    m_actions.remove(get_recent_name(i));
  }
  auto& paths = m_recent_files.get_paths();
  for(auto i = 0; i != static_cast<int>(paths.size()); ++i) {
    auto path = paths[i];
    auto name = get_recent_name(i);
    auto text = QFileInfo(path).fileName().replace("&", "&&");
    auto& action = m_actions.add(ActionDefinition(name,
      QString("&%1 %2").arg(i + 1).arg(text), QKeySequence(), "file.recent",
      "files"));
    action.setToolTip(path);
    action.setStatusTip(path);
    m_actions.set_handler(name, [this, path] { open(path); });
  }
  m_actions.set_enabled("file.recent.clear", !paths.isEmpty());
}

void MainWindow::prompt_open() {
  auto path = QFileDialog::getOpenFileName(
    this, "Open", QString(), "Adobe XD Files (*.xd);;All Files (*)");
  if(!path.isEmpty()) {
    open(path);
  }
}

double MainWindow::get_zoom() const {
  return m_view->get_renderer().get_view().m_a;
}

void MainWindow::update_zoom() {
  auto text = QString("%1%").arg(std::round(get_zoom() * 1000) / 10);
  if(m_zoom->text() != text) {
    m_zoom->setText(text);
  }
}

void MainWindow::update_cursor(const QPointF& position) {
  if(!m_document) {
    m_cursor->clear();
    return;
  }
  auto inverse = invert(m_view->get_renderer().get_view());
  if(!inverse) {
    m_cursor->clear();
    return;
  }
  auto point = inverse->map(Point(position.x(), position.y()));
  m_cursor->setText(QString("X: %1  Y: %2").arg(
    std::round(point.m_x * 10) / 10).arg(std::round(point.m_y * 10) / 10));
}

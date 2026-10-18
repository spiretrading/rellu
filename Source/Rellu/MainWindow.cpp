#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "MainWindow.hpp"

using namespace Rellu;

namespace {
  constexpr auto LAYOUT_VERSION = 1;
  constexpr auto ZOOM_STEP = 2.0;
  constexpr auto MAXIMUM_RECENT_FILES = RecentFiles::DEFAULT_CAPACITY;
  constexpr auto REVEAL_MARGIN = 24.0;
  const auto FILE_FILTER = QString("Adobe XD Files (*.xd);;All Files (*)");

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
    m_measurer(make_text_measurer(get_default_font_library())) {
  add_standard_commands(m_commands);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
  setCentralWidget(m_view);
  m_view->setMouseTracking(true);
  m_view->installEventFilter(this);
  m_layers_dock = make_dock("Layers", "layers_dock", m_layers);
  addDockWidget(Qt::LeftDockWidgetArea, m_layers_dock);
  m_inspector_dock = make_dock("Properties", "properties_dock", m_inspector);
  addDockWidget(Qt::RightDockWidgetArea, m_inspector_dock);
  resizeDocks({m_layers_dock, m_inspector_dock}, {260, 280}, Qt::Horizontal);
  m_inspector->set_measurer(m_measurer);
  m_inspector->set_property_editors(
    make_standard_property_editors(m_measurer));
  m_layers->set_activation_handler([this] (const auto& id) {
    reveal(id);
  });
  statusBar()->addWidget(m_message, 1);
  statusBar()->addPermanentWidget(m_cursor);
  statusBar()->addPermanentWidget(m_zoom);
  add_actions();
  m_actions.build(*menuBar());
  m_default_layout = saveState(LAYOUT_VERSION);
  restore_settings();
  update_title();
  update_zoom();
}

MainWindow::~MainWindow() {
  detach();
  delete m_view;
  m_view = nullptr;
  m_session.reset();
}

ActionRegistry& MainWindow::get_actions() {
  return m_actions;
}

bool MainWindow::open(const QString& path) {
  if(!confirm_close()) {
    return false;
  }
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
  install(std::move(document), path);
  auto shown = std::chrono::steady_clock::now();
  QApplication::restoreOverrideCursor();
  m_message->setText(QString(
    "%1 nodes  |  imported in %2 ms  |  shown in %3 ms").arg(
      node_count).arg(to_milliseconds(imported - start), 0, 'f', 1).arg(
      to_milliseconds(shown - imported), 0, 'f', 1));
  m_recent_files.add(path);
  update_recent_files();
  save_settings();
  return true;
}

bool MainWindow::make_new() {
  if(!confirm_close()) {
    return false;
  }
  install(std::make_unique<Document>(), QString());
  m_message->setText("New document");
  return true;
}

bool MainWindow::save() {
  if(!m_session) {
    return false;
  } else if(m_session->get_path().isEmpty()) {
    return save_as();
  }
  return save_to(m_session->get_path());
}

bool MainWindow::save_as() {
  if(!m_session) {
    return false;
  }
  auto initial = m_session->get_path();
  if(initial.isEmpty()) {
    initial = m_session->get_name() + ".xd";
  }
  auto path = QFileDialog::getSaveFileName(this, "Save As", initial,
    FILE_FILTER);
  if(path.isEmpty()) {
    return false;
  }
  return save_to(to_xd_path(path));
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if(!confirm_close()) {
    event->ignore();
    return;
  }
  save_settings();
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if(m_view && watched == m_view) {
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
  m_actions.add(ActionDefinition(
    "file.new", "&New", QKeySequence("Ctrl+N"), "file", "open"));
  m_actions.set_handler("file.new", [this] { make_new(); });
  m_actions.add(ActionDefinition(
    "file.open", "&Open...", QKeySequence("Ctrl+O"), "file", "open"));
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
    "file.save", "&Save", QKeySequence("Ctrl+S"), "file", "save"));
  m_actions.set_handler("file.save", [this] { save(); });
  m_actions.add(ActionDefinition("file.save_as", "Save &As...",
    QKeySequence("Shift+Ctrl+S"), "file", "save"));
  m_actions.set_handler("file.save_as", [this] { save_as(); });
  m_actions.add(ActionDefinition(
    "file.exit", "E&xit", QKeySequence("Ctrl+Q"), "file", "exit"));
  m_actions.set_handler("file.exit", [this] { close(); });
  m_edit_actions = std::make_unique<EditActions>(m_actions, m_commands);
  m_edit_actions->set_error_handler([this] (const auto& message) {
    m_message->setText(message);
  });
  m_actions.add(MenuDefinition("view", "&View"));
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
  auto path = QFileDialog::getOpenFileName(this, "Open", QString(),
    FILE_FILTER);
  if(!path.isEmpty()) {
    open(path);
  }
}

bool MainWindow::save_to(const QString& path) {
  QApplication::setOverrideCursor(Qt::WaitCursor);
  try {
    m_session->save(path);
  } catch(const std::exception& error) {
    QApplication::restoreOverrideCursor();
    QMessageBox::critical(this, "Rellu",
      QString("Unable to save %1:\n%2").arg(path, error.what()));
    return false;
  }
  QApplication::restoreOverrideCursor();
  m_message->setText(QString("Saved %1").arg(path));
  m_recent_files.add(path);
  update_recent_files();
  save_settings();
  update_title();
  return true;
}

bool MainWindow::confirm_close() {
  if(!m_session) {
    return true;
  }
  return resolve_unsaved_changes(*m_session, [this] {
    return prompt_unsaved();
  }, [this] {
    return save();
  });
}

UnsavedChoice MainWindow::prompt_unsaved() {
  auto box = QMessageBox(QMessageBox::Warning, "Rellu",
    QString("Do you want to save the changes you made to %1?").arg(
      m_session->get_name()),
    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
  box.setInformativeText("Your changes will be lost if you don't save them.");
  box.button(QMessageBox::Discard)->setText("Do&n't Save");
  box.setDefaultButton(QMessageBox::Save);
  auto result = box.exec();
  if(result == QMessageBox::Save) {
    return UnsavedChoice::SAVE;
  } else if(result == QMessageBox::Discard) {
    return UnsavedChoice::DISCARD;
  }
  return UnsavedChoice::CANCEL;
}

void MainWindow::install(
    std::unique_ptr<Document> document, const QString& path) {
  auto session = std::make_unique<DocumentSession>(
    *document, path, make_xd_text_layout(get_default_font_library()));
  detach();
  m_view->set_editor(&session->get_editor());
  m_view->set_document(std::move(document));
  m_session = std::move(session);
  auto& editor = m_session->get_editor();
  m_layers->set_editor(&editor);
  m_inspector->set_editor(&editor);
  m_edit_actions->set_editor(&editor);
  m_history_subscription = editor.get_history().subscribe([this] {
    update_title();
  });
  update_title();
  update_zoom();
  m_view->setFocus();
}

void MainWindow::detach() {
  m_history_subscription.release();
  m_edit_actions->set_editor(nullptr);
  m_inspector->set_editor(nullptr);
  m_layers->set_editor(nullptr);
  if(m_view) {
    m_view->set_editor(nullptr);
  }
}

void MainWindow::update_title() {
  auto has_session = static_cast<bool>(m_session);
  if(has_session) {
    setWindowTitle(QString("%1[*] - Rellu").arg(m_session->get_name()));
    setWindowModified(m_session->is_dirty());
  } else {
    setWindowTitle("Rellu");
    setWindowModified(false);
  }
  m_actions.set_enabled("file.save", has_session);
  m_actions.set_enabled("file.save_as", has_session);
}

void MainWindow::reveal(const NodeId& id) {
  if(!m_session || m_view->width() <= 0 || m_view->height() <= 0) {
    return;
  }
  auto bounds = get_world_bounds(m_session->get_document(), id,
    BoundsType::GEOMETRY, m_measurer);
  auto inverse = invert(m_view->get_renderer().get_view());
  if(!bounds || !inverse) {
    return;
  }
  auto visible = inverse->map(Rect(0, 0, m_view->width(), m_view->height()));
  if(visible.intersects(*bounds) || visible.contains(bounds->get_center())) {
    return;
  }
  auto zoom = get_zoom();
  auto size = Size(
    std::max(m_view->width() - 2 * REVEAL_MARGIN, 1.0) / zoom,
    std::max(m_view->height() - 2 * REVEAL_MARGIN, 1.0) / zoom);
  if(bounds->m_width > size.m_width || bounds->m_height > size.m_height) {
    m_view->zoom_to(*bounds);
    return;
  }
  auto center = bounds->get_center();
  m_view->zoom_to(Rect(center.m_x - size.m_width / 2,
    center.m_y - size.m_height / 2, size.m_width, size.m_height));
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
  if(!m_session) {
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

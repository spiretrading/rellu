#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "Rellu/Ui/TextPropertyEditors.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "MainWindow.hpp"

using namespace Rellu;

namespace {
  constexpr auto ZOOM_STEP = 2.0;
  constexpr auto MAXIMUM_RECENT_FILES = 15;
  constexpr auto REVEAL_MARGIN = 24.0;
  constexpr auto LEFT_PANEL_WIDTH = 407;
  constexpr auto MENU_ROW_HEIGHT = 32;
  const auto FILE_FILTER = QString("Adobe XD Files (*.xd);;All Files (*)");
  const auto PASTEBOARD = QColor(0xE4, 0xE4, 0xE4);
  const auto DEFAULT_PANEL = QString("layers");

  std::string get_recent_name(int index) {
    return "file.recent." + std::to_string(index);
  }

  std::string get_panel_action(const std::string& panel) {
    if(panel == "assets") {
      return "view.libraries";
    } else if(panel == "layers") {
      return "view.layers";
    }
    return "view.plugins";
  }

  QPixmap make_mark(double ratio) {
    auto size = 20;
    auto pixmap = QPixmap(static_cast<int>(size * ratio),
      static_cast<int>(size * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x2B, 0x2F, 0x4A));
    painter.drawRoundedRect(QRectF(0, 0, size, size), 3, 3);
    auto font = QFont("Segoe UI");
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(0x8F, 0xD3, 0xFF));
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, "Rl");
    return pixmap;
  }
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_view(new DocumentView()),
      m_actions(*this),
      m_app_bar(new AppBar()),
      m_toolbar(new Toolbar(make_xd_tools(), make_xd_panels())),
      m_left_panel(new QStackedWidget()),
      m_assets(new AssetsPanel()),
      m_layers(new LayersPanel()),
      m_plugins(new PluginsPanel()),
      m_inspector(new PropertyInspector()),
      m_inspector_frame(new QWidget()),
      m_recent_files(MAXIMUM_RECENT_FILES),
      m_measurer(make_text_measurer(get_default_font_library())) {
  add_standard_commands(m_commands);
  m_view->get_renderer().set_background(PASTEBOARD);
  m_view->installEventFilter(this);
  m_inspector->set_measurer(m_measurer);
  auto editors = make_standard_property_editors(m_measurer);
  add_text_property_editors(editors, get_default_font_library(), m_measurer,
    [this] () -> const TextSelection* {
      if(!m_session) {
        return nullptr;
      }
      return &m_session->get_editor().get_text_selection();
    });
  m_inspector->set_property_editors(std::move(editors));
  m_inspector->set_actions(&m_actions);
  m_layers->set_activation_handler([this] (const auto& id) {
    reveal(id);
  });
  m_toolbar->set_tool_handler([this] (const auto& name) {
    select_tool(name);
  });
  m_toolbar->set_panel_handler([this] (const auto& name) {
    show_panel(name);
  });
  m_app_bar->set_workspace_handler([this] (auto workspace) {
    set_workspace(workspace);
  });
  m_app_bar->set_zoom_handler([this] (auto zoom) {
    m_view->set_zoom(zoom);
  });
  auto bar = make_layout();
  add_xd_menus(m_actions);
  add_actions();
  m_actions.build(*bar);
  restore_settings();
  update_title();
  update_selection_actions();
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
  install(std::move(document), path);
  QApplication::restoreOverrideCursor();
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
  if(m_view && watched == m_view && event->type() == QEvent::Paint) {
    update_zoom();
  }
  return QMainWindow::eventFilter(watched, event);
}

QMenuBar* MainWindow::make_layout() {
  auto menu_row = new QWidget();
  menu_row->setObjectName("menu_row");
  menu_row->setAttribute(Qt::WA_StyledBackground);
  menu_row->setFixedHeight(MENU_ROW_HEIGHT);
  auto menu_layout = new QHBoxLayout(menu_row);
  menu_layout->setContentsMargins(15, 0, 0, 0);
  menu_layout->setSpacing(14);
  auto mark = new QLabel();
  mark->setPixmap(make_mark(devicePixelRatioF()));
  mark->setToolTip("Rellu");
  menu_layout->addWidget(mark);
  auto bar = new QMenuBar();
  bar->setNativeMenuBar(false);
  menu_layout->addWidget(bar, 1);
  setMenuWidget(menu_row);
  auto central = new QWidget();
  central->setObjectName("central");
  auto layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_app_bar);
  auto body = new QWidget();
  auto body_layout = new QHBoxLayout(body);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(0);
  body_layout->addWidget(m_toolbar);
  m_left_panel->setObjectName("left_panel");
  m_left_panel->setFixedWidth(LEFT_PANEL_WIDTH);
  m_left_panel->addWidget(m_assets);
  m_left_panel->addWidget(m_layers);
  m_left_panel->addWidget(m_plugins);
  body_layout->addWidget(m_left_panel);
  body_layout->addWidget(m_view, 1);
  m_inspector_frame->setObjectName("inspector_frame");
  m_inspector_frame->setAttribute(Qt::WA_StyledBackground);
  m_inspector_frame->setFixedWidth(PropertyInspector::WIDTH);
  auto inspector_layout = new QVBoxLayout(m_inspector_frame);
  inspector_layout->setContentsMargins(1, 0, 0, 0);
  inspector_layout->addWidget(m_inspector);
  body_layout->addWidget(m_inspector_frame);
  layout->addWidget(body, 1);
  setCentralWidget(central);
  return bar;
}

void MainWindow::add_actions() {
  m_actions.set_handler("file.new", [this] { make_new(); });
  m_actions.set_handler("file.open", [this] { prompt_open(); });
  m_actions.set_handler("file.open_local", [this] { prompt_open(); });
  m_actions.set_handler("file.close", [this] { close(); });
  m_actions.set_handler("file.save", [this] { save(); });
  m_actions.set_handler("file.save_as", [this] { save_as(); });
  m_actions.set_handler("file.save_as_local", [this] { save_as(); });
  m_edit_actions = std::make_unique<EditActions>(m_actions, m_commands);
  m_edit_actions->set_error_handler([this] (const auto& message) {
    QMessageBox::warning(this, "Rellu", message);
  });
  m_actions.set_handler("edit.rename", [this] {
    if(!m_session) {
      return;
    }
    auto& primary = m_session->get_editor().get_selection().get_primary();
    if(primary.is_nil()) {
      return;
    }
    show_panel("layers");
    m_layers->rename(primary);
  });
  auto& zoom_in = m_actions.get("view.zoom_in");
  zoom_in.setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
  m_actions.set_handler("view.zoom_in", [this] {
    m_view->set_zoom(get_zoom() * ZOOM_STEP);
  });
  m_actions.set_handler("view.zoom_out", [this] {
    m_view->set_zoom(get_zoom() / ZOOM_STEP);
  });
  m_actions.set_handler("view.zoom_actual", [this] { m_view->set_zoom(1); });
  m_actions.set_handler("view.zoom_double", [this] { m_view->set_zoom(2); });
  m_actions.set_handler("view.zoom_selection", [this] {
    zoom_to_selection();
  });
  m_actions.set_handler("view.zoom_fit", [this] { m_view->fit(); });
  for(auto panel : {"assets", "layers", "plugins"}) {
    auto name = std::string(panel);
    m_actions.set_handler(get_panel_action(name), [this, name] {
      if(m_toolbar->get_panel() == name) {
        show_panel("");
      } else {
        show_panel(name);
      }
    });
  }
  auto& document = m_actions.add(ActionDefinition(
    "window.document", "Untitled", QKeySequence(), "window", "documents",
    true));
  document.setChecked(true);
  m_actions.set_handler("window.document", [this] {
    m_actions.get("window.document").setChecked(true);
    activateWindow();
  });
  m_actions.set_handler("help.about", [this] {
    QMessageBox::about(this, "About Rellu", "Rellu\n\n"
      "An editor for Adobe XD documents.");
  });
  add_tool_actions();
}

void MainWindow::add_tool_actions() {
  for(auto& tool : m_toolbar->get_tools()) {
    auto name = "tool." + tool.m_name;
    m_actions.add(
      ActionDefinition(name, tool.m_label, tool.m_shortcut, "", "tools"));
    m_actions.set_handler(name, [this, tool = tool.m_name] {
      select_tool(tool);
    });
  }
}

void MainWindow::select_tool(const std::string& name) {
  if(auto tools = m_view->get_tools(); tools && tools->has_tool(name)) {
    tools->set_tool(name);
  }
  update_tool();
}

void MainWindow::update_tool() {
  if(auto tools = m_view->get_tools()) {
    m_toolbar->set_tool(tools->get_tool_name());
  }
}

void MainWindow::show_panel(const std::string& name) {
  auto panel = name;
  if(m_app_bar->get_workspace() == Workspace::SHARE) {
    panel.clear();
  }
  m_toolbar->set_panel(name);
  if(name == "assets") {
    m_left_panel->setCurrentWidget(m_assets);
  } else if(name == "layers") {
    m_left_panel->setCurrentWidget(m_layers);
  } else if(name == "plugins") {
    m_left_panel->setCurrentWidget(m_plugins);
  }
  m_left_panel->setVisible(!panel.empty());
  for(auto candidate : {"assets", "layers", "plugins"}) {
    m_actions.get(get_panel_action(candidate)).setChecked(
      m_toolbar->get_panel() == candidate);
  }
}

void MainWindow::set_workspace(Workspace workspace) {
  m_app_bar->set_workspace(workspace);
  for(auto& tool : m_toolbar->get_tools()) {
    m_toolbar->set_tool_visible(tool.m_name, workspace == Workspace::DESIGN ||
      tool.m_name == "select" || tool.m_name == "zoom");
  }
  if(workspace != Workspace::DESIGN && m_toolbar->get_tool() != "zoom") {
    select_tool("select");
  }
  for(auto& tool : m_toolbar->get_tools()) {
    m_actions.set_enabled("tool." + tool.m_name,
      workspace == Workspace::DESIGN || tool.m_name == "select" ||
      tool.m_name == "zoom");
  }
  m_inspector->set_workspace(workspace);
  show_panel(m_toolbar->get_panel());
}

void MainWindow::restore_settings() {
  auto settings = QSettings();
  m_recent_files.set_paths(settings.value("recent_files").toStringList());
  update_recent_files();
  if(!restoreGeometry(settings.value("main_window/geometry").toByteArray())) {
    resize(1600, 960);
  }
  show_panel(settings.value("main_window/panel", DEFAULT_PANEL).toString().
    toStdString());
}

void MainWindow::save_settings() const {
  auto settings = QSettings();
  settings.setValue("recent_files", m_recent_files.get_paths());
  settings.setValue("main_window/geometry", saveGeometry());
  settings.setValue("main_window/panel",
    QString::fromStdString(m_toolbar->get_panel()));
}

void MainWindow::update_recent_files() {
  for(auto i = 0; i != MAXIMUM_RECENT_FILES; ++i) {
    m_actions.remove(get_recent_name(i));
  }
  m_actions.remove("file.recent.clear");
  auto& paths = m_recent_files.get_paths();
  for(auto i = 0; i != static_cast<int>(paths.size()); ++i) {
    auto path = paths[i];
    auto name = get_recent_name(i);
    auto text = QFileInfo(path).fileName().replace("&", "&&");
    auto& action = m_actions.add(ActionDefinition(
      name, text, QKeySequence(), "file.recent", "files"));
    action.setToolTip(path);
    action.setStatusTip(path);
    m_actions.set_handler(name, [this, path] { open(path); });
  }
  m_actions.add(ActionDefinition("file.recent.clear", "Clear Menu",
    QKeySequence(), "file.recent", "clear"));
  m_actions.set_handler("file.recent.clear", [this] {
    m_recent_files.clear();
    update_recent_files();
    save_settings();
  });
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
  m_assets->set_document(&m_session->get_document());
  m_inspector->set_editor(&editor);
  m_edit_actions->set_editor(&editor);
  m_history_subscription = editor.get_history().subscribe([this] {
    update_title();
  });
  m_selection_subscription = editor.get_selection().subscribe([this] {
    update_selection_actions();
  });
  if(auto tools = m_view->get_tools()) {
    m_tool_subscription = tools->subscribe_tool([this] {
      update_tool();
    });
  }
  update_title();
  update_tool();
  update_selection_actions();
  update_zoom();
  m_view->setFocus();
}

void MainWindow::detach() {
  m_tool_subscription.release();
  m_history_subscription.release();
  m_selection_subscription.release();
  m_edit_actions->set_editor(nullptr);
  m_inspector->set_editor(nullptr);
  m_layers->set_editor(nullptr);
  m_assets->set_document(nullptr);
  if(m_view) {
    m_view->set_editor(nullptr);
  }
}

void MainWindow::update_title() {
  auto has_session = static_cast<bool>(m_session);
  if(has_session) {
    auto name = m_session->get_name();
    setWindowTitle(QString("%1 - Rellu").arg(name));
    m_app_bar->set_title(name, m_session->is_dirty());
    m_actions.get("window.document").setText(QString(name).replace("&",
      "&&"));
  } else {
    setWindowTitle("Rellu");
    m_app_bar->set_title(QString(), false);
    m_actions.get("window.document").setText("Untitled");
  }
  m_actions.set_enabled("file.save", has_session &&
    (m_session->is_dirty() || m_session->get_path().isEmpty()));
  m_actions.set_enabled("file.save_as", has_session);
  m_actions.set_enabled("file.save_as_local", has_session);
  m_actions.set_enabled("window.document", has_session);
}

void MainWindow::update_selection_actions() {
  auto has_selection = m_session &&
    !m_session->get_editor().get_selection().is_empty();
  m_actions.set_enabled("view.zoom_selection", has_selection);
  m_actions.set_enabled("edit.rename", has_selection);
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

void MainWindow::zoom_to_selection() {
  if(!m_session) {
    return;
  }
  auto& nodes = m_session->get_editor().get_selection().get_nodes();
  if(auto bounds = get_world_bounds(m_session->get_document(), nodes,
      BoundsType::GEOMETRY, m_measurer)) {
    m_view->zoom_to(*bounds);
  }
}

double MainWindow::get_zoom() const {
  return m_view->get_renderer().get_view().m_a;
}

void MainWindow::update_zoom() {
  m_app_bar->set_zoom(get_zoom());
}

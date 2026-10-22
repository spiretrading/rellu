#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDir>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QToolButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include "Rellu/Commands/AlignCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Rendering/RenderPlan.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Tools/ViewFit.hpp"
#include "Rellu/Ui/CanvasFrame.hpp"
#include "Rellu/Ui/ComponentPropertyEditors.hpp"
#include "Rellu/Ui/ContextMenus.hpp"
#include "Rellu/Ui/DocumentSettings.hpp"
#include "Rellu/Ui/LastProperties.hpp"
#include "Rellu/Ui/LayoutProperties.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "Rellu/Ui/TextPropertyEditors.hpp"
#include "Rellu/Ui/Theme.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "MainWindow.hpp"
#include "NativeFrame.hpp"

using namespace Rellu;

namespace {
  constexpr auto ZOOM_STEP = 2.0;
  constexpr auto MAXIMUM_RECENT_FILES = 15;
  constexpr auto LEFT_PANEL_WIDTH = 407;
  constexpr auto MENU_ROW_HEIGHT = 32;
  constexpr auto CAPTION_BUTTON_WIDTH = 46;
  constexpr auto HIT_TOLERANCE = 3.0;
  constexpr auto HIGHLIGHT_WIDTH = 2.0;
  constexpr auto MESSAGE_MARGIN = 48;
  const auto HIGHLIGHT_COLOR = QColor(0x00, 0x90, 0xE9);
  const auto FILE_FILTER = QString("Adobe XD Files (*.xd);;All Files (*)");
  const auto PASTEBOARD = QColor(0xE4, 0xE4, 0xE4);
  const auto DEFAULT_PANEL = QString("layers");
  const auto LANGUAGE_PREFIX = std::string("edit.preferences.language.");
  const auto LANGUAGE_KEY = QString("preferences/language");
  const auto LAST_PROPERTIES_KEY = QString("last_text_properties");
  const auto GROUP_DELETION_KEY =
    QString("assets/delete_nested_group_contents_dismissed");
  const auto GUIDE_SETTINGS_KEY = QString("guides/documents");
  const auto GRID_SETTINGS_KEY = QString("grids/documents");

  std::int64_t get_time() {
    return QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
  }

  std::string get_recent_name(int index) {
    return "file.recent." + std::to_string(index);
  }

  std::string get_nudge_label(int key) {
    if(key == Qt::Key_Left) {
      return "Nudge Left";
    } else if(key == Qt::Key_Right) {
      return "Nudge Right";
    } else if(key == Qt::Key_Up) {
      return "Nudge Up";
    } else if(key == Qt::Key_Down) {
      return "Nudge Down";
    }
    return std::string();
  }

  std::optional<Point> get_nudge_direction(int key) {
    if(key == Qt::Key_Left) {
      return Point(-1, 0);
    } else if(key == Qt::Key_Right) {
      return Point(1, 0);
    } else if(key == Qt::Key_Up) {
      return Point(0, -1);
    } else if(key == Qt::Key_Down) {
      return Point(0, 1);
    }
    return std::nullopt;
  }

  std::string get_panel_action(const std::string& panel) {
    if(panel == "assets") {
      return "view.libraries";
    } else if(panel == "layers") {
      return "view.layers";
    }
    return "view.plugins";
  }

  enum class CaptionKind : unsigned char {
    MINIMIZE,
    MAXIMIZE,
    CLOSE
  };

  class CaptionButton : public QAbstractButton {
    public:
      CaptionButton(CaptionKind kind, const QString& tip)
          : m_kind(kind),
            m_is_hovered(false) {
        setObjectName("caption_button");
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(CAPTION_BUTTON_WIDTH, MENU_ROW_HEIGHT);
        setToolTip(translate("window", tip));
        setAccessibleName(toolTip());
        setAttribute(Qt::WA_Hover);
      }

    protected:
      void enterEvent(QEnterEvent* event) override {
        m_is_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
      }

      void leaveEvent(QEvent* event) override {
        m_is_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
      }

      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        auto ink = QColor(0x3F, 0x3F, 0x3F);
        if(m_kind == CaptionKind::CLOSE && (m_is_hovered || isDown())) {
          painter.fillRect(rect(), isDown() ? QColor(0xF1, 0x70, 0x7A) :
            QColor(0xE8, 0x11, 0x23));
          ink = Qt::white;
        } else if(isDown()) {
          painter.fillRect(rect(), QColor(0xC6, 0xC6, 0xC6));
        } else if(m_is_hovered) {
          painter.fillRect(rect(), QColor(0xD6, 0xD6, 0xD6));
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(ink, 1));
        painter.setBrush(Qt::NoBrush);
        auto center = QPointF(width() / 2.0, height() / 2.0);
        if(m_kind == CaptionKind::MINIMIZE) {
          painter.drawLine(QPointF(center.x() - 5, center.y() + 0.5),
            QPointF(center.x() + 5, center.y() + 0.5));
        } else if(m_kind == CaptionKind::MAXIMIZE) {
          if(window()->isMaximized()) {
            painter.drawRect(QRectF(center.x() - 5, center.y() - 3, 8, 8));
            painter.drawPolyline(QList<QPointF>({
              QPointF(center.x() - 3, center.y() - 5),
              QPointF(center.x() + 5, center.y() - 5),
              QPointF(center.x() + 5, center.y() + 3)}));
          } else {
            painter.drawRect(QRectF(center.x() - 5, center.y() - 5, 10, 10));
          }
        } else {
          painter.drawLine(QPointF(center.x() - 5, center.y() - 5),
            QPointF(center.x() + 5, center.y() + 5));
          painter.drawLine(QPointF(center.x() + 5, center.y() - 5),
            QPointF(center.x() - 5, center.y() + 5));
        }
      }

    private:
      CaptionKind m_kind;
      bool m_is_hovered;
  };

  class HighlightOverlay : public QWidget {
    public:
      std::function<std::vector<QPolygonF> ()> m_outlines;

      explicit HighlightOverlay(QWidget* parent)
          : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        if(!m_outlines) {
          return;
        }
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(HIGHLIGHT_COLOR, 2 * HIGHLIGHT_WIDTH));
        painter.setBrush(Qt::NoBrush);
        for(auto& outline : m_outlines()) {
          auto path = QPainterPath();
          path.addPolygon(outline);
          path.closeSubpath();
          painter.save();
          painter.setClipPath(path);
          painter.drawPath(path);
          painter.restore();
        }
      }
  };

  class PaintOverlay : public QWidget {
    public:
      std::function<void (QPainter&)> m_paint;

      explicit PaintOverlay(QWidget* parent)
          : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        if(!m_paint) {
          return;
        }
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_paint(painter);
      }
  };

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

Language Rellu::load_language() {
  auto settings = QSettings();
  return parse_language(settings.value(LANGUAGE_KEY).toString().
    toStdString()).value_or(Language::ENGLISH);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_view(new DocumentView()),
      m_menu_row(nullptr),
      m_menu_bar(nullptr),
      m_maximize(nullptr),
      m_actions(*this),
      m_app_bar(new AppBar()),
      m_toolbar(new Toolbar(make_xd_tools(), make_xd_panels())),
      m_left_panel(new QStackedWidget()),
      m_assets(new AssetsPanel()),
      m_layers(new LayersPanel()),
      m_plugins(new PluginsPanel()),
      m_inspector(new PropertyInspector()),
      m_prototype_inspector(new PrototypeInspector()),
      m_presets(new ArtboardPresetsPanel(m_inspector)),
      m_preview(nullptr),
      m_inspector_frame(new QWidget()),
      m_recent_files(MAXIMUM_RECENT_FILES),
      m_measurer(make_text_measurer(get_default_font_library())),
      m_highlight(nullptr),
      m_gradient_overlay(nullptr),
      m_is_launching(false),
      m_is_restored_expanded(false) {
  add_standard_commands(m_commands, m_measurer);
  add_align_commands(m_commands, m_measurer);
  add_path_commands(
    m_commands, make_text_outliner(get_default_font_library()));
  add_repeat_grid_commands(m_commands, m_measurer);
  add_component_commands(m_commands, m_measurer);
  add_header_commands(m_commands, m_measurer);
  add_asset_commands(m_commands);
  m_view->get_renderer().set_background(PASTEBOARD);
  m_view->installEventFilter(this);
  auto highlight = new HighlightOverlay(m_view);
  highlight->m_outlines = [this] {
    return get_highlight_outlines();
  };
  m_highlight = highlight;
  auto gradient_overlay = new PaintOverlay(m_view);
  gradient_overlay->m_paint = [this] (QPainter& painter) {
    paint_gradient_editor(painter);
  };
  m_gradient_overlay = gradient_overlay;
  m_inspector->get_color_picker().installEventFilter(this);
  m_inspector->get_color_picker().set_canvas_handler(
    [this] (auto event, auto position) {
      return handle_gradient_event(event, position);
    });
  m_inspector->get_color_picker().set_key_handler([this] (const auto& event) {
    return handle_gradient_key(event);
  });
  m_inspector->get_color_picker().set_model_handler([this] (auto model) {
    m_assets->get_color_picker().set_model(model);
  });
  m_assets->get_color_picker().set_model_handler([this] (auto model) {
    m_inspector->get_color_picker().set_model(model);
  });
  m_inspector->set_measurer(m_measurer);
  auto editors = make_standard_property_editors(m_measurer);
  add_text_property_editors(editors, get_default_font_library(), m_measurer,
    [this] () -> TextSelection* {
      if(!m_session) {
        return nullptr;
      }
      return &m_session->get_editor().get_text_selection();
    });
  add_layout_property_editors(editors, m_measurer);
  add_component_property_editors(editors);
  m_inspector->set_property_editors(std::move(editors));
  m_inspector->set_actions(&m_actions);
  m_layers->set_actions(&m_actions);
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
  m_app_bar->set_release_handler([this] {
    m_view->setFocus();
  });
  auto release = [this] {
    if(m_view) {
      m_view->setFocus();
    }
  };
  m_inspector->set_release_handler(release);
  m_prototype_inspector->set_release_handler(release);
  auto bar = make_layout();
  add_xd_menus(m_actions);
  add_actions();
  add_component_actions();
  m_text_actions =
    std::make_unique<TextActions>(m_actions, get_default_font_library());
  add_asset_actions();
  m_guide_actions = std::make_unique<GuideActions>(m_actions);
  m_guide_actions->set_settings_handler([this] (const auto& settings) {
    if(auto tools = m_view->get_tools()) {
      tools->get_context().set_guide_settings(settings);
      m_view->update();
    }
    store_guide_settings();
  });
  m_grid_actions = std::make_unique<GridActions>(m_actions);
  m_grid_actions->set_settings_handler([this] (const auto& settings) {
    if(auto tools = m_view->get_tools()) {
      tools->get_context().set_grid_settings(settings);
      m_view->update();
    }
    store_grid_settings();
  });
  m_inspector->set_grid_actions(m_grid_actions.get());
  m_edit_actions->set_paste_handler([this] (const auto& data, auto is_applied) {
    return m_guide_actions->paste(data, is_applied);
  });
  m_actions.build(*bar);
  restore_settings();
  update_title();
  update_selection_actions();
  update_zoom();
  update_document_state();
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

bool MainWindow::revert() {
  if(!m_session || m_session->get_path().isEmpty()) {
    return false;
  }
  auto path = m_session->get_path();
  auto box = QMessageBox(QMessageBox::Warning, "Rellu", translate("dialog",
    "Do you want to revert to the most recently saved version of the "
    "document \"%1\"?").arg(QFileInfo(path).fileName()),
    QMessageBox::NoButton, this);
  box.setInformativeText(
    translate("dialog", "Your current changes will be lost."));
  auto revert =
    box.addButton(translate("dialog", "Revert"), QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Cancel)->setText(translate("dialog", "Cancel"));
  box.setDefaultButton(revert);
  box.exec();
  if(box.clickedButton() != revert) {
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
    return false;
  }
  install(std::move(document), path);
  QApplication::restoreOverrideCursor();
  return true;
}

bool MainWindow::make_new() {
  if(!confirm_close()) {
    return false;
  }
  install(make_new_document(), QString());
  auto& canvas = m_session->get_document().get_canvas();
  if(!canvas.empty()) {
    m_session->get_editor().get_selection().select(canvas.front());
  }
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
  store_properties();
  save_settings();
  QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if(event->type() == QEvent::WindowStateChange) {
    update_launch();
  }
  if(event->type() == QEvent::WindowStateChange && m_maximize) {
    auto tip = translate("window",
      isMaximized() ? QString("Restore Down") : QString("Maximize"));
    m_maximize->setToolTip(tip);
    m_maximize->setAccessibleName(tip);
    m_maximize->update();
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if(m_view && watched == m_view) {
    if(event->type() == QEvent::Resize || event->type() == QEvent::Show) {
      update_launch();
    } else if(event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::Wheel || event->type() == QEvent::KeyPress) {
      finish_launch();
    }
    if(event->type() == QEvent::Resize) {
      m_highlight->setGeometry(m_view->rect());
      m_gradient_overlay->setGeometry(m_view->rect());
    } else if(event->type() == QEvent::MouseButtonPress) {
      set_highlights({});
    }
    if(event->type() == QEvent::Paint) {
      update_zoom();
      if(m_highlight->isVisible()) {
        m_highlight->update();
      }
      update_gradient_editor();
    } else if(event->type() == QEvent::ContextMenu && m_session) {
      auto context = static_cast<QContextMenuEvent*>(event);
      if(!select_for_context_menu(context->pos())) {
        return true;
      }
      auto menu = make_context_menu(m_actions, ContextMenuKind::CANVAS, this);
      menu->setAttribute(Qt::WA_DeleteOnClose);
      if(menu->isEmpty()) {
        menu->deleteLater();
      } else {
        menu->popup(context->globalPos());
      }
      return true;
    }
  }
  if(watched == &m_inspector->get_color_picker() &&
      (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
    m_gradient_handles.reset();
    QTimer::singleShot(0, this, [this] {
      update_gradient_editor();
    });
  }
  return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(
    const QByteArray& type, void* message, qintptr* result) {
  if(m_restored_bounds && is_showing(message)) {
    set_normal_bounds(*this, *m_restored_bounds);
    m_restored_bounds.reset();
  }
  if(m_maximize && handle_frame_event(*this, message, result,
      [this] (QPoint position) {
        return is_caption(position);
      }, MENU_ROW_HEIGHT, m_maximize)) {
    return true;
  }
  return QMainWindow::nativeEvent(type, message, result);
}

bool MainWindow::select_for_context_menu(QPoint position) {
  auto tools = m_view->get_tools();
  auto inverse = invert(m_view->get_renderer().get_view());
  if(!tools || !inverse) {
    return true;
  }
  auto& editor = m_session->get_editor();
  auto& document = m_session->get_document();
  auto& selection = editor.get_selection();
  auto location = Point(position.x(), position.y());
  auto target = tools->get_context().get_titles().find(location);
  auto background = NodeId();
  if(target.is_nil()) {
    auto hit = tools->get_hit_tester().hit(
      inverse->map(location), HIT_TOLERANCE / get_zoom(), true);
    if(!hit.is_nil() && document.get(hit).get_artboard()) {
      background = hit;
    } else if(!hit.is_nil()) {
      auto& context = editor.get_context();
      while(!context.get_container().is_nil() &&
          !is_ancestor(document, context.get_container(), hit)) {
        context.exit();
      }
      target = context.resolve(hit);
    }
  }
  if(!target.is_nil()) {
    if(!selection.contains(target)) {
      selection.select(target);
    }
    return true;
  }
  selection.clear();
  return background.is_nil();
}

bool MainWindow::is_caption(QPoint position) const {
  if(!m_menu_row || !m_menu_row->geometry().contains(position)) {
    return false;
  }
  auto local = m_menu_row->mapFrom(this, position);
  auto child = m_menu_row->childAt(local);
  if(!child) {
    return true;
  } else if(child == m_menu_bar) {
    return !m_menu_bar->actionAt(m_menu_bar->mapFrom(m_menu_row, local));
  }
  return static_cast<bool>(qobject_cast<QLabel*>(child));
}

std::optional<MainWindow::GradientTarget>
    MainWindow::find_gradient_target() const {
  auto& picker = m_inspector->get_color_picker();
  if(!m_session || !picker.isVisible() ||
      m_inspector->get_picked_path() != "fill.paint" ||
      m_inspector->get_nodes().size() != 1) {
    return std::nullopt;
  }
  auto gradient = std::get_if<GradientFill>(&picker.get_fill());
  if(!gradient || !GradientHandles::is_editable(*gradient)) {
    return std::nullopt;
  }
  auto& document = m_session->get_document();
  auto& id = m_inspector->get_nodes().front();
  auto node = document.find(id);
  if(!node || (!node->get_shape() && !node->get_text())) {
    return std::nullopt;
  }
  auto bounds =
    get_local_bounds(document, id, BoundsType::GEOMETRY, m_measurer);
  if(!bounds || !(bounds->m_width > 0) || !(bounds->m_height > 0)) {
    return std::nullopt;
  }
  auto to_view =
    m_view->get_renderer().get_view() * get_world_transform(document, id);
  if(!invert(to_view)) {
    return std::nullopt;
  }
  return GradientTarget(*gradient, GradientFrame(*bounds, to_view));
}

void MainWindow::paint_gradient_editor(QPainter& painter) {
  if(auto target = find_gradient_target()) {
    m_gradient_handles.paint(painter, target->m_fill, target->m_frame,
      m_inspector->get_color_picker().get_stop());
  }
}

bool MainWindow::handle_gradient_event(
    ColorPicker::CanvasEvent event, QPointF position) {
  if(event == ColorPicker::CanvasEvent::RELEASE) {
    m_gradient_handles.release();
    return true;
  }
  auto target = find_gradient_target();
  if(!target) {
    return false;
  }
  auto local = QPointF(m_view->mapFromGlobal(position.toPoint()));
  auto point = Point(local.x(), local.y());
  auto edit = GradientEdit();
  if(event == ColorPicker::CanvasEvent::PRESS) {
    edit = m_gradient_handles.press(target->m_fill, target->m_frame, point);
  } else {
    edit = m_gradient_handles.move(target->m_fill, target->m_frame, point,
      QGuiApplication::keyboardModifiers());
    edit.m_is_handled = true;
  }
  auto& picker = m_inspector->get_color_picker();
  if(edit.m_fill) {
    picker.apply_fill(*edit.m_fill);
  }
  if(edit.m_stop) {
    picker.set_stop(*edit.m_stop);
  }
  m_gradient_overlay->update();
  return edit.m_is_handled;
}

bool MainWindow::handle_gradient_key(const QKeyEvent& event) {
  auto target = find_gradient_target();
  if(!target) {
    return false;
  }
  auto& picker = m_inspector->get_color_picker();
  auto& editor = m_session->get_editor();
  auto direction = get_nudge_direction(event.key());
  auto edit = GradientEdit();
  if(direction && event.modifiers().testFlag(Qt::AltModifier)) {
    return true;
  } else if(direction) {
    edit = GradientHandles::nudge(target->m_fill, target->m_frame.m_bounds,
      get_zoom(), picker.get_stop(), get_gradient_nudge(*direction,
        event.modifiers().testFlag(Qt::ShiftModifier)));
  } else {
    edit = GradientHandles::press_key(target->m_fill,
      target->m_frame.m_bounds, get_zoom(), picker.get_stop(), event.key(),
      event.modifiers());
  }
  if(!edit.m_is_handled) {
    return false;
  }
  if(auto label = get_nudge_label(event.key()); !label.empty()) {
    if(edit.m_fill) {
      if(!editor.get_history().is_dirty()) {
        editor.get_history().seal();
      }
      editor.merge(std::make_unique<SetFillCommand>(
        m_inspector->get_nodes(), Fill(*edit.m_fill), std::move(label)));
      picker.set_fill(*edit.m_fill);
    }
  } else if(edit.m_fill) {
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(std::make_unique<SetFillCommand>(
      m_inspector->get_nodes(), Fill(*edit.m_fill)));
    editor.get_history().seal();
    editor.execute(
      std::make_unique<CompositeCommand>("Delete", std::move(commands)));
    editor.get_history().seal();
    picker.set_fill(*edit.m_fill);
  }
  if(edit.m_stop) {
    picker.set_stop(*edit.m_stop);
  }
  m_gradient_overlay->update();
  return true;
}

Point MainWindow::get_gradient_nudge(Point direction, bool is_large) const {
  auto& document = m_session->get_document();
  auto& editor = m_session->get_editor();
  auto& nodes = m_inspector->get_nodes();
  auto delta = direction;
  if(is_large) {
    auto artboard = editor.get_context().get_artboard();
    auto nudged = std::vector<NodeId>();
    for(auto& id : nodes) {
      if(find_artboard(document, id) == artboard) {
        nudged.push_back(id);
      }
    }
    delta = direction * 10;
    if(auto bounds = get_world_bounds(
        document, nudged, BoundsType::GEOMETRY, m_measurer)) {
      delta =
        get_large_nudge(document, artboard, bounds->get_top_left(), direction);
    }
  }
  if(auto bounds =
      get_world_bounds(document, nodes, BoundsType::GEOMETRY, m_measurer)) {
    delta = keep_on_canvas(*bounds, delta);
  }
  return delta;
}

void MainWindow::update_gradient_editor() {
  m_gradient_overlay->setGeometry(m_view->rect());
  m_gradient_overlay->setVisible(static_cast<bool>(find_gradient_target()));
  m_gradient_overlay->raise();
  m_gradient_overlay->update();
}

void MainWindow::set_highlights(std::vector<NodeId> nodes) {
  m_highlighted = std::move(nodes);
  m_highlight->setGeometry(m_view->rect());
  m_highlight->setVisible(!m_highlighted.empty());
  m_highlight->raise();
  m_highlight->update();
}

std::vector<QPolygonF> MainWindow::get_highlight_outlines() const {
  auto outlines = std::vector<QPolygonF>();
  if(!m_session) {
    return outlines;
  }
  auto& document = m_session->get_document();
  auto view = m_view->get_renderer().get_view();
  for(auto& id : m_highlighted) {
    if(!document.find(id)) {
      continue;
    }
    auto bounds =
      get_local_bounds(document, id, BoundsType::GEOMETRY, m_measurer);
    if(!bounds) {
      continue;
    }
    auto world = get_world_transform(document, id);
    auto outline = QPolygonF();
    for(auto corner : {bounds->get_top_left(),
        Point(bounds->m_x + bounds->m_width, bounds->m_y),
        Point(bounds->m_x + bounds->m_width, bounds->m_y + bounds->m_height),
        Point(bounds->m_x, bounds->m_y + bounds->m_height)}) {
      auto point = view.map(world.map(corner));
      outline.push_back(QPointF(point.m_x, point.m_y));
    }
    outlines.push_back(std::move(outline));
  }
  return outlines;
}

void MainWindow::add_asset_actions() {
  m_assets->set_highlight_handler([this] (const auto& nodes) {
    set_highlights(nodes);
  });
  m_assets->set_message_handler([this] (const auto& message) {
    show_message(message);
  });
  m_assets->set_font_styles([] (const QString& family) {
    auto styles = QStringList();
    for(auto& face : get_default_font_library().get_faces()) {
      if(QString::fromStdString(face.m_names.m_typographic_family) ==
          family) {
        auto style = QString::fromStdString(face.m_names.m_typographic_style);
        if(!style.isEmpty() && !styles.contains(style)) {
          styles.push_back(style);
        }
      }
    }
    return styles;
  });
  m_assets->set_font_families([] {
    auto families = QStringList();
    for(auto& face : get_default_font_library().get_faces()) {
      auto family =
        QString::fromStdString(face.m_names.m_typographic_family);
      if(!family.isEmpty() && !families.contains(family)) {
        families.push_back(family);
      }
    }
    families.sort(Qt::CaseInsensitive);
    return families;
  });
  m_actions.set_handler("object.reveal_color", [this] {
    if(!m_session) {
      return;
    }
    auto& document = m_session->get_document();
    auto node = document.find(
      m_session->get_editor().get_selection().get_primary());
    auto style = node ? (node->get_shape() ? &node->get_shape()->m_style :
      node->get_text() ? &node->get_text()->m_style : nullptr) : nullptr;
    if(!style) {
      return;
    }
    if(auto swatch = find_swatch(document.get_resources(), style->m_fill)) {
      show_panel("assets");
      m_assets->reveal_swatch(swatch->m_id);
    }
  });
  m_actions.set_handler("object.reveal_character_style", [this] {
    if(!m_session) {
      return;
    }
    auto& document = m_session->get_document();
    auto node = document.find(
      m_session->get_editor().get_selection().get_primary());
    auto text = node ? node->get_text() : nullptr;
    if(!text) {
      return;
    }
    if(auto style = find_character_style(document.get_resources(), *text)) {
      show_panel("assets");
      m_assets->reveal_character_style(style->m_id);
    }
  });
}

void MainWindow::update_asset_actions() {
  auto is_color = false;
  auto is_style = false;
  if(m_session) {
    auto& document = m_session->get_document();
    if(auto node = document.find(
        m_session->get_editor().get_selection().get_primary())) {
      auto& resources = document.get_resources();
      if(auto shape = node->get_shape()) {
        is_color =
          static_cast<bool>(find_swatch(resources, shape->m_style.m_fill));
      } else if(auto text = node->get_text()) {
        is_color =
          static_cast<bool>(find_swatch(resources, text->m_style.m_fill));
        is_style =
          static_cast<bool>(find_character_style(resources, *text));
      }
    }
  }
  m_actions.set_enabled("object.reveal_color", is_color);
  m_actions.set_enabled("object.reveal_character_style", is_style);
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
  menu_layout->addWidget(bar);
  menu_layout->addStretch(1);
  m_menu_row = menu_row;
  m_menu_bar = bar;
  if(is_custom_frame_supported()) {
    menu_layout->setSpacing(0);
    menu_layout->insertSpacing(1, 14);
    auto minimize = new CaptionButton(CaptionKind::MINIMIZE, "Minimize");
    connect(minimize, &QAbstractButton::clicked, this, [this] {
      showMinimized();
    });
    auto maximize = new CaptionButton(CaptionKind::MAXIMIZE, "Maximize");
    connect(maximize, &QAbstractButton::clicked, this, [this] {
      if(isMaximized()) {
        showNormal();
      } else {
        showMaximized();
      }
    });
    auto close = new CaptionButton(CaptionKind::CLOSE, "Close");
    connect(close, &QAbstractButton::clicked, this, [this] {
      this->close();
    });
    menu_layout->addWidget(minimize);
    menu_layout->addWidget(maximize);
    menu_layout->addWidget(close);
    m_maximize = maximize;
    enable_custom_frame(*this);
  }
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
  body_layout->addWidget(new CanvasFrame(*m_view), 1);
  m_inspector_frame->setObjectName("inspector_frame");
  m_inspector_frame->setAttribute(Qt::WA_StyledBackground);
  m_inspector_frame->setFixedWidth(PropertyInspector::WIDTH);
  auto inspector_layout = new QVBoxLayout(m_inspector_frame);
  inspector_layout->setContentsMargins(1, 0, 0, 0);
  inspector_layout->addWidget(m_inspector);
  inspector_layout->addWidget(m_prototype_inspector);
  m_prototype_inspector->hide();
  m_prototype_inspector->set_fixed_position_factory(
    [] (const NodeId& node, bool is_fixed) {
      return std::make_unique<SetFixedPositionCommand>(
        std::vector<NodeId>({node}), is_fixed);
    });
  body_layout->addWidget(m_inspector_frame);
  layout->addWidget(body, 1);
  setCentralWidget(central);
  m_banners = std::make_unique<BannerHost>(*central);
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
  m_actions.set_handler("file.revert", [this] { revert(); });
  m_edit_actions = std::make_unique<EditActions>(m_actions, m_commands);
  m_edit_actions->set_text_measurer(m_measurer);
  m_edit_actions->set_source_provider([this] {
    auto source = XdClipboardSource();
    if(m_session) {
      source.m_id = m_session->get_instance();
      source.m_path =
        QDir::toNativeSeparators(m_session->get_path()).toStdString();
    }
    return source;
  });
  auto view_provider = [this] () -> std::optional<Rect> {
    auto inverse = invert(m_view->get_renderer().get_view());
    if(!inverse || m_view->width() <= 0 || m_view->height() <= 0) {
      return std::nullopt;
    }
    return inverse->map(Rect(0, 0, m_view->width(), m_view->height()));
  };
  m_edit_actions->set_view_provider(view_provider);
  m_presets->set_view_provider(view_provider);
  m_presets->hide();
  m_edit_actions->set_reveal_handler([this] (const auto& region) {
    show_region(region);
  });
  m_presets->set_reveal_handler([this] (const auto& region) {
    reveal_artboard(region);
  });
  m_edit_actions->set_error_handler([this] (const auto& message) {
    QMessageBox::warning(this, "Rellu", message);
  });
  m_edit_actions->set_notice_handler([this] (const auto& message) {
    m_banners->show(message);
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
    "window.document", translate("Untitled"), QKeySequence(), "window",
    "documents", true));
  document.setChecked(true);
  m_actions.set_handler("window.document", [this] {
    m_actions.get("window.document").setChecked(true);
    activateWindow();
  });
  m_actions.set_handler("window.preview", [this] { toggle_preview(); });
  for(auto language : LANGUAGES) {
    m_actions.set_handler(LANGUAGE_PREFIX + std::string(get_code(language)),
      [this, language] {
        choose_language(language);
      });
  }
  for(auto button : m_app_bar->findChildren<QToolButton*>()) {
    if(button->accessibleName() == translate("app_bar", "Desktop Preview")) {
      button->setEnabled(true);
      connect(button, &QToolButton::clicked, this, [this] {
        m_actions.get("window.preview").trigger();
      });
    }
  }
  m_actions.set_handler("help.about", [this] {
    QMessageBox::about(this, translate("menu", "About Rellu"), "Rellu\n\n"
      "An editor for Adobe XD documents.");
  });
  add_tool_actions();
}

void MainWindow::add_component_actions() {
  m_component_actions = std::make_unique<ComponentActions>(m_actions);
  m_component_actions->set_reveal_handler([this] (const auto& id) {
    reveal(id);
  });
  m_component_actions->set_component_handler([this] (const auto& id) {
    show_panel("assets");
    m_assets->reveal(id);
  });
  m_assets->set_thumbnailer([this] (const auto& document,
      const auto& component, int size) {
    return m_thumbnails.render(document, component, size);
  });
  m_assets->set_main_component_handler([this] (const auto& component) {
    m_component_actions->edit_main_component(component);
  });
  m_assets->set_group_deletion_confirmed(
    !QSettings().value(GROUP_DELETION_KEY, false).toBool());
  m_assets->set_confirmation_handler([this] (const QString& title,
      const QString& message, const QString& ok, const QString& cancel,
      const QString& dismissal) {
    auto box = QMessageBox(QMessageBox::Warning, title, message,
      QMessageBox::NoButton, this);
    auto accept = box.addButton(ok, QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel)->setText(cancel);
    box.setDefaultButton(accept);
    auto check = static_cast<QCheckBox*>(nullptr);
    if(!dismissal.isEmpty()) {
      check = new QCheckBox(dismissal);
      box.setCheckBox(check);
    }
    box.exec();
    auto answer = AssetConfirmation();
    answer.m_is_accepted = box.clickedButton() == accept;
    answer.m_is_dismissed =
      answer.m_is_accepted && check && check->isChecked();
    if(answer.m_is_dismissed) {
      QSettings().setValue(GROUP_DELETION_KEY, true);
    }
    return answer;
  });
  m_component_drop = std::make_unique<ComponentDrop>(*m_view);
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
  auto tool = name;
  if(tool == "select" && m_app_bar->get_workspace() == Workspace::PROTOTYPE) {
    tool = "prototype";
  }
  if(auto tools = m_view->get_tools(); tools && tools->has_tool(tool)) {
    tools->set_tool(tool);
  }
  update_tool();
}

void MainWindow::update_tool() {
  if(auto tools = m_view->get_tools()) {
    if(auto& name = tools->get_tool_name();
        name == "prototype" || name == "path_edit") {
      m_toolbar->set_tool("select");
    } else {
      m_toolbar->set_tool(tools->get_tool_name());
    }
  }
  update_presets();
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
  } else if(auto tools = m_view->get_tools();
      tools && tools->get_tool_name() == "prototype") {
    select_tool("select");
  }
  update_document_actions(m_actions, workspace, static_cast<bool>(m_session));
  m_inspector->set_workspace(workspace);
  if(m_session) {
    m_session->get_editor().get_context().set_prototyping(
      workspace == Workspace::PROTOTYPE);
  }
  m_edit_actions->set_workspace(workspace);
  m_guide_actions->set_workspace(workspace);
  m_grid_actions->set_workspace(workspace);
  update_presets();
  m_inspector->setVisible(workspace != Workspace::PROTOTYPE);
  m_prototype_inspector->setVisible(workspace == Workspace::PROTOTYPE);
  show_panel(m_toolbar->get_panel());
}

void MainWindow::toggle_preview() {
  auto& action = m_actions.get("window.preview");
  if(m_preview && m_preview->isVisible()) {
    m_preview->close();
    action.setChecked(false);
    return;
  }
  auto start = NodeId();
  if(m_session) {
    start = find_preview_start(m_session->get_document(),
      m_session->get_editor().get_selection().get_nodes());
  }
  if(start.is_nil()) {
    action.setChecked(false);
    return;
  }
  if(!m_preview) {
    m_preview = new PreviewWindow(this);
    m_preview->addAction(&action);
    m_preview->set_close_handler([this] {
      m_actions.get("window.preview").setChecked(false);
    });
  }
  m_preview->setWindowTitle(
    translate("preview", "Preview : ") + m_session->get_name());
  m_preview->play(m_session->get_document(), start);
  m_preview->show();
  m_preview->raise();
  m_preview->activateWindow();
  action.setChecked(true);
}

void MainWindow::restore_settings() {
  auto settings = QSettings();
  m_recent_files.set_paths(settings.value("recent_files").toStringList());
  update_recent_files();
  if(!restoreGeometry(settings.value("main_window/geometry").toByteArray())) {
    if(auto screen = this->screen()) {
      auto area = screen->availableGeometry();
      auto geometry = center_window(Size(1600, 960),
        Rect(area.x(), area.y(), area.width(), area.height()));
      setGeometry(static_cast<int>(geometry.m_x),
        static_cast<int>(geometry.m_y), static_cast<int>(geometry.m_width),
        static_cast<int>(geometry.m_height));
    } else {
      resize(1600, 960);
    }
  } else if(auto saved = settings.value("main_window/bounds").toRect();
      saved.isValid()) {
    if(auto area = get_work_area(saved); area.isValid()) {
      auto bounds = fit_window(
        Rect(saved.x(), saved.y(), saved.width(), saved.height()),
        Rect(area.x(), area.y(), area.width(), area.height()));
      auto normal = QRect(static_cast<int>(bounds.m_x),
        static_cast<int>(bounds.m_y), static_cast<int>(bounds.m_width),
        static_cast<int>(bounds.m_height));
      if(windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen)) {
        QTimer::singleShot(0, this, [this, normal] {
          set_normal_bounds(*this, normal);
        });
      } else {
        m_restored_bounds = normal;
      }
    }
  }
  m_is_restored_expanded = static_cast<bool>(
    windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen));
  m_is_launching = true;
  m_view->set_fit_held(true);
  show_panel(settings.value("main_window/panel", DEFAULT_PANEL).toString().
    toStdString());
  check_language(load_language());
}

void MainWindow::update_launch() {
  if(!m_is_launching || !isVisible()) {
    return;
  }
  auto state = windowState();
  auto area = QRect();
  if(auto screen = this->screen()) {
    area = state & Qt::WindowFullScreen ? screen->geometry() :
      screen->availableGeometry();
  }
  auto frame = frameGeometry();
  if(is_settled(m_is_restored_expanded,
      static_cast<bool>(state & (Qt::WindowMaximized | Qt::WindowFullScreen)),
      Size(frame.width(), frame.height()), Size(area.width(), area.height()))) {
    QTimer::singleShot(0, this, [this] {
      finish_launch();
    });
  }
}

void MainWindow::finish_launch() {
  if(m_is_launching) {
    m_is_launching = false;
    m_view->set_fit_held(false);
  }
}

void MainWindow::check_language(Language language) {
  for(auto candidate : LANGUAGES) {
    m_actions.get(LANGUAGE_PREFIX + std::string(get_code(candidate))).
      setChecked(candidate == language);
  }
}

void MainWindow::choose_language(Language language) {
  auto previous = load_language();
  check_language(language);
  if(language == previous) {
    return;
  }
  auto settings = QSettings();
  auto code = get_code(language);
  settings.setValue(LANGUAGE_KEY,
    QString::fromUtf8(code.data(), static_cast<qsizetype>(code.size())));
  auto message = localize("message",
    "You must restart Rellu to apply the language change.", language);
  show_message(QString::fromUtf8(
    message.data(), static_cast<qsizetype>(message.size())));
}

void MainWindow::show_message(const QString& message) {
  QToolTip::showText(m_view->mapToGlobal(QPoint(m_view->width() / 2,
    m_view->height() - MESSAGE_MARGIN)), message, m_view);
}

void MainWindow::restore_properties() {
  m_inspector->get_color_picker().set_model(ColorModel::HEX);
  m_assets->get_color_picker().set_model(ColorModel::HEX);
  if(!m_session || m_session->get_id().empty()) {
    return;
  }
  auto settings = QSettings();
  if(auto properties = find_last_properties(
      settings.value(LAST_PROPERTIES_KEY).toByteArray(),
      m_session->get_id())) {
    apply_properties(*properties);
  }
}

LastProperties MainWindow::get_properties() const {
  auto& selection = m_session->get_editor().get_text_selection();
  auto properties = LastProperties();
  properties.m_text = LastTextProperties(selection.get_last_style(),
    selection.get_last_paragraph_style(), selection.is_last_fill_enabled(),
    selection.get_last_border());
  properties.m_color_model = m_inspector->get_color_picker().get_model();
  return properties;
}

void MainWindow::apply_properties(const LastProperties& properties) {
  auto& selection = m_session->get_editor().get_text_selection();
  if(auto& text = properties.m_text) {
    selection.set_last_style(text->m_style, text->m_paragraph);
    selection.set_last_appearance(text->m_is_fill_enabled, text->m_border);
  }
  m_inspector->get_color_picker().set_model(properties.m_color_model);
  m_assets->get_color_picker().set_model(properties.m_color_model);
}

void MainWindow::store_properties() {
  if(!m_session || m_session->get_id().empty()) {
    return;
  }
  auto settings = QSettings();
  settings.setValue(LAST_PROPERTIES_KEY, record_last_properties(
    settings.value(LAST_PROPERTIES_KEY).toByteArray(), m_session->get_id(),
    get_properties(), QDateTime::currentDateTimeUtc()));
}

void MainWindow::change_properties_id() {
  auto properties = get_properties();
  auto settings = QSettings();
  settings.setValue(LAST_PROPERTIES_KEY, change_last_properties_id(
    settings.value(LAST_PROPERTIES_KEY).toByteArray(), m_session->get_id(),
    properties, QDateTime::currentDateTimeUtc()));
  apply_properties(properties);
}

void MainWindow::load_document_settings(
    GuideSettings guides, GridSettings grids) {
  if(m_session && !m_session->get_id().empty()) {
    auto& id = m_session->get_id();
    auto settings = QSettings();
    auto time = get_time();
    for(auto& key : {GUIDE_SETTINGS_KEY, GRID_SETTINGS_KEY}) {
      settings.setValue(key, purge_document_settings(
        settings.value(key).toByteArray(), id, time));
    }
    if(auto recorded = find_guide_settings(
        settings.value(GUIDE_SETTINGS_KEY).toByteArray(), id)) {
      guides = *recorded;
    }
    if(auto recorded = find_grid_settings(
        settings.value(GRID_SETTINGS_KEY).toByteArray(), id, grids)) {
      grids = *recorded;
    }
  }
  m_guide_actions->set_settings(guides);
  m_grid_actions->set_settings(grids);
  store_guide_settings();
  store_grid_settings();
}

void MainWindow::store_guide_settings() const {
  if(!m_guide_actions || !m_session || m_session->get_id().empty()) {
    return;
  }
  auto settings = QSettings();
  settings.setValue(GUIDE_SETTINGS_KEY, record_guide_settings(
    settings.value(GUIDE_SETTINGS_KEY).toByteArray(), m_session->get_id(),
    m_guide_actions->get_settings(), get_time()));
}

void MainWindow::store_grid_settings() const {
  if(!m_grid_actions || !m_session || m_session->get_id().empty()) {
    return;
  }
  auto settings = QSettings();
  settings.setValue(GRID_SETTINGS_KEY, record_grid_settings(
    settings.value(GRID_SETTINGS_KEY).toByteArray(), m_session->get_id(),
    m_grid_actions->get_settings(), get_time()));
}

void MainWindow::save_settings() const {
  auto settings = QSettings();
  settings.setValue("recent_files", m_recent_files.get_paths());
  settings.setValue("main_window/geometry", saveGeometry());
  if(auto bounds = get_normal_bounds(*this); bounds.isValid()) {
    settings.setValue("main_window/bounds", bounds);
  }
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
  m_actions.add(ActionDefinition("file.recent.clear",
    translate("menu", "Clear Menu"), QKeySequence(), "file.recent", "clear"));
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
  auto id = m_session->get_id();
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
  if(m_session->get_id() == id) {
    store_properties();
  } else {
    change_properties_id();
    load_document_settings(
      m_guide_actions->get_settings(), m_grid_actions->get_settings());
  }
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
  auto box = QMessageBox(QMessageBox::Warning, "Rellu", translate("dialog",
    "Do you want to save the changes made to the document \"%1\"?").arg(
      m_session->get_name()),
    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
  box.setInformativeText(translate("dialog",
    "Your changes will be lost if you don't save them."));
  box.button(QMessageBox::Save)->setText(translate("dialog", "Save"));
  box.button(QMessageBox::Discard)->setText(
    translate("dialog", "Do&n't Save"));
  box.button(QMessageBox::Cancel)->setText(translate("dialog", "Cancel"));
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
  restore_properties();
  auto& editor = m_session->get_editor();
  editor.get_history().set_text_measurer(m_measurer);
  editor.get_context().set_prototyping(
    m_app_bar->get_workspace() == Workspace::PROTOTYPE);
  m_layers->set_editor(&editor);
  m_assets->set_editor(&editor);
  m_inspector->set_editor(&editor);
  m_prototype_inspector->set_editor(&editor);
  m_presets->set_editor(&editor);
  m_edit_actions->set_editor(&editor);
  m_component_actions->set_editor(&editor);
  m_text_actions->set_editor(&editor);
  m_guide_actions->set_editor(&editor);
  m_grid_actions->set_editor(&editor);
  load_document_settings(GuideSettings(), GridSettings());
  m_component_drop->set_editor(&editor);
  m_history_subscription = editor.get_history().subscribe([this] {
    update_title();
    update_asset_actions();
    update_canvas_color();
  });
  m_selection_subscription = editor.get_selection().subscribe([this] {
    set_highlights({});
    update_selection_actions();
  });
  if(auto tools = m_view->get_tools()) {
    tools->get_context().set_text_measurer(m_measurer);
    m_guide_actions->set_settings(m_guide_actions->get_settings());
    m_grid_actions->set_settings(m_grid_actions->get_settings());
    m_tool_subscription = tools->subscribe_tool([this] {
      update_tool();
    });
  }
  update_title();
  update_tool();
  update_selection_actions();
  update_zoom();
  update_document_state();
  update_canvas_color();
  m_view->setFocus();
}

void MainWindow::update_canvas_color() {
  auto color = m_session ? get_canvas_color(m_session->get_document()) :
    PASTEBOARD;
  auto& renderer = m_view->get_renderer();
  if(renderer.get_background() != color) {
    renderer.set_background(color);
    m_view->update();
  }
}

void MainWindow::detach() {
  store_properties();
  set_highlights({});
  m_tool_subscription.release();
  m_history_subscription.release();
  m_selection_subscription.release();
  m_edit_actions->set_editor(nullptr);
  if(m_component_actions) {
    m_component_actions->set_editor(nullptr);
  }
  if(m_text_actions) {
    m_text_actions->set_editor(nullptr);
  }
  if(m_guide_actions) {
    m_guide_actions->set_editor(nullptr);
  }
  if(m_grid_actions) {
    m_grid_actions->set_editor(nullptr);
  }
  if(m_component_drop) {
    m_component_drop->set_editor(nullptr);
  }
  m_inspector->set_editor(nullptr);
  m_prototype_inspector->set_editor(nullptr);
  m_presets->set_editor(nullptr);
  if(m_preview) {
    m_preview->close();
  }
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
    m_actions.get("window.document").setText(translate("Untitled"));
  }
  m_actions.set_enabled("file.save", has_session &&
    (m_session->is_dirty() || m_session->get_path().isEmpty()));
  m_actions.set_enabled("file.save_as", has_session);
  m_actions.set_enabled("file.revert", has_session &&
    m_session->is_dirty() && !m_session->get_path().isEmpty());
  m_actions.set_enabled("file.save_as_local", has_session);
  m_actions.set_enabled("window.document", has_session);
}

void MainWindow::update_document_state() {
  auto has_session = static_cast<bool>(m_session);
  for(auto widget : std::to_array<QWidget*>({m_app_bar, m_toolbar,
      m_left_panel, m_view, m_inspector_frame})) {
    widget->setEnabled(has_session);
  }
  update_document_actions(m_actions, m_app_bar->get_workspace(), has_session);
}

void MainWindow::update_selection_actions() {
  auto has_selection = m_session &&
    !m_session->get_editor().get_selection().is_empty();
  m_actions.set_enabled("view.zoom_selection", has_selection);
  m_actions.set_enabled("edit.rename", has_selection);
  update_asset_actions();
  update_presets();
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
  auto fitted = get_fitted_region(bounds->get_center(), get_zoom(),
    m_view->get_view_region().get_size());
  if(bounds->m_width > fitted.m_width || bounds->m_height > fitted.m_height) {
    m_view->zoom_to(*bounds);
    return;
  }
  m_view->zoom_to(fitted);
}

void MainWindow::reveal_artboard(const Rect& frame) {
  auto inverse = invert(m_view->get_renderer().get_view());
  if(!inverse || m_view->width() <= 0 || m_view->height() <= 0) {
    return;
  }
  auto visible = inverse->map(Rect(0, 0, m_view->width(), m_view->height()));
  if(frame.m_width > visible.m_width || frame.m_height > visible.m_height) {
    m_view->zoom_to(frame);
    return;
  }
  show_region(frame);
}

void MainWindow::show_region(const Rect& region) {
  auto inverse = invert(m_view->get_renderer().get_view());
  if(!inverse || m_view->width() <= 0 || m_view->height() <= 0) {
    return;
  }
  auto visible = inverse->map(Rect(0, 0, m_view->width(), m_view->height()));
  if(visible.contains(region)) {
    return;
  }
  m_view->center_on(region.get_center());
}

void MainWindow::update_presets() {
  auto tools = m_view ? m_view->get_tools() : nullptr;
  if(!m_session || !tools || tools->get_tool_name() != "artboard" ||
      m_app_bar->get_workspace() != Workspace::DESIGN) {
    m_inspector->set_footer(nullptr, false);
    return;
  }
  auto& document = m_session->get_document();
  auto& nodes = m_session->get_editor().get_selection().get_nodes();
  auto has_artboard = std::any_of(nodes.begin(), nodes.end(),
    [&] (const auto& id) {
      auto node = document.find(id);
      return node && node->get_artboard();
    });
  m_inspector->set_footer(m_presets, !has_artboard);
}

void MainWindow::zoom_to_selection() {
  if(!m_session) {
    return;
  }
  auto& nodes = m_session->get_editor().get_selection().get_nodes();
  if(auto bounds = get_world_bounds(m_session->get_document(), nodes,
      BoundsType::GEOMETRY, m_measurer)) {
    m_view->zoom_to(Rect(bounds->m_x, bounds->m_y,
      bounds->m_width > 0 ? bounds->m_width : 1,
      bounds->m_height > 0 ? bounds->m_height : 1));
  }
}

double MainWindow::get_zoom() const {
  return m_view->get_renderer().get_view().m_a;
}

void MainWindow::update_zoom() {
  m_app_bar->set_zoom(get_zoom());
}

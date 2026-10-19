#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <tuple>
#include <utility>
#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
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
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Rendering/RenderPlan.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/ComponentPropertyEditors.hpp"
#include "Rellu/Ui/ContextMenus.hpp"
#include "Rellu/Ui/LayoutProperties.hpp"
#include "Rellu/Ui/PropertyEditors.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "Rellu/Ui/TextPropertyEditors.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "MainWindow.hpp"
#include "NativeFrame.hpp"

using namespace Rellu;

namespace {
  constexpr auto ZOOM_STEP = 2.0;
  constexpr auto MAXIMUM_RECENT_FILES = 15;
  constexpr auto REVEAL_MARGIN = 24.0;
  constexpr auto LEFT_PANEL_WIDTH = 407;
  constexpr auto MENU_ROW_HEIGHT = 32;
  constexpr auto CAPTION_BUTTON_WIDTH = 46;
  constexpr auto HIT_TOLERANCE = 3.0;
  constexpr auto HIGHLIGHT_WIDTH = 2.0;
  constexpr auto HANDLE_RADIUS = 6.0;
  constexpr auto HANDLE_TOLERANCE = 8.0;
  constexpr auto ROTATION_HANDLE_DISTANCE = 20.0;
  constexpr auto TRACK_SEGMENTS = 96;
  const auto HANDLE_COLOR = QColor(0x26, 0x80, 0xEB);
  constexpr auto MESSAGE_MARGIN = 48;
  const auto HIGHLIGHT_COLOR = QColor(0x00, 0x90, 0xE9);
  const auto FILE_FILTER = QString("Adobe XD Files (*.xd);;All Files (*)");
  const auto PASTEBOARD = QColor(0xE4, 0xE4, 0xE4);
  const auto DEFAULT_PANEL = QString("layers");
  const auto DEFAULT_LANGUAGE = QString("english");
  const auto LANGUAGE_PREFIX = std::string("edit.preferences.language.");
  constexpr auto LANGUAGES = std::to_array({"german", "english", "spanish",
    "french", "italian", "japanese", "korean", "portuguese", "russian",
    "chinese"});

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

  enum class CaptionKind : unsigned char {
    MINIMIZE,
    MAXIMIZE,
    CLOSE
  };

  class CaptionButton : public QAbstractButton {
    public:
      CaptionButton(CaptionKind kind, const QString& tip)
          : m_kind(kind) {
        setObjectName("caption_button");
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(CAPTION_BUTTON_WIDTH, MENU_ROW_HEIGHT);
        setToolTip(tip);
        setAccessibleName(tip);
        setAttribute(Qt::WA_Hover);
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        auto is_hovered = underMouse();
        auto ink = QColor(0x3F, 0x3F, 0x3F);
        if(m_kind == CaptionKind::CLOSE && (is_hovered || isDown())) {
          painter.fillRect(rect(), isDown() ? QColor(0xF1, 0x70, 0x7A) :
            QColor(0xE8, 0x11, 0x23));
          ink = Qt::white;
        } else if(isDown()) {
          painter.fillRect(rect(), QColor(0xC6, 0xC6, 0xC6));
        } else if(is_hovered) {
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

  QPointF to_qpoint(Point point) {
    return QPointF(point.m_x, point.m_y);
  }

  double get_distance(QPointF left, QPointF right) {
    auto delta = left - right;
    return std::hypot(delta.x(), delta.y());
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
      m_rotation_handle_angle(DEFAULT_ROTATION_HANDLE_ANGLE) {
  add_standard_commands(m_commands, m_measurer);
  add_align_commands(m_commands, m_measurer);
  add_path_commands(m_commands);
  add_repeat_grid_commands(m_commands, m_measurer);
  add_component_commands(m_commands, m_measurer);
  add_header_commands(m_commands, m_measurer);
  add_artboard_commands(m_commands);
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
  auto bar = make_layout();
  add_xd_menus(m_actions);
  add_actions();
  add_component_actions();
  m_text_actions =
    std::make_unique<TextActions>(m_actions, get_default_font_library());
  add_asset_actions();
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

bool MainWindow::revert() {
  if(!m_session || m_session->get_path().isEmpty()) {
    return false;
  }
  auto path = m_session->get_path();
  auto box = QMessageBox(QMessageBox::Warning, "Rellu",
    QString("Do you want to revert to the most recently saved version of the "
      "document \"%1\"?").arg(QFileInfo(path).fileName()),
    QMessageBox::NoButton, this);
  box.setInformativeText("Your current changes will be lost.");
  auto revert = box.addButton("Revert", QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Cancel);
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

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if(event->type() == QEvent::WindowStateChange && m_maximize) {
    auto tip = isMaximized() ? QString("Restore Down") : QString("Maximize");
    m_maximize->setToolTip(tip);
    m_maximize->setAccessibleName(tip);
    m_maximize->update();
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if(m_view && watched == m_view) {
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
    m_gradient_drag = GradientDrag();
    m_rotation_handle_angle = DEFAULT_ROTATION_HANDLE_ANGLE;
    QTimer::singleShot(0, this, [this] {
      update_gradient_editor();
    });
  }
  return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(
    const QByteArray& type, void* message, qintptr* result) {
  if(m_maximize && handle_frame_event(*this, message, result,
      [this] (QPoint position) {
        return is_caption(position);
      })) {
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

void MainWindow::update_grid_actions() {
  auto texts = std::to_array<std::tuple<const char*, GridType, const char*,
      const char*>>({
    std::tuple("view.layout_grid", GridType::LAYOUT, "Show Layout Grid",
      "Hide Layout Grid"),
    std::tuple("view.square_grid", GridType::SQUARE, "Show Square Grid",
      "Hide Square Grid")});
  for(auto& [name, type, show, hide] : texts) {
    auto is_shown = false;
    if(m_session) {
      auto& document = m_session->get_document();
      auto artboards = get_target_artboards(document,
        m_session->get_editor().get_selection().get_nodes());
      is_shown = !artboards.empty() && std::all_of(artboards.begin(),
        artboards.end(), [&] (const auto& id) {
          auto& grid = document.get(id).get_artboard()->m_layout_grid;
          return grid && grid->m_is_visible && grid->m_type == type;
        });
    }
    m_actions.get(name).setText(is_shown ? hide : show);
  }
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
  if(!gradient || gradient->m_units != GradientUnits::OBJECT_BOUNDING_BOX ||
      !gradient->m_transform.is_identity() ||
      !std::holds_alternative<AngularGradient>(gradient->m_geometry)) {
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
  auto to_view = m_view->get_renderer().get_view() *
    get_world_transform(document, id) *
    make_translation(bounds->m_x, bounds->m_y) *
    make_scale(bounds->m_width, bounds->m_height);
  auto from_view = invert(to_view);
  if(!from_view) {
    return std::nullopt;
  }
  return GradientTarget(*gradient, to_view, *from_view);
}

void MainWindow::paint_gradient_editor(QPainter& painter) {
  auto target = find_gradient_target();
  if(!target) {
    return;
  }
  auto& angular = std::get<AngularGradient>(target->m_fill.m_geometry);
  auto point_at = [&] (double turns) {
    auto radians = 2 * std::numbers::pi * turns;
    return to_qpoint(target->m_to_view.map(angular.m_center +
      Point(0.5 * std::cos(radians), 0.5 * std::sin(radians))));
  };
  auto track = QPolygonF();
  for(auto i = 0; i != TRACK_SEGMENTS; ++i) {
    track.push_back(point_at(static_cast<double>(i) / TRACK_SEGMENTS));
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(0, 0, 0, 90), 3));
  painter.drawPolygon(track);
  painter.setPen(QPen(Qt::white, 1));
  painter.drawPolygon(track);
  auto center = to_qpoint(target->m_to_view.map(angular.m_center));
  auto handle = get_rotation_handle(*target);
  painter.setPen(QPen(HANDLE_COLOR, 1));
  painter.drawLine(center, handle);
  painter.setBrush(Qt::white);
  painter.drawEllipse(handle, HANDLE_RADIUS - 2, HANDLE_RADIUS - 2);
  painter.drawEllipse(center, HANDLE_RADIUS - 1, HANDLE_RADIUS - 1);
  auto selected = m_inspector->get_color_picker().get_stop();
  auto& stops = target->m_fill.m_stops;
  for(auto i = 0; i != static_cast<int>(stops.size()); ++i) {
    auto position =
      point_at(stops[i].m_offset + angular.m_rotation / 360);
    painter.setPen(QPen(i == selected ? HANDLE_COLOR : QColor(Qt::white),
      i == selected ? 2.5 : 2));
    painter.setBrush(QColor::fromRgbF(
      static_cast<float>(stops[i].m_color.m_red),
      static_cast<float>(stops[i].m_color.m_green),
      static_cast<float>(stops[i].m_color.m_blue)));
    painter.drawEllipse(position, HANDLE_RADIUS, HANDLE_RADIUS);
  }
}

QPointF MainWindow::get_rotation_handle(const GradientTarget& target) const {
  auto& angular = std::get<AngularGradient>(target.m_fill.m_geometry);
  auto center = to_qpoint(target.m_to_view.map(angular.m_center));
  auto radians = m_rotation_handle_angle * std::numbers::pi / 180;
  auto direction = QPointF(std::cos(radians), std::sin(radians));
  auto unit = target.m_from_view.map(
    Point(center.x() + direction.x(), center.y() + direction.y()));
  auto offset = get_angular_offset(angular, unit);
  auto turns = 2 * std::numbers::pi * (offset + angular.m_rotation / 360);
  auto edge = to_qpoint(target.m_to_view.map(angular.m_center +
    Point(0.5 * std::cos(turns), 0.5 * std::sin(turns))));
  return center + direction *
    (get_distance(edge, center) + ROTATION_HANDLE_DISTANCE);
}

bool MainWindow::handle_gradient_event(
    ColorPicker::CanvasEvent event, QPointF position) {
  auto& picker = m_inspector->get_color_picker();
  auto target = find_gradient_target();
  if(event == ColorPicker::CanvasEvent::RELEASE) {
    m_gradient_drag = GradientDrag();
    return true;
  }
  if(!target) {
    return false;
  }
  auto local = QPointF(m_view->mapFromGlobal(position.toPoint()));
  auto unit = target->m_from_view.map(Point(local.x(), local.y()));
  auto fill = target->m_fill;
  auto& angular = std::get<AngularGradient>(fill.m_geometry);
  auto center = to_qpoint(target->m_to_view.map(angular.m_center));
  if(event == ColorPicker::CanvasEvent::PRESS) {
    m_gradient_drag = GradientDrag();
    auto& stops = fill.m_stops;
    for(auto i = 0; i != static_cast<int>(stops.size()); ++i) {
      auto turns = 2 * std::numbers::pi *
        (stops[i].m_offset + angular.m_rotation / 360);
      auto stop = to_qpoint(target->m_to_view.map(angular.m_center +
        Point(0.5 * std::cos(turns), 0.5 * std::sin(turns))));
      if(get_distance(stop, local) <= HANDLE_TOLERANCE) {
        m_gradient_drag.m_part = GradientPart::STOP;
        m_gradient_drag.m_stop = i;
        picker.set_stop(i);
        m_gradient_overlay->update();
        return true;
      }
    }
    if(get_distance(get_rotation_handle(*target), local) <=
        HANDLE_TOLERANCE) {
      m_gradient_drag.m_part = GradientPart::ROTATION;
      m_gradient_drag.m_rotation = angular.m_rotation;
      m_gradient_drag.m_angle = std::atan2(unit.m_y - angular.m_center.m_y,
        unit.m_x - angular.m_center.m_x) * 180 / std::numbers::pi;
      return true;
    }
    if(get_distance(center, local) <= HANDLE_TOLERANCE) {
      m_gradient_drag.m_part = GradientPart::CENTER;
      m_gradient_drag.m_offset = angular.m_center - unit;
      return true;
    }
    auto offset = get_angular_offset(angular, unit);
    auto turns =
      2 * std::numbers::pi * (offset + angular.m_rotation / 360);
    auto track = to_qpoint(target->m_to_view.map(angular.m_center +
      Point(0.5 * std::cos(turns), 0.5 * std::sin(turns))));
    if(get_distance(track, local) > HANDLE_TOLERANCE) {
      return false;
    }
    auto index = static_cast<int>(std::upper_bound(stops.begin(),
      stops.end(), offset, [] (double offset, const auto& stop) {
        return offset < stop.m_offset;
      }) - stops.begin());
    stops.insert(stops.begin() + index,
      GradientStop(offset, evaluate_gradient(target->m_fill.m_stops, offset)));
    picker.apply_fill(fill);
    picker.set_stop(index);
    m_gradient_drag.m_part = GradientPart::STOP;
    m_gradient_drag.m_stop = index;
    m_gradient_overlay->update();
    return true;
  }
  if(m_gradient_drag.m_part == GradientPart::CENTER) {
    angular.m_center = unit + m_gradient_drag.m_offset;
  } else if(m_gradient_drag.m_part == GradientPart::ROTATION) {
    auto angle = std::atan2(unit.m_y - angular.m_center.m_y,
      unit.m_x - angular.m_center.m_x) * 180 / std::numbers::pi;
    angular.m_rotation =
      m_gradient_drag.m_rotation + angle - m_gradient_drag.m_angle;
    m_rotation_handle_angle = std::atan2(local.y() - center.y(),
      local.x() - center.x()) * 180 / std::numbers::pi;
  } else if(m_gradient_drag.m_part == GradientPart::STOP &&
      m_gradient_drag.m_stop < static_cast<int>(fill.m_stops.size())) {
    auto stop = fill.m_stops[m_gradient_drag.m_stop];
    stop.m_offset = get_angular_offset(angular, unit);
    fill.m_stops.erase(fill.m_stops.begin() + m_gradient_drag.m_stop);
    auto index = static_cast<int>(std::upper_bound(fill.m_stops.begin(),
      fill.m_stops.end(), stop.m_offset, [] (double offset, const auto& stop) {
        return offset < stop.m_offset;
      }) - fill.m_stops.begin());
    fill.m_stops.insert(fill.m_stops.begin() + index, stop);
    m_gradient_drag.m_stop = index;
    picker.apply_fill(fill);
    picker.set_stop(index);
    m_gradient_overlay->update();
    return true;
  } else {
    return true;
  }
  picker.apply_fill(fill);
  m_gradient_overlay->update();
  return true;
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
    QToolTip::showText(m_view->mapToGlobal(QPoint(m_view->width() / 2,
      m_view->height() - MESSAGE_MARGIN)), message, m_view);
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
  body_layout->addWidget(m_view, 1);
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
    show_region(region);
  });
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
  m_actions.set_handler("window.preview", [this] { toggle_preview(); });
  for(auto language : LANGUAGES) {
    auto name = std::string(language);
    m_actions.set_handler(LANGUAGE_PREFIX + name, [this, name] {
      set_language(name);
      auto settings = QSettings();
      settings.setValue("preferences/language", QString::fromStdString(name));
    });
  }
  for(auto button : m_app_bar->findChildren<QToolButton*>()) {
    if(button->accessibleName() == "Desktop Preview") {
      button->setEnabled(true);
      connect(button, &QToolButton::clicked, this, [this] {
        m_actions.get("window.preview").trigger();
      });
    }
  }
  m_actions.set_handler("help.about", [this] {
    QMessageBox::about(this, "About Rellu", "Rellu\n\n"
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
  for(auto& tool : m_toolbar->get_tools()) {
    m_actions.set_enabled("tool." + tool.m_name,
      workspace == Workspace::DESIGN || tool.m_name == "select" ||
      tool.m_name == "zoom");
  }
  m_inspector->set_workspace(workspace);
  m_edit_actions->set_workspace(workspace);
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
  m_preview->setWindowTitle("Preview : " + m_session->get_name());
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
    resize(1600, 960);
  }
  show_panel(settings.value("main_window/panel", DEFAULT_PANEL).toString().
    toStdString());
  set_language(settings.value("preferences/language", DEFAULT_LANGUAGE).
    toString().toStdString());
}

void MainWindow::set_language(const std::string& language) {
  auto chosen = std::find(LANGUAGES.begin(), LANGUAGES.end(), language) ==
    LANGUAGES.end() ? DEFAULT_LANGUAGE.toStdString() : language;
  for(auto candidate : LANGUAGES) {
    m_actions.get(LANGUAGE_PREFIX + candidate).setChecked(
      chosen == candidate);
  }
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
  editor.get_history().set_text_measurer(m_measurer);
  m_layers->set_editor(&editor);
  m_assets->set_editor(&editor);
  m_inspector->set_editor(&editor);
  m_prototype_inspector->set_editor(&editor);
  m_presets->set_editor(&editor);
  m_edit_actions->set_editor(&editor);
  m_component_actions->set_editor(&editor);
  m_text_actions->set_editor(&editor);
  m_component_drop->set_editor(&editor);
  m_history_subscription = editor.get_history().subscribe([this] {
    update_title();
    update_asset_actions();
    update_grid_actions();
  });
  m_selection_subscription = editor.get_selection().subscribe([this] {
    set_highlights({});
    update_selection_actions();
  });
  if(auto tools = m_view->get_tools()) {
    tools->get_context().set_text_measurer(m_measurer);
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
    m_actions.get("window.document").setText("Untitled");
  }
  m_actions.set_enabled("file.save", has_session &&
    (m_session->is_dirty() || m_session->get_path().isEmpty()));
  m_actions.set_enabled("file.save_as", has_session);
  m_actions.set_enabled("file.revert", has_session &&
    m_session->is_dirty() && !m_session->get_path().isEmpty());
  m_actions.set_enabled("file.save_as_local", has_session);
  m_actions.set_enabled("window.document", has_session);
}

void MainWindow::update_selection_actions() {
  auto has_selection = m_session &&
    !m_session->get_editor().get_selection().is_empty();
  m_actions.set_enabled("view.zoom_selection", has_selection);
  m_actions.set_enabled("edit.rename", has_selection);
  update_asset_actions();
  update_grid_actions();
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

void MainWindow::show_region(const Rect& region) {
  auto inverse = invert(m_view->get_renderer().get_view());
  if(!inverse || m_view->width() <= 0 || m_view->height() <= 0) {
    return;
  }
  auto visible = inverse->map(Rect(0, 0, m_view->width(), m_view->height()));
  if(visible.contains(region)) {
    return;
  }
  auto zoom = get_zoom();
  auto size = Size(
    std::max(m_view->width() - 2 * REVEAL_MARGIN, 1.0) / zoom,
    std::max(m_view->height() - 2 * REVEAL_MARGIN, 1.0) / zoom);
  if(region.m_width > size.m_width || region.m_height > size.m_height) {
    m_view->zoom_to(region);
    return;
  }
  auto center = region.get_center();
  m_view->zoom_to(Rect(center.m_x - size.m_width / 2,
    center.m_y - size.m_height / 2, size.m_width, size.m_height));
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
    m_view->zoom_to(*bounds);
  }
}

double MainWindow::get_zoom() const {
  return m_view->get_renderer().get_view().m_a;
}

void MainWindow::update_zoom() {
  m_app_bar->set_zoom(get_zoom());
}

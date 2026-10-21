#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QStyleHints>
#include <QWheelEvent>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Tools/CanvasBounds.hpp"
#include "Rellu/Tools/CanvasCursors.hpp"
#include "Rellu/Tools/PathEditTool.hpp"
#include "Rellu/Tools/ViewFit.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "DocumentView.hpp"

using namespace Rellu;

namespace {
  constexpr auto WHEEL_ZOOM = 1.0015;
  constexpr auto SCENE_BUDGET = std::chrono::milliseconds(8);
  constexpr auto PREPARATION_BUDGET = std::chrono::milliseconds(8);
  constexpr auto TEXT_SETTLE_TIME = std::chrono::milliseconds(150);
  constexpr auto TITLE_PADDING = 1;
  constexpr auto TITLE_SLACK = 12.0;
  const auto TITLE_STYLE = QString(
    "QLineEdit { border: 1px solid #2680EB; padding: 0px 1px; "
    "background: white; color: #2680EB; }");

  class TitleEditor : public QLineEdit {
    public:
      std::function<void ()> m_cancel;

      explicit TitleEditor(QWidget* parent)
        : QLineEdit(parent) {}

    protected:
      void keyPressEvent(QKeyEvent* event) override {
        if(event->key() == Qt::Key_Escape && m_cancel) {
          event->accept();
          m_cancel();
          return;
        }
        QLineEdit::keyPressEvent(event);
      }
  };

  bool is_moving(const ChangeSet& changes) {
    return std::any_of(changes.m_modified.begin(), changes.m_modified.end(),
      [] (const auto& change) {
        return change.m_mask == ChangeMask::TRANSFORM;
      });
  }

  double get_level(double zoom) {
    return std::exp2(std::ceil(std::log2(zoom)));
  }

  PointerEvent to_pointer_event(const QMouseEvent& event) {
    return PointerEvent(Point(event.position().x(), event.position().y()),
      event.button(), event.buttons(), event.modifiers());
  }

  KeyEvent to_key_event(const QKeyEvent& event) {
    return KeyEvent(event.key(), event.modifiers(), event.isAutoRepeat(),
      event.text().toStdU16String());
  }

  InputMethodEvent to_input_method_event(const QInputMethodEvent& event) {
    auto cursor = static_cast<int>(event.preeditString().size());
    for(auto& attribute : event.attributes()) {
      if(attribute.type == QInputMethodEvent::Cursor) {
        cursor = attribute.length == 0 ? -1 : attribute.start;
      }
    }
    return InputMethodEvent(event.commitString().toStdU16String(),
      event.preeditString().toStdU16String(), cursor,
      event.replacementStart(), event.replacementLength());
  }
}

DocumentView::DocumentView(QWidget* parent)
    : Canvas(parent),
      m_shared_editor(nullptr),
      m_editor(nullptr),
      m_zoom(1),
      m_offset(0, 0),
      m_level(1),
      m_text_scale(0),
      m_is_text_settled(false),
      m_is_fitting(false),
      m_is_fit_held(false),
      m_is_space_pressed(false),
      m_is_panning(false),
      m_title_editor(nullptr) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_InputMethodEnabled);
  m_caret_timer.setInterval(
    std::max(QGuiApplication::styleHints()->cursorFlashTime() / 2, 1));
  QObject::connect(&m_caret_timer, &QTimer::timeout, this, [this] {
    auto tool = get_text_tool();
    if(!tool || !tool->is_editing(m_tools->get_context())) {
      m_caret_timer.stop();
      return;
    }
    tool->blink();
    update();
  });
  m_text_timer.setSingleShot(true);
  m_text_timer.setInterval(TEXT_SETTLE_TIME);
  QObject::connect(&m_text_timer, &QTimer::timeout, this, [this] {
    if(m_scene) {
      m_is_text_settled = true;
      m_scene->set_text_transform(make_scale(m_text_scale, m_text_scale));
      update();
    }
  });
  m_preparation.setInterval(0);
  QObject::connect(&m_preparation, &QTimer::timeout, this, [this] {
    if(!m_tools || m_tools->get_hit_tester().prepare(PREPARATION_BUDGET)) {
      m_preparation.stop();
    }
  });
}

DocumentView::~DocumentView() {
  finish_rename(false);
  m_preparation.stop();
  m_text_timer.stop();
  m_caret_timer.stop();
  m_text_subscription.release();
  m_tool_subscription.release();
  m_tools.reset();
  m_own_editor.reset();
}

const Document* DocumentView::get_document() const {
  return m_document.get();
}

void DocumentView::set_document(std::unique_ptr<Document> document) {
  finish_rename(false);
  get_renderer().set_draw_list(nullptr);
  get_renderer().set_overlay(CanvasOverlay());
  m_text_subscription.release();
  m_tool_subscription.release();
  m_tools.reset();
  m_own_editor.reset();
  m_editor = nullptr;
  m_subscription.release();
  m_scene.reset();
  m_document = std::move(document);
  if(m_document) {
    m_level = 1;
    m_text_scale = 0;
    m_scene = std::make_unique<Scene>(*m_document, StripRasterizer::TOLERANCE);
    m_scene->update();
    get_renderer().set_drawing(m_scene->get_drawing());
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      if(is_moving(changes)) {
        m_is_text_settled = false;
        m_text_timer.start();
      }
      update();
    });
  }
  attach_tools();
  fit();
}

Editor* DocumentView::get_editor() const {
  return m_editor;
}

void DocumentView::set_editor(Editor* editor) {
  finish_rename(false);
  m_shared_editor = editor;
  attach_tools();
  update_view();
}

ToolManager* DocumentView::get_tools() const {
  return m_tools.get();
}

void DocumentView::fit() {
  m_is_fitting = true;
  auto bounds = Rect(0, 0, 0, 0);
  if(m_scene) {
    bounds = m_scene->get_drawing()->get_bounds();
  }
  if(bounds.is_empty() || width() <= 0 || height() <= 0) {
    m_zoom = 1;
    m_offset = Point(0, 0);
    update_view();
  } else {
    frame(bounds);
  }
}

void DocumentView::set_fit_held(bool is_held) {
  m_is_fit_held = is_held;
}

void DocumentView::zoom_to(const Rect& region) {
  m_is_fitting = false;
  frame(region);
}

void DocumentView::center_on(Point point) {
  m_is_fitting = false;
  m_offset = Point(width() / 2.0 - m_zoom * point.m_x,
    height() / 2.0 - m_zoom * point.m_y);
  update_view();
}

void DocumentView::set_zoom(double zoom) {
  this->zoom(zoom / m_zoom, QPointF(width() / 2.0, height() / 2.0));
}

bool DocumentView::event(QEvent* event) {
  if(event->type() == QEvent::ShortcutOverride) {
    if(auto tool = get_text_tool(); tool && tool->is_text_key(
        m_tools->get_context(),
        to_key_event(*static_cast<QKeyEvent*>(event)))) {
      event->accept();
      return true;
    }
    if(auto tool = m_tools ?
        dynamic_cast<PathEditTool*>(&m_tools->get_tool()) : nullptr; tool &&
        tool->is_edit_key(m_tools->get_context(),
          to_key_event(*static_cast<QKeyEvent*>(event)))) {
      event->accept();
      return true;
    }
  }
  return Canvas::event(event);
}

void DocumentView::render(QRhiCommandBuffer* buffer) {
  if(!m_is_fit_held) {
    m_is_fitting = false;
  }
  update_text();
  if(m_scene) {
    auto region = Rect(-m_offset.m_x / m_zoom, -m_offset.m_y / m_zoom,
      width() / m_zoom, height() / m_zoom);
    if(!m_scene->is_current()) {
      get_renderer().invalidate(m_scene->update(region, SCENE_BUDGET));
    } else if(m_is_text_settled && !m_scene->is_rasterized()) {
      get_renderer().invalidate(m_scene->rasterize(region, SCENE_BUDGET));
    }
    if(!m_scene->is_current() ||
        (m_is_text_settled && !m_scene->is_rasterized())) {
      update();
    }
  }
  if(m_document) {
    auto overlay = m_tools ? m_tools->draw() : CanvasOverlay();
    auto outside = std::vector<OverlayShape>();
    for(auto& region : get_outside_regions(
        get_renderer().get_view(), Size(width(), height()))) {
      auto shape = OverlayShape();
      shape.m_center = region.get_center();
      shape.m_size = region.get_size();
      shape.m_fill = OUTSIDE_CANVAS_COLOR;
      outside.push_back(shape);
    }
    overlay.m_shapes.insert(
      overlay.m_shapes.begin(), outside.begin(), outside.end());
    get_renderer().set_overlay(std::move(overlay));
  }
  Canvas::render(buffer);
}

void DocumentView::frame(const Rect& region) {
  auto size = Size(width(), height());
  if(size.m_width > 0 && size.m_height > 0) {
    if(region.m_width > 0 || region.m_height > 0) {
      auto fit = fit_view(region, size);
      m_zoom = fit.m_zoom;
      m_offset = fit.m_offset;
    } else {
      m_offset = Point(size.m_width / 2 - m_zoom * region.m_x,
        size.m_height / 2 - m_zoom * region.m_y);
    }
  }
  update_view();
}

void DocumentView::mousePressEvent(QMouseEvent* event) {
  if(event->button() == Qt::MiddleButton ||
      (event->button() == Qt::LeftButton && m_is_space_pressed)) {
    m_is_panning = true;
    m_position = event->position();
    update_cursor();
    event->accept();
    return;
  }
  if(forward(event, [&] (const auto& pointer) {
      m_tools->press(pointer);
    })) {
    update_text_editing();
    return;
  }
  Canvas::mousePressEvent(event);
}

void DocumentView::mouseMoveEvent(QMouseEvent* event) {
  if(m_is_panning) {
    auto position = event->position();
    pan(position - m_position);
    m_position = position;
    event->accept();
    return;
  }
  if(forward(event, [&] (const auto& pointer) {
      m_tools->move(pointer);
    })) {
    return;
  }
  Canvas::mouseMoveEvent(event);
}

void DocumentView::mouseReleaseEvent(QMouseEvent* event) {
  if(m_is_panning && (event->button() == Qt::MiddleButton ||
      event->button() == Qt::LeftButton)) {
    m_is_panning = false;
    update_cursor();
    event->accept();
    return;
  }
  if(forward(event, [&] (const auto& pointer) {
      m_tools->release(pointer);
    })) {
    update_text_editing();
    return;
  }
  Canvas::mouseReleaseEvent(event);
}

void DocumentView::mouseDoubleClickEvent(QMouseEvent* event) {
  if(event->button() == Qt::MiddleButton ||
      (event->button() == Qt::LeftButton && m_is_space_pressed)) {
    mousePressEvent(event);
    return;
  }
  if(event->button() == Qt::LeftButton && !m_is_space_pressed &&
      start_rename(Point(event->position().x(), event->position().y()))) {
    event->accept();
    return;
  }
  if(forward(event, [&] (const auto& pointer) {
      m_tools->double_click(pointer);
    })) {
    update_text_editing();
    return;
  }
  Canvas::mouseDoubleClickEvent(event);
}

void DocumentView::wheelEvent(QWheelEvent* event) {
  if(event->modifiers() & Qt::ControlModifier) {
    auto delta = event->angleDelta().y();
    if(delta == 0) {
      delta = event->pixelDelta().y();
    }
    zoom(std::pow(WHEEL_ZOOM, delta), event->position());
  } else {
    auto delta = QPointF(event->pixelDelta());
    if(delta.isNull()) {
      delta = QPointF(event->angleDelta()) / 2;
    }
    if(event->modifiers() & Qt::ShiftModifier) {
      delta = QPointF(delta.y(), delta.x());
    }
    pan(delta);
  }
  event->accept();
}

void DocumentView::keyPressEvent(QKeyEvent* event) {
  auto text_tool = get_text_tool();
  auto is_editing =
    text_tool && text_tool->is_editing(m_tools->get_context());
  if(m_tools && !is_editing && event->key() == Qt::Key_T &&
      event->modifiers() == Qt::NoModifier) {
    m_tools->set_tool("text");
    update_cursor();
    event->accept();
    return;
  }
  if(event->key() == Qt::Key_Space && !is_editing) {
    if(!event->isAutoRepeat()) {
      m_is_space_pressed = true;
      update_cursor();
    }
    event->accept();
    return;
  }
  if(m_tools && m_tools->key_press(to_key_event(*event))) {
    update_cursor();
    update();
    update_text_editing();
    event->accept();
    return;
  }
  Canvas::keyPressEvent(event);
}

void DocumentView::keyReleaseEvent(QKeyEvent* event) {
  if(event->key() == Qt::Key_Space && m_is_space_pressed) {
    if(!event->isAutoRepeat()) {
      m_is_space_pressed = false;
      update_cursor();
    }
    event->accept();
    return;
  }
  if(m_tools && m_tools->key_release(to_key_event(*event))) {
    update_cursor();
    update();
    event->accept();
    return;
  }
  Canvas::keyReleaseEvent(event);
}

void DocumentView::inputMethodEvent(QInputMethodEvent* event) {
  if(auto tool = get_text_tool();
      tool && tool->is_editing(m_tools->get_context())) {
    tool->input_method(m_tools->get_context(), to_input_method_event(*event));
    update();
    update_text_editing();
    event->accept();
    return;
  }
  Canvas::inputMethodEvent(event);
}

QVariant DocumentView::inputMethodQuery(Qt::InputMethodQuery query) const {
  auto tool = get_text_tool();
  if(!tool || !tool->is_editing(m_tools->get_context())) {
    if(query == Qt::ImEnabled) {
      return false;
    }
    return Canvas::inputMethodQuery(query);
  }
  auto& context = m_tools->get_context();
  auto& selection = m_editor->get_text_selection();
  auto content = QString();
  if(auto node = m_document->find(selection.get_node())) {
    content = QString::fromStdU16String(*node->get_text()->m_content);
  }
  auto range = selection.get_range();
  if(query == Qt::ImEnabled) {
    return true;
  } else if(query == Qt::ImCursorRectangle) {
    if(auto rect = tool->get_caret_rect(context)) {
      return QRectF(rect->m_x, rect->m_y, std::max(rect->m_width, 1.0),
        rect->m_height);
    }
    return QRectF();
  } else if(query == Qt::ImCursorPosition) {
    return static_cast<int>(selection.get_caret());
  } else if(query == Qt::ImAnchorPosition) {
    return static_cast<int>(selection.get_anchor());
  } else if(query == Qt::ImSurroundingText) {
    return content;
  } else if(query == Qt::ImCurrentSelection) {
    return content.mid(static_cast<qsizetype>(range.m_start),
      static_cast<qsizetype>(range.m_end - range.m_start));
  } else if(query == Qt::ImHints) {
    return static_cast<int>(Qt::ImhMultiLine);
  }
  return Canvas::inputMethodQuery(query);
}

void DocumentView::leaveEvent(QEvent* event) {
  if(m_tools && !m_tools->is_busy()) {
    m_tools->leave();
    update_cursor();
  }
  Canvas::leaveEvent(event);
}

void DocumentView::resizeEvent(QResizeEvent* event) {
  Canvas::resizeEvent(event);
  if(m_is_fitting) {
    fit();
  } else {
    update_view();
  }
}

void DocumentView::zoom(double factor, QPointF center) {
  m_is_fitting = false;
  auto zoom = std::clamp(m_zoom * factor, MINIMUM_ZOOM, MAXIMUM_ZOOM);
  auto anchor = Point((center.x() - m_offset.m_x) / m_zoom,
    (center.y() - m_offset.m_y) / m_zoom);
  m_zoom = zoom;
  m_offset = Point(center.x() - m_zoom * anchor.m_x,
    center.y() - m_zoom * anchor.m_y);
  update_view();
}

void DocumentView::pan(QPointF delta) {
  m_is_fitting = false;
  m_offset = Point(m_offset.m_x + delta.x(), m_offset.m_y + delta.y());
  update_view();
}

void DocumentView::update_view() {
  auto level = get_level(m_zoom * devicePixelRatio());
  if(level != m_level) {
    m_level = level;
    if(m_scene) {
      m_scene->set_tolerance(StripRasterizer::TOLERANCE / m_level);
    }
  }
  auto view =
    make_translation(m_offset.m_x, m_offset.m_y) * make_scale(m_zoom, m_zoom);
  get_renderer().set_view(view);
  if(m_tools) {
    m_tools->set_view(view, devicePixelRatio(), Size(width(), height()));
  }
  update_text();
  place_title_editor();
  update();
}

void DocumentView::update_text() {
  if(!m_scene) {
    return;
  }
  auto scale = m_zoom * devicePixelRatio();
  if(scale != m_text_scale) {
    m_text_scale = scale;
    m_is_text_settled = false;
    m_scene->set_text_transform(std::nullopt);
    m_text_timer.start();
  }
  auto region = Rect(-m_offset.m_x / m_zoom, -m_offset.m_y / m_zoom,
    width() / m_zoom, height() / m_zoom);
  m_scene->set_text_region(
    inflate(region, region.m_width, region.m_height));
}

void DocumentView::update_cursor() {
  auto cursor = ToolCursor();
  if(m_is_panning) {
    cursor = ToolCursor(CursorKind::CLOSED_HAND);
  } else if(m_is_space_pressed) {
    cursor = ToolCursor(CursorKind::OPEN_HAND);
  } else if(m_tools) {
    cursor = m_tools->get_cursor();
  }
  auto ratio = devicePixelRatio();
  auto key = std::tuple(cursor.m_kind, get_orientation(cursor), ratio);
  auto entry = m_cursors.find(key);
  if(entry == m_cursors.end()) {
    auto hot_spot = get_hot_spot(cursor);
    entry = m_cursors.emplace(key, QCursor(QPixmap::fromImage(
      draw_cursor(cursor, ratio)), hot_spot.x(), hot_spot.y())).first;
  }
  setCursor(entry->second);
}

void DocumentView::attach_tools() {
  auto editor = static_cast<Editor*>(nullptr);
  if(m_document) {
    if(m_shared_editor) {
      if(&m_shared_editor->get_document() == m_document.get()) {
        editor = m_shared_editor;
      }
    } else {
      if(!m_own_editor) {
        m_own_editor = std::make_unique<Editor>(*m_document);
      }
      editor = m_own_editor.get();
    }
  }
  if(editor == m_editor && (m_tools || !editor)) {
    return;
  }
  m_text_subscription.release();
  m_tool_subscription.release();
  m_tools.reset();
  m_editor = editor;
  get_renderer().set_overlay(CanvasOverlay());
  if(editor != m_own_editor.get()) {
    m_own_editor.reset();
  }
  update_cursor();
  update_edited_text();
  if(!editor) {
    return;
  }
  m_text_subscription = editor->get_text_selection().subscribe([this] {
    update_edited_text();
  });
  m_tools = std::make_unique<ToolManager>(
    *editor, make_text_measurer(get_default_font_library()));
  m_tool_subscription = m_tools->subscribe([this] {
    update();
    update_cursor();
  });
  m_tools->get_context().set_navigator(ViewNavigator(
    [this] (const auto& factor, const auto& center) {
      zoom(factor, QPointF(center.m_x, center.m_y));
    }, [this] (const auto& region) {
      zoom_to(region);
    }));
  m_tools->set_view(get_renderer().get_view(), devicePixelRatio(),
    Size(width(), height()));
  m_preparation.start();
}

TextTool* DocumentView::get_text_tool() const {
  if(!m_tools) {
    return nullptr;
  }
  return dynamic_cast<TextTool*>(&m_tools->get_tool());
}

void DocumentView::update_text_editing() {
  auto tool = get_text_tool();
  if(!tool || !tool->is_editing(m_tools->get_context())) {
    m_caret_timer.stop();
    return;
  }
  if(QGuiApplication::styleHints()->cursorFlashTime() > 0) {
    m_caret_timer.start();
  }
  if(auto method = QGuiApplication::inputMethod()) {
    method->update(Qt::ImQueryAll);
  }
}

void DocumentView::update_edited_text() {
  if(!m_scene) {
    return;
  }
  auto id = NodeId();
  if(m_editor && m_editor->get_text_selection().is_editing()) {
    id = m_editor->get_text_selection().get_node();
  }
  m_scene->set_edited_text(id);
  update();
}

bool DocumentView::forward(QMouseEvent* event, auto handler) {
  if(!m_tools || m_is_space_pressed) {
    return false;
  }
  if(event->type() != QEvent::MouseMove &&
      event->button() != Qt::LeftButton) {
    return false;
  }
  handler(to_pointer_event(*event));
  update_cursor();
  event->accept();
  return true;
}

bool DocumentView::start_rename(Point position) {
  if(!m_tools || !m_editor) {
    return false;
  }
  auto& titles = m_tools->get_context().get_titles();
  auto id = titles.find(position);
  auto node = m_document ? m_document->find(id) : nullptr;
  if(!node || node->m_header.m_is_locked) {
    return false;
  }
  finish_rename(true);
  m_editor->get_selection().select(id);
  m_renamed = id;
  auto editor = new TitleEditor(this);
  editor->m_cancel = [this] {
    finish_rename(false);
  };
  editor->setStyleSheet(TITLE_STYLE);
  editor->setFrame(false);
  editor->setFont(titles.get_font());
  editor->setTextMargins(TITLE_PADDING, 0, TITLE_PADDING, 0);
  editor->setText(QString::fromStdString(node->m_header.m_name));
  QObject::connect(editor, &QLineEdit::editingFinished, this, [this] {
    finish_rename(true);
  });
  QObject::connect(editor, &QLineEdit::textChanged, this, [this] {
    place_title_editor();
  });
  m_title_editor = editor;
  titles.set_hidden(id);
  place_title_editor();
  editor->show();
  editor->selectAll();
  editor->setFocus(Qt::MouseFocusReason);
  update();
  return true;
}

void DocumentView::finish_rename(bool is_committed) {
  if(!m_title_editor) {
    return;
  }
  auto editor = std::exchange(m_title_editor, nullptr);
  auto id = std::exchange(m_renamed, NodeId());
  auto text = editor->text().toStdString();
  editor->disconnect(this);
  editor->hide();
  editor->deleteLater();
  if(m_tools) {
    m_tools->get_context().get_titles().set_hidden(NodeId());
  }
  auto node = m_document ? m_document->find(id) : nullptr;
  if(is_committed && m_editor && node &&
      !QString::fromStdString(text).trimmed().isEmpty() &&
      text != node->m_header.m_name) {
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(std::make_unique<RenameCommand>(id, text));
    m_editor->execute(std::make_unique<CompositeCommand>(
      "Rename Artboard", std::move(commands)));
  }
  setFocus();
  update();
}

void DocumentView::place_title_editor() {
  if(!m_title_editor || !m_tools) {
    return;
  }
  auto& titles = m_tools->get_context().get_titles();
  auto node = m_document ? m_document->find(m_renamed) : nullptr;
  auto artboard = node ? node->get_artboard() : nullptr;
  if(!artboard) {
    return;
  }
  auto frame = get_renderer().get_view().map(artboard->m_frame);
  auto metrics = QFontMetricsF(titles.get_font());
  auto width = metrics.horizontalAdvance(m_title_editor->text()) +
    TITLE_SLACK;
  auto height = ArtboardTitles::HEIGHT + 2 * TITLE_PADDING + 2;
  m_title_editor->setGeometry(static_cast<int>(std::floor(frame.m_x)) -
    TITLE_PADDING - 1, static_cast<int>(std::floor(frame.m_y -
      ArtboardTitles::GAP - ArtboardTitles::HEIGHT)) - TITLE_PADDING - 1,
    static_cast<int>(std::ceil(width)), static_cast<int>(height));
}

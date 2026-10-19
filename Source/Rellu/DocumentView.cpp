#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QStyleHints>
#include <QWheelEvent>
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "DocumentView.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_ZOOM = 1.0 / 64;
  constexpr auto MAXIMUM_ZOOM = 256.0;
  constexpr auto FIT_MARGIN = 24.0;
  constexpr auto WHEEL_ZOOM = 1.0015;
  constexpr auto SCENE_BUDGET = std::chrono::milliseconds(8);
  constexpr auto CURSOR_SIZE = 24;
  constexpr auto PREPARATION_BUDGET = std::chrono::milliseconds(8);

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

  Qt::CursorShape get_resize_shape(double angle) {
    auto eighth = std::numbers::pi / 4;
    auto index = static_cast<int>(std::lround(angle / eighth)) % 4;
    if(index < 0) {
      index += 4;
    }
    if(index == 0) {
      return Qt::SizeHorCursor;
    } else if(index == 1) {
      return Qt::SizeFDiagCursor;
    } else if(index == 2) {
      return Qt::SizeVerCursor;
    }
    return Qt::SizeBDiagCursor;
  }

  QCursor make_rotate_cursor() {
    auto pixmap = QPixmap(CURSOR_SIZE, CURSOR_SIZE);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    auto arc = QPainterPath();
    auto bounds = QRectF(5, 5, 14, 14);
    arc.arcMoveTo(bounds, 200);
    arc.arcTo(bounds, 200, 230);
    auto draw = [&] (const QColor& color, double width) {
      painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap,
        Qt::RoundJoin));
      painter.drawPath(arc);
      auto start = arc.pointAtPercent(0);
      auto end = arc.pointAtPercent(1);
      painter.drawLine(start, start + QPointF(-2.5, -3));
      painter.drawLine(start, start + QPointF(3, -2));
      painter.drawLine(end, end + QPointF(3, 1.5));
      painter.drawLine(end, end + QPointF(1, 3.5));
    };
    draw(Qt::white, 3.5);
    draw(Qt::black, 1.5);
    painter.end();
    return QCursor(pixmap, CURSOR_SIZE / 2, CURSOR_SIZE / 2);
  }
}

DocumentView::DocumentView(QWidget* parent)
    : Canvas(parent),
      m_shared_editor(nullptr),
      m_editor(nullptr),
      m_zoom(1),
      m_offset(0, 0),
      m_level(1),
      m_is_fitting(false),
      m_is_space_pressed(false),
      m_is_panning(false) {
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
  m_preparation.setInterval(0);
  QObject::connect(&m_preparation, &QTimer::timeout, this, [this] {
    if(!m_tools || m_tools->get_hit_tester().prepare(PREPARATION_BUDGET)) {
      m_preparation.stop();
    }
  });
}

DocumentView::~DocumentView() {
  m_preparation.stop();
  m_caret_timer.stop();
  m_tool_subscription.release();
  m_tools.reset();
  m_own_editor.reset();
}

const Document* DocumentView::get_document() const {
  return m_document.get();
}

void DocumentView::set_document(std::unique_ptr<Document> document) {
  get_renderer().set_draw_list(nullptr);
  get_renderer().set_overlay(CanvasOverlay());
  m_tool_subscription.release();
  m_tools.reset();
  m_own_editor.reset();
  m_editor = nullptr;
  m_subscription.release();
  m_scene.reset();
  m_document = std::move(document);
  if(m_document) {
    m_level = 1;
    m_scene = std::make_unique<Scene>(*m_document, StripRasterizer::TOLERANCE);
    m_scene->update();
    get_renderer().set_drawing(m_scene->get_drawing());
    m_subscription = m_document->subscribe([this] (const auto&) {
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

void DocumentView::zoom_to(const Rect& region) {
  m_is_fitting = false;
  frame(region);
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
  }
  return Canvas::event(event);
}

void DocumentView::render(QRhiCommandBuffer* buffer) {
  if(m_scene && !m_scene->is_current()) {
    auto region = Rect(-m_offset.m_x / m_zoom, -m_offset.m_y / m_zoom,
      width() / m_zoom, height() / m_zoom);
    get_renderer().invalidate(m_scene->update(region, SCENE_BUDGET));
    if(!m_scene->is_current()) {
      update();
    }
  }
  if(m_tools) {
    get_renderer().set_overlay(m_tools->draw());
  }
  Canvas::render(buffer);
}

void DocumentView::frame(const Rect& region) {
  auto width = static_cast<double>(this->width());
  auto height = static_cast<double>(this->height());
  if(width > 0 && height > 0) {
    auto zoom = std::numeric_limits<double>::infinity();
    if(region.m_width > 0) {
      zoom = std::max(width - 2 * FIT_MARGIN, 1.0) / region.m_width;
    }
    if(region.m_height > 0) {
      zoom = std::min(zoom,
        std::max(height - 2 * FIT_MARGIN, 1.0) / region.m_height);
    }
    if(std::isfinite(zoom)) {
      m_zoom = std::clamp(zoom, MINIMUM_ZOOM, MAXIMUM_ZOOM);
    }
    auto center = region.get_center();
    m_offset = Point(width / 2 - m_zoom * center.m_x,
      height / 2 - m_zoom * center.m_y);
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
  update();
}

void DocumentView::update_cursor() {
  if(m_is_panning) {
    setCursor(Qt::ClosedHandCursor);
    return;
  } else if(m_is_space_pressed) {
    setCursor(Qt::OpenHandCursor);
    return;
  }
  auto cursor = ToolCursor();
  if(m_tools) {
    cursor = m_tools->get_cursor();
  }
  if(cursor.m_kind == CursorKind::RESIZE) {
    setCursor(get_resize_shape(cursor.m_angle));
  } else if(cursor.m_kind == CursorKind::TEXT) {
    setCursor(Qt::IBeamCursor);
  } else if(cursor.m_kind == CursorKind::ROTATE) {
    if(!m_rotate_cursor) {
      m_rotate_cursor = make_rotate_cursor();
    }
    setCursor(*m_rotate_cursor);
  } else if(cursor.m_kind == CursorKind::CROSSHAIR) {
    setCursor(Qt::CrossCursor);
  } else {
    unsetCursor();
  }
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
  m_tool_subscription.release();
  m_tools.reset();
  m_editor = editor;
  get_renderer().set_overlay(CanvasOverlay());
  if(editor != m_own_editor.get()) {
    m_own_editor.reset();
  }
  update_cursor();
  if(!editor) {
    return;
  }
  m_tools = std::make_unique<ToolManager>(*editor);
  m_tool_subscription = m_tools->subscribe([this] {
    update();
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

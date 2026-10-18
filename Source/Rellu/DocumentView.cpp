#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "DocumentView.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_ZOOM = 1.0 / 64;
  constexpr auto MAXIMUM_ZOOM = 256.0;
  constexpr auto FIT_MARGIN = 24.0;
  constexpr auto WHEEL_ZOOM = 1.0015;

  Rect get_draw_bounds(const DrawList& list) {
    auto bounds = Rect(0, 0, 0, 0);
    for(auto& command : list) {
      auto fill = std::get_if<FillCommand>(&command);
      if(!fill) {
        continue;
      }
      auto box = get_bounds(fill->m_path, fill->m_transform);
      if(fill->m_clip) {
        box = intersect(box, *fill->m_clip);
      }
      bounds = unite(bounds, box);
    }
    return bounds;
  }

  double get_level(double zoom) {
    return std::exp2(std::ceil(std::log2(zoom)));
  }
}

DocumentView::DocumentView(QWidget* parent)
  : Canvas(parent),
    m_bounds(0, 0, 0, 0),
    m_zoom(1),
    m_offset(0, 0),
    m_level(1),
    m_is_fitting(false),
    m_is_space_pressed(false),
    m_is_panning(false) {
  setFocusPolicy(Qt::StrongFocus);
}

DocumentView::~DocumentView() = default;

const Document* DocumentView::get_document() const {
  return m_document.get();
}

void DocumentView::set_document(std::unique_ptr<Document> document) {
  get_renderer().set_draw_list(nullptr);
  m_builder.reset();
  m_document = std::move(document);
  if(m_document) {
    m_builder = std::make_unique<SceneBuilder>(
      *m_document, StripRasterizer::TOLERANCE);
    m_level = 1;
    build();
    m_bounds = get_draw_bounds(*get_renderer().get_draw_list());
  } else {
    m_bounds = Rect(0, 0, 0, 0);
  }
  fit();
}

void DocumentView::fit() {
  m_is_fitting = true;
  auto width = static_cast<double>(this->width());
  auto height = static_cast<double>(this->height());
  if(m_bounds.is_empty() || width <= 0 || height <= 0) {
    m_zoom = 1;
    m_offset = Point(0, 0);
  } else {
    m_zoom = std::clamp(std::min(
      std::max(width - 2 * FIT_MARGIN, 1.0) / m_bounds.m_width,
      std::max(height - 2 * FIT_MARGIN, 1.0) / m_bounds.m_height),
      MINIMUM_ZOOM, MAXIMUM_ZOOM);
    auto center = m_bounds.get_center();
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
  Canvas::mouseReleaseEvent(event);
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
  if(event->key() == Qt::Key_Space) {
    if(!event->isAutoRepeat()) {
      m_is_space_pressed = true;
      update_cursor();
    }
    event->accept();
    return;
  }
  Canvas::keyPressEvent(event);
}

void DocumentView::keyReleaseEvent(QKeyEvent* event) {
  if(event->key() == Qt::Key_Space) {
    if(!event->isAutoRepeat()) {
      m_is_space_pressed = false;
      update_cursor();
    }
    event->accept();
    return;
  }
  Canvas::keyReleaseEvent(event);
}

void DocumentView::resizeEvent(QResizeEvent* event) {
  Canvas::resizeEvent(event);
  if(m_is_fitting) {
    fit();
  }
}

void DocumentView::build() {
  if(!m_builder) {
    return;
  }
  m_builder->set_tolerance(StripRasterizer::TOLERANCE / m_level);
  get_renderer().set_draw_list(
    std::make_shared<const DrawList>(m_builder->build()));
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
    build();
  }
  get_renderer().set_view(
    make_translation(m_offset.m_x, m_offset.m_y) * make_scale(m_zoom, m_zoom));
  update();
}

void DocumentView::update_cursor() {
  if(m_is_panning) {
    setCursor(Qt::ClosedHandCursor);
  } else if(m_is_space_pressed) {
    setCursor(Qt::OpenHandCursor);
  } else {
    unsetCursor();
  }
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
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

  double get_level(double zoom) {
    return std::exp2(std::ceil(std::log2(zoom)));
  }
}

DocumentView::DocumentView(QWidget* parent)
  : Canvas(parent),
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
  fit();
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

void DocumentView::render(QRhiCommandBuffer* buffer) {
  if(m_scene && !m_scene->is_current()) {
    auto region = Rect(-m_offset.m_x / m_zoom, -m_offset.m_y / m_zoom,
      width() / m_zoom, height() / m_zoom);
    get_renderer().invalidate(m_scene->update(region, SCENE_BUDGET));
    if(!m_scene->is_current()) {
      update();
    }
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

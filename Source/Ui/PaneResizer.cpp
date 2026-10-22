#include <cmath>
#include <utility>
#include <QEvent>
#include <QMouseEvent>
#include "Rellu/Ui/PaneResizer.hpp"

using namespace Rellu;

PaneResizer::PaneResizer(PaneFrame& pane, QWidget* parent)
    : QWidget(parent),
      m_pane(&pane),
      m_is_pressed(false),
      m_is_dragging(false),
      m_press(0),
      m_origin(0),
      m_width(0) {
  setMouseTracking(true);
  setAttribute(Qt::WA_NoSystemBackground);
  pane.installEventFilter(this);
  if(parent) {
    parent->installEventFilter(this);
  }
  place();
}

void PaneResizer::set_width_handler(WidthHandler handler) {
  m_handler = std::move(handler);
}

void PaneResizer::set_width_provider(WidthProvider provider) {
  m_provider = std::move(provider);
}

void PaneResizer::place() {
  if(!m_pane || !m_pane->isVisibleTo(parentWidget())) {
    hide();
    return;
  }
  auto ratio = devicePixelRatio();
  auto edge = m_pane->get_device_width();
  auto band = static_cast<int>(std::lround(BAND_WIDTH * ratio));
  auto left = static_cast<int>(std::floor((edge - band) / ratio));
  auto right = static_cast<int>(std::ceil(edge / ratio));
  auto x = m_pane->geometry().x();
  setGeometry(x + left, 0, std::max(right - left, 0),
    parentWidget()->height());
  show();
  raise();
}

bool PaneResizer::is_on_band(QPointF position) const {
  if(!m_pane) {
    return false;
  }
  auto ratio = devicePixelRatio();
  auto offset = geometry().x() - m_pane->geometry().x() + position.x();
  auto device = static_cast<int>(std::floor(offset * ratio + 1e-6));
  auto edge = m_pane->get_device_width();
  auto band = static_cast<int>(std::lround(BAND_WIDTH * ratio));
  return device >= edge - band && device < edge;
}

bool PaneResizer::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_pane || watched == parentWidget()) {
    auto type = event->type();
    if(type == QEvent::Resize || type == QEvent::Move ||
        type == QEvent::Show || type == QEvent::Hide) {
      place();
    }
  }
  return QWidget::eventFilter(watched, event);
}

void PaneResizer::mousePressEvent(QMouseEvent* event) {
  if(event->button() != Qt::LeftButton || !m_pane ||
      !is_on_band(event->position())) {
    event->ignore();
    return;
  }
  m_is_pressed = true;
  m_is_dragging = false;
  m_press = event->globalPosition().x();
  if(m_provider) {
    m_width = m_provider();
  } else {
    m_width = m_pane->width();
  }
  event->accept();
}

void PaneResizer::mouseMoveEvent(QMouseEvent* event) {
  if(!m_is_pressed) {
    if(is_on_band(event->position())) {
      setCursor(Qt::SizeHorCursor);
    } else {
      setCursor(Qt::ArrowCursor);
    }
    return;
  }
  auto x = event->globalPosition().x();
  if(!m_is_dragging) {
    if(std::abs(x - m_press) * devicePixelRatio() < DEAD_ZONE) {
      return;
    }
    m_is_dragging = true;
    m_origin = x;
    return;
  }
  if(m_handler) {
    m_handler(m_width + (x - m_origin));
  }
}

void PaneResizer::leaveEvent(QEvent*) {
  if(!m_is_pressed) {
    unsetCursor();
  }
}

void PaneResizer::mouseReleaseEvent(QMouseEvent* event) {
  if(event->button() == Qt::LeftButton) {
    m_is_pressed = false;
    m_is_dragging = false;
  }
}

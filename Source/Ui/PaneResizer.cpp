#include <cmath>
#include <utility>
#include <QEvent>
#include <QMouseEvent>
#include "Rellu/Ui/PaneResizer.hpp"

using namespace Rellu;

PaneResizer::PaneResizer(QWidget& pane, QWidget* parent)
    : QWidget(parent),
      m_pane(&pane),
      m_is_pressed(false),
      m_is_dragging(false),
      m_press(0),
      m_origin(0),
      m_width(0) {
  setCursor(Qt::SizeHorCursor);
  setAttribute(Qt::WA_NoSystemBackground);
  pane.installEventFilter(this);
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
  auto geometry = m_pane->geometry();
  setGeometry(geometry.x() + geometry.width() - BAND_WIDTH, geometry.y(),
    BAND_WIDTH, geometry.height());
  show();
  raise();
}

bool PaneResizer::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_pane) {
    auto type = event->type();
    if(type == QEvent::Resize || type == QEvent::Move ||
        type == QEvent::Show || type == QEvent::Hide) {
      place();
    }
  }
  return QWidget::eventFilter(watched, event);
}

void PaneResizer::mousePressEvent(QMouseEvent* event) {
  if(event->button() != Qt::LeftButton || !m_pane) {
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

void PaneResizer::mouseReleaseEvent(QMouseEvent* event) {
  if(event->button() == Qt::LeftButton) {
    m_is_pressed = false;
    m_is_dragging = false;
  }
}

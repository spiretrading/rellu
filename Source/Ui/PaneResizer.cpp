#include <cmath>
#include <utility>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
#include "Rellu/Ui/PaneResizer.hpp"

using namespace Rellu;

namespace {
  class EnterFilter : public QObject {
    public:
      EnterFilter(std::function<void ()> callback, QObject* parent)
        : QObject(parent),
          m_callback(std::move(callback)) {}

    protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if(event->type() == QEvent::Enter) {
          m_callback();
        }
        return QObject::eventFilter(watched, event);
      }

    private:
      std::function<void ()> m_callback;
  };
}

PaneResizer::PaneResizer(PaneFrame& pane, QWidget* parent)
    : QWidget(parent),
      m_pane(&pane),
      m_enter_filter(nullptr),
      m_is_pressed(false),
      m_is_dragging(false),
      m_has_parent_cursor(false),
      m_press(0),
      m_origin(0),
      m_width(0) {
  setMouseTracking(true);
  setAttribute(Qt::WA_NoSystemBackground);
  m_enter_filter = new EnterFilter([this] {
    if(!m_is_pressed) {
      set_parent_cursor(false);
    }
  }, this);
  pane.installEventFilter(this);
  if(parent) {
    parent->setMouseTracking(true);
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
  if(!m_pane || !parentWidget()->isAncestorOf(m_pane) ||
      !m_pane->isVisibleTo(parentWidget())) {
    hide();
    return;
  }
  auto ratio = devicePixelRatio();
  auto edge = m_pane->get_device_width();
  auto band = static_cast<int>(std::lround(BAND_WIDTH * ratio));
  auto left = static_cast<int>(std::floor((edge - band) / ratio));
  auto right = static_cast<int>(std::ceil(edge / ratio));
  auto position = get_pane_position();
  setGeometry(position.x() + left, position.y(), std::max(right - left, 0),
    std::max(parentWidget()->height() - position.y(), 0));
  show();
  raise();
}

bool PaneResizer::is_on_band(QPointF position) const {
  if(!m_pane || !parentWidget()->isAncestorOf(m_pane)) {
    return false;
  }
  auto ratio = devicePixelRatio();
  auto offset = geometry().x() - get_pane_position().x() + position.x();
  auto device = static_cast<int>(std::floor(offset * ratio + 1e-6));
  auto edge = m_pane->get_device_width();
  auto band = static_cast<int>(std::lround(BAND_WIDTH * ratio));
  return device >= edge - band && device < edge;
}

QPoint PaneResizer::get_pane_position() const {
  return m_pane->mapTo(parentWidget(), QPoint(0, 0));
}

void PaneResizer::update_cursor(QPointF position) {
  if(is_on_band(position)) {
    setCursor(Qt::SizeHorCursor);
  } else {
    setCursor(Qt::ArrowCursor);
  }
}

bool PaneResizer::event(QEvent* event) {
  if(event->type() == QEvent::Enter && !m_is_pressed) {
    update_cursor(static_cast<QEnterEvent*>(event)->position());
  }
  return QWidget::event(event);
}

bool PaneResizer::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_pane || watched == parentWidget()) {
    auto type = event->type();
    if(type == QEvent::Resize || type == QEvent::Move ||
        type == QEvent::Show || type == QEvent::Hide) {
      place();
    }
  }
  if(watched == parentWidget() && handle_parent_event(*event)) {
    return true;
  }
  return QWidget::eventFilter(watched, event);
}

void PaneResizer::mousePressEvent(QMouseEvent* event) {
  if(event->button() != Qt::LeftButton || !m_pane ||
      !is_on_band(event->position())) {
    event->ignore();
    return;
  }
  press(event->globalPosition().x());
  event->accept();
}

void PaneResizer::mouseMoveEvent(QMouseEvent* event) {
  if(!m_is_pressed) {
    update_cursor(event->position());
    return;
  }
  drag(event->globalPosition().x());
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

void PaneResizer::press(double x) {
  m_is_pressed = true;
  m_is_dragging = false;
  m_press = x;
  if(m_provider) {
    m_width = m_provider();
  } else {
    m_width = m_pane->width();
  }
}

void PaneResizer::drag(double x) {
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

void PaneResizer::set_parent_cursor(bool is_band) {
  if(is_band == m_has_parent_cursor) {
    return;
  }
  m_has_parent_cursor = is_band;
  if(is_band) {
    parentWidget()->setCursor(Qt::SizeHorCursor);
    QCoreApplication::instance()->installEventFilter(m_enter_filter);
  } else {
    parentWidget()->unsetCursor();
    QCoreApplication::instance()->removeEventFilter(m_enter_filter);
  }
}

bool PaneResizer::handle_parent_event(QEvent& event) {
  auto type = event.type();
  if(type == QEvent::Leave) {
    if(!m_is_pressed) {
      set_parent_cursor(false);
    }
    return false;
  } else if(type != QEvent::MouseButtonPress &&
      type != QEvent::MouseMove && type != QEvent::MouseButtonRelease) {
    return false;
  }
  auto& mouse = static_cast<QMouseEvent&>(event);
  auto position = mouse.position() - QPointF(pos());
  auto is_band = m_pane && isVisible() && position.y() >= 0 &&
    is_on_band(position);
  auto x = mouse.globalPosition().x();
  if(type == QEvent::MouseButtonPress) {
    if(mouse.button() != Qt::LeftButton || m_is_pressed || !is_band) {
      return false;
    }
    set_parent_cursor(true);
    press(x);
    return true;
  } else if(type == QEvent::MouseMove) {
    if(m_is_pressed && m_has_parent_cursor) {
      drag(x);
      return true;
    }
    if(!m_is_pressed) {
      set_parent_cursor(is_band);
    }
    return false;
  }
  if(mouse.button() != Qt::LeftButton || !m_is_pressed ||
      !m_has_parent_cursor) {
    return false;
  }
  m_is_pressed = false;
  m_is_dragging = false;
  set_parent_cursor(is_band);
  return true;
}

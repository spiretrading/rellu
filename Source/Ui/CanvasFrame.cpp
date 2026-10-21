#include <algorithm>
#include <cmath>
#include <QEvent>
#include <QPainter>
#include <QRegion>
#include "Rellu/Ui/CanvasFrame.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_OFFSET = 8;
  constexpr auto TOLERANCE = 1e-6;
}

int Rellu::align_to_device_pixels(int position, double ratio) {
  for(auto offset = 0; offset != MAXIMUM_OFFSET; ++offset) {
    auto device = (position + offset) * ratio;
    if(std::abs(device - std::round(device)) < TOLERANCE) {
      return position + offset;
    }
  }
  return position;
}

CanvasFrame::CanvasFrame(Canvas& canvas, QWidget* parent)
    : QWidget(parent),
      m_canvas(&canvas) {
  m_canvas->setParent(this);
  place();
}

Canvas& CanvasFrame::get_canvas() const {
  return *m_canvas;
}

bool CanvasFrame::event(QEvent* event) {
  auto result = QWidget::event(event);
  auto type = event->type();
  if(type == QEvent::Move || type == QEvent::Resize ||
      type == QEvent::Show || type == QEvent::ParentChange ||
      type == QEvent::DevicePixelRatioChange) {
    place();
  }
  return result;
}

void CanvasFrame::paintEvent(QPaintEvent* event) {
  auto painter = QPainter(this);
  auto region = QRegion(rect()).subtracted(m_canvas->geometry());
  for(const auto& area : region) {
    painter.fillRect(area, m_canvas->get_renderer().get_background());
  }
}

void CanvasFrame::place() {
  auto ratio = devicePixelRatio();
  auto origin = mapTo(window(), QPoint(0, 0));
  auto left = align_to_device_pixels(origin.x(), ratio) - origin.x();
  auto top = align_to_device_pixels(origin.y(), ratio) - origin.y();
  auto geometry = QRect(left, top, std::max(0, width() - left),
    std::max(0, height() - top));
  if(m_canvas->geometry() != geometry) {
    m_canvas->setGeometry(geometry);
    update();
  }
}

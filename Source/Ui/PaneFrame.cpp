#include <algorithm>
#include <cmath>
#include <QPainter>
#include "Rellu/Ui/PaneFrame.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 1e-6;
}

PaneFrame::PaneFrame(QWidget* parent)
    : QStackedWidget(parent),
      m_device_width(0) {
  setFrameShape(QFrame::NoFrame);
}

int PaneFrame::get_device_width() const {
  return m_device_width;
}

void PaneFrame::set_device_width(int width) {
  m_device_width = std::max(width, 0);
  auto ratio = devicePixelRatio();
  auto logical = static_cast<int>(
    std::ceil(m_device_width / ratio - TOLERANCE));
  auto content = static_cast<int>(std::floor(
    (m_device_width - get_border_device_width()) / ratio + TOLERANCE));
  setContentsMargins(0, 0, std::max(logical - content, 0), 0);
  setFixedWidth(logical);
  update();
}

void PaneFrame::paintEvent(QPaintEvent*) {
  auto painter = QPainter(this);
  auto ratio = devicePixelRatio();
  auto border = (m_device_width - get_border_device_width()) / ratio;
  painter.fillRect(QRectF(0, 0, border, height()), BACKGROUND);
  painter.fillRect(
    QRectF(border, 0, width() - border, height()), BORDER);
}

int PaneFrame::get_border_device_width() const {
  return static_cast<int>(std::lround(BORDER_WIDTH * devicePixelRatio()));
}

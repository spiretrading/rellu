#include <array>
#include <functional>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto ICON_SIZE = 16;
  const auto INK = QColor(0x4A, 0x4F, 0x57);
  const auto FAINT_INK = QColor(0xA8, 0xAD, 0xB4);
  const auto ACCENT = QColor(0x2F, 0x6F, 0xEB);

  QIcon make_icon(std::function<void (QPainter&)> paint) {
    auto icon = QIcon();
    for(auto scale : {1, 2, 3}) {
      auto pixmap = QPixmap(ICON_SIZE * scale, ICON_SIZE * scale);
      pixmap.fill(Qt::transparent);
      auto painter = QPainter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.scale(scale, scale);
      paint(painter);
      painter.end();
      icon.addPixmap(pixmap);
    }
    return icon;
  }

  QPen make_pen(const QColor& color, double width = 1.2) {
    auto pen = QPen(color, width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    return pen;
  }

  void paint_kind(QPainter& painter, NodeKind kind) {
    painter.setPen(make_pen(INK));
    painter.setBrush(Qt::NoBrush);
    switch(kind) {
      case NodeKind::ARTBOARD:
        painter.drawRect(QRectF(3.5, 4.5, 10, 9));
        painter.drawLine(QPointF(3.5, 2), QPointF(3.5, 3));
        painter.drawLine(QPointF(1, 4.5), QPointF(2, 4.5));
        break;
      case NodeKind::GROUP:
      {
        auto path = QPainterPath();
        path.moveTo(2.5, 4.5);
        path.lineTo(6.5, 4.5);
        path.lineTo(8, 6);
        path.lineTo(13.5, 6);
        path.lineTo(13.5, 12.5);
        path.lineTo(2.5, 12.5);
        path.closeSubpath();
        painter.drawPath(path);
        break;
      }
      case NodeKind::SHAPE:
        painter.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1.5, 1.5);
        break;
      case NodeKind::TEXT:
        painter.drawLine(QPointF(4, 4), QPointF(12, 4));
        painter.drawLine(QPointF(8, 4), QPointF(8, 13));
        painter.drawLine(QPointF(6, 13), QPointF(10, 13));
        break;
      case NodeKind::REFERENCE:
        painter.drawEllipse(QRectF(3.5, 3.5, 9, 9));
        break;
    }
  }

  void paint_link(QPainter& painter) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QRectF(8.5, 8.5, 7.5, 7.5));
    painter.setPen(make_pen(ACCENT, 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(10.5, 13.75), QPointF(14, 10.25));
    painter.drawLine(QPointF(11.25, 10.25), QPointF(14, 10.25));
    painter.drawLine(QPointF(14, 10.25), QPointF(14, 13));
  }

  void paint_eye(QPainter& painter, bool is_open) {
    auto color = is_open ? INK : FAINT_INK;
    painter.setPen(make_pen(color));
    painter.setBrush(Qt::NoBrush);
    auto path = QPainterPath();
    path.moveTo(2, 8);
    path.quadTo(8, 1.5, 14, 8);
    path.quadTo(8, 14.5, 2, 8);
    painter.drawPath(path);
    if(is_open) {
      painter.setBrush(color);
      painter.drawEllipse(QPointF(8, 8), 1.8, 1.8);
    } else {
      painter.drawLine(QPointF(3, 13), QPointF(13, 3));
    }
  }

  void paint_lock(QPainter& painter) {
    painter.setPen(make_pen(INK));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(3.5, 7.5, 9, 6.5), 1, 1);
    auto path = QPainterPath();
    path.moveTo(5.5, 7.5);
    path.lineTo(5.5, 5.5);
    path.arcTo(QRectF(5.5, 2.5, 5, 6), 180, -180);
    path.lineTo(10.5, 7.5);
    painter.drawPath(path);
  }
}

const QIcon& Rellu::get_kind_icon(NodeKind kind, bool is_reference) {
  static auto icons = [] {
    auto icons = std::array<QIcon, 10>();
    for(auto i = 0; i != 5; ++i) {
      auto kind = static_cast<NodeKind>(i);
      icons[2 * i] = make_icon([&] (auto& painter) {
        paint_kind(painter, kind);
      });
      icons[2 * i + 1] = make_icon([&] (auto& painter) {
        paint_kind(painter, kind);
        paint_link(painter);
      });
    }
    return icons;
  }();
  return icons[2 * static_cast<int>(kind) + (is_reference ? 1 : 0)];
}

const QIcon& Rellu::get_visibility_icon(bool is_visible) {
  static auto visible = make_icon([] (auto& painter) {
    paint_eye(painter, true);
  });
  static auto hidden = make_icon([] (auto& painter) {
    paint_eye(painter, false);
  });
  if(is_visible) {
    return visible;
  }
  return hidden;
}

const QIcon& Rellu::get_lock_icon() {
  static auto icon = make_icon([] (auto& painter) {
    paint_lock(painter);
  });
  return icon;
}

#include <QBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include "HyperlinkStatus.hpp"

using namespace Rellu;

namespace {
  constexpr auto ICON_SIZE = 16;
  constexpr auto ICON_SCALE = 2;
  const auto VALID_COLOR = QColor(0x26, 0x8E, 0x6C);
  const auto INVALID_COLOR = QColor(0xD7, 0x37, 0x3F);

  QPixmap make_pixmap(auto paint) {
    auto pixmap = QPixmap(ICON_SIZE * ICON_SCALE, ICON_SIZE * ICON_SCALE);
    pixmap.setDevicePixelRatio(ICON_SCALE);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    paint(painter);
    painter.end();
    return pixmap;
  }

  QPixmap make_check() {
    return make_pixmap([] (QPainter& painter) {
      auto pen = QPen(VALID_COLOR, 1.8);
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      painter.setPen(pen);
      painter.drawPolyline(QList<QPointF>({QPointF(3.5, 8.5),
        QPointF(6.5, 11.5), QPointF(12.5, 4.5)}));
    });
  }

  QPixmap make_warning() {
    return make_pixmap([] (QPainter& painter) {
      auto triangle = QPainterPath();
      triangle.moveTo(8, 1.5);
      triangle.lineTo(15, 14);
      triangle.lineTo(1, 14);
      triangle.closeSubpath();
      painter.setPen(Qt::NoPen);
      painter.setBrush(INVALID_COLOR);
      painter.drawPath(triangle);
      painter.setBrush(Qt::white);
      painter.drawRect(QRectF(7.25, 5.5, 1.5, 4.5));
      painter.drawEllipse(QPointF(8, 11.75), 0.9, 0.9);
    });
  }
}

QWidget* Rellu::add_hyperlink_status(QWidget* row, QLineEdit& field) {
  auto container = new QWidget();
  auto layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);
  layout->addWidget(row);
  auto icon = new QLabel();
  icon->setObjectName("hyperlink_status");
  icon->setFixedSize(ICON_SIZE + 4, ICON_SIZE + 4);
  icon->setAlignment(Qt::AlignCenter);
  if(auto row_layout = qobject_cast<QBoxLayout*>(row->layout())) {
    row_layout->addWidget(icon);
  }
  auto message = new QLabel(HYPERLINK_PROTOCOL_MESSAGE);
  message->setObjectName("hyperlink_message");
  message->setWordWrap(true);
  message->setStyleSheet(
    QString("color: %1;").arg(INVALID_COLOR.name()));
  layout->addWidget(message);
  auto update = [icon, message, field = &field] {
    auto url = field->text();
    field->setToolTip(url);
    if(url.trimmed().isEmpty()) {
      icon->clear();
      message->hide();
    } else if(is_valid_hyperlink(url)) {
      icon->setPixmap(make_check());
      message->hide();
    } else {
      icon->setPixmap(make_warning());
      message->show();
    }
  };
  QObject::connect(&field, &QLineEdit::textChanged, icon,
    [=, line = &field] {
      if(!line->isModified()) {
        update();
      }
    });
  QObject::connect(&field, &QLineEdit::editingFinished, icon, update);
  update();
  return container;
}

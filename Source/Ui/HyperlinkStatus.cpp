#include <QBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include "Rellu/Ui/Translation.hpp"
#include "HyperlinkStatus.hpp"

using namespace Rellu;

namespace {
  constexpr auto ICON_SIZE = 16;
  constexpr auto ICON_SCALE = 2;
  const auto VALID_COLOR = QColor(0x26, 0x8E, 0x6C);
  const auto INVALID_COLOR = QColor(0xCB, 0x6D, 0x0F);

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

QString Rellu::get_hyperlink_message(HyperlinkValidity validity) {
  if(validity == HyperlinkValidity::INVALID_PROTOCOL) {
    return translate("inspector", "URL protocol must be https:// or mailto:");
  } else if(validity == HyperlinkValidity::INVALID_DOMAIN) {
    return translate("inspector", "URL includes an invalid domain");
  } else if(validity == HyperlinkValidity::INVALID) {
    return translate("inspector", "Invalid URL");
  }
  return QString();
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
  auto message = new QLabel();
  message->setObjectName("hyperlink_message");
  message->setWordWrap(true);
  message->setStyleSheet(QString("color: %1; font-style: italic;").arg(
    INVALID_COLOR.name()));
  layout->addWidget(message);
  auto update = [icon, message, field = &field] {
    auto url = field->text();
    field->setToolTip(url);
    auto validity = check_hyperlink(url);
    if(validity == HyperlinkValidity::EMPTY) {
      icon->clear();
    } else if(validity == HyperlinkValidity::VALID) {
      icon->setPixmap(make_check());
    } else {
      icon->setPixmap(make_warning());
    }
    message->setText(get_hyperlink_message(validity));
    message->setHidden(message->text().isEmpty());
    if(message->isHidden()) {
      field->setStyleSheet(QString());
    } else {
      field->setStyleSheet(QString("border-bottom: 1px solid %1;").arg(
        INVALID_COLOR.name()));
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

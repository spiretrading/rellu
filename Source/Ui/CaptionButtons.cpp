#include <QColor>
#include <QEnterEvent>
#include <QHideEvent>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QShowEvent>
#include "Rellu/Ui/CaptionButtons.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

CaptionButton::CaptionButton(CaptionKind kind, const QString& tip)
    : m_kind(kind),
      m_is_hovered(false) {
  setObjectName("caption_button");
  setFocusPolicy(Qt::NoFocus);
  setFixedSize(WIDTH, HEIGHT);
  setToolTip(translate("window", tip));
  setAccessibleName(toolTip());
  setAttribute(Qt::WA_Hover);
}

bool CaptionButton::is_hovered() const {
  return m_is_hovered;
}

void CaptionButton::enterEvent(QEnterEvent* event) {
  m_is_hovered = true;
  update();
  QAbstractButton::enterEvent(event);
}

void CaptionButton::leaveEvent(QEvent* event) {
  m_is_hovered = false;
  update();
  QAbstractButton::leaveEvent(event);
}

void CaptionButton::showEvent(QShowEvent* event) {
  reset();
  QAbstractButton::showEvent(event);
}

void CaptionButton::hideEvent(QHideEvent* event) {
  reset();
  QAbstractButton::hideEvent(event);
}

void CaptionButton::reset() {
  m_is_hovered = false;
  setDown(false);
  update();
}

void CaptionButton::paintEvent(QPaintEvent*) {
  auto painter = QPainter(this);
  auto ink = QColor(0x3F, 0x3F, 0x3F);
  if(m_kind == CaptionKind::CLOSE && (m_is_hovered || isDown())) {
    painter.fillRect(rect(), isDown() ? QColor(0xF1, 0x70, 0x7A) :
      QColor(0xE8, 0x11, 0x23));
    ink = Qt::white;
  } else if(isDown()) {
    painter.fillRect(rect(), QColor(0xC6, 0xC6, 0xC6));
  } else if(m_is_hovered) {
    painter.fillRect(rect(), QColor(0xD6, 0xD6, 0xD6));
  }
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(ink, 1));
  painter.setBrush(Qt::NoBrush);
  auto center = QPointF(width() / 2.0, height() / 2.0);
  if(m_kind == CaptionKind::MINIMIZE) {
    painter.drawLine(QPointF(center.x() - 5, center.y() + 0.5),
      QPointF(center.x() + 5, center.y() + 0.5));
  } else if(m_kind == CaptionKind::MAXIMIZE) {
    if(window()->isMaximized()) {
      painter.drawRect(QRectF(center.x() - 5, center.y() - 3, 8, 8));
      painter.drawPolyline(QList<QPointF>({
        QPointF(center.x() - 3, center.y() - 5),
        QPointF(center.x() + 5, center.y() - 5),
        QPointF(center.x() + 5, center.y() + 3)}));
    } else {
      painter.drawRect(QRectF(center.x() - 5, center.y() - 5, 10, 10));
    }
  } else {
    painter.drawLine(QPointF(center.x() - 5, center.y() - 5),
      QPointF(center.x() + 5, center.y() + 5));
    painter.drawLine(QPointF(center.x() + 5, center.y() - 5),
      QPointF(center.x() - 5, center.y() + 5));
  }
}

std::vector<CaptionButton*> Rellu::make_caption_buttons(
    QWidget& window, QWidget& parent) {
  auto minimize = new CaptionButton(CaptionKind::MINIMIZE, "Minimize");
  QObject::connect(minimize, &QAbstractButton::clicked, &window, [&window] {
    window.showMinimized();
  });
  auto maximize = new CaptionButton(CaptionKind::MAXIMIZE, "Maximize");
  QObject::connect(maximize, &QAbstractButton::clicked, &window, [&window] {
    if(window.isMaximized()) {
      window.showNormal();
    } else {
      window.showMaximized();
    }
  });
  auto close = new CaptionButton(CaptionKind::CLOSE, "Close");
  QObject::connect(close, &QAbstractButton::clicked, &window, [&window] {
    window.close();
  });
  auto buttons = std::vector<CaptionButton*>({minimize, maximize, close});
  for(auto button : buttons) {
    button->setParent(&parent);
  }
  return buttons;
}

void Rellu::update_maximize_tip(QAbstractButton& button, bool is_maximized) {
  auto tip = translate("window",
    is_maximized ? QString("Restore Down") : QString("Maximize"));
  button.setToolTip(tip);
  button.setAccessibleName(tip);
  button.update();
}

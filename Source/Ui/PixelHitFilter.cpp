#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>
#include "Rellu/Ui/PixelHitFilter.hpp"

using namespace Rellu;

namespace {
  QWidget* find_root(QObject& object) {
    if(!object.isWindowType()) {
      return nullptr;
    }
    auto& window = static_cast<QWindow&>(object);
    if(!window.handle()) {
      return nullptr;
    }
    return QWidget::find(window.winId());
  }

  bool is_hover(const QMouseEvent& event) {
    if(event.type() == QEvent::MouseMove) {
      return event.buttons() == Qt::NoButton;
    }
    if(event.type() == QEvent::MouseButtonPress ||
        event.type() == QEvent::MouseButtonDblClick) {
      return event.buttons() == event.button() &&
        !(event.flags() & Qt::MouseEventCreatedDoubleClick);
    }
    return false;
  }
}

PixelHitFilter::PixelHitFilter(QObject* parent)
  : QObject(parent) {}

bool PixelHitFilter::eventFilter(QObject* watched, QEvent* event) {
  auto type = event->type();
  if(type != QEvent::MouseMove && type != QEvent::MouseButtonPress &&
      type != QEvent::MouseButtonDblClick && type != QEvent::Wheel &&
      type != QEvent::Enter) {
    return QObject::eventFilter(watched, event);
  }
  auto root = find_root(*watched);
  if(!root) {
    return QObject::eventFilter(watched, event);
  }
  if(type == QEvent::Wheel) {
    auto& wheel = static_cast<QWheelEvent&>(*event);
    auto hit = get_pixel_hit(*root, wheel.position());
    if(!hit) {
      return QObject::eventFilter(watched, event);
    }
    auto adjusted = QWheelEvent(*hit, wheel.globalPosition(),
      wheel.pixelDelta(), wheel.angleDelta(), wheel.buttons(),
      wheel.modifiers(), wheel.phase(), wheel.inverted(), wheel.source(),
      wheel.pointingDevice());
    adjusted.setTimestamp(wheel.timestamp());
    QCoreApplication::sendEvent(watched, &adjusted);
    wheel.setAccepted(adjusted.isAccepted());
    return true;
  } else if(type == QEvent::Enter) {
    auto& enter = static_cast<QEnterEvent&>(*event);
    auto hit = get_pixel_hit(*root, enter.position());
    if(!hit) {
      return QObject::eventFilter(watched, event);
    }
    auto adjusted = QEnterEvent(*hit, *hit, enter.globalPosition());
    QCoreApplication::sendEvent(watched, &adjusted);
    return true;
  }
  auto& mouse = static_cast<QMouseEvent&>(*event);
  if(!is_hover(mouse)) {
    return QObject::eventFilter(watched, event);
  }
  auto hit = get_pixel_hit(*root, mouse.position());
  if(!hit) {
    return QObject::eventFilter(watched, event);
  }
  auto adjusted = QMouseEvent(type, *hit, *hit, mouse.globalPosition(),
    mouse.button(), mouse.buttons(), mouse.modifiers(), mouse.source(),
    mouse.pointingDevice());
  adjusted.setTimestamp(mouse.timestamp());
  QCoreApplication::sendEvent(watched, &adjusted);
  mouse.setAccepted(adjusted.isAccepted());
  return true;
}

std::optional<QPointF> Rellu::get_pixel_hit(
    const QWidget& root, QPointF position) {
  auto pixel = QPoint(static_cast<int>(std::floor(position.x())),
    static_cast<int>(std::floor(position.y())));
  if(!root.rect().contains(pixel)) {
    return std::nullopt;
  }
  auto expected = root.childAt(pixel);
  if(root.childAt(position) == expected) {
    return std::nullopt;
  }
  auto& target = expected ? *expected : root;
  auto origin = target.mapTo(&root, QPointF(0, 0));
  auto hit = QPointF(
    std::min(position.x(), origin.x() + target.width() - 1),
    std::min(position.y(), origin.y() + target.height() - 1));
  if(root.childAt(hit) != expected) {
    return std::nullopt;
  }
  return hit;
}

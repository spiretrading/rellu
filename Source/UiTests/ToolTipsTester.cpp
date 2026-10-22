#include <cmath>
#include <cstdlib>
#include <doctest/doctest.h>
#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QImage>
#include <QListView>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QToolTip>
#include <QWidget>
#include "Rellu/Ui/ToolTips.hpp"

using namespace Rellu;

namespace {
  QWidget* find_tip() {
    for(auto widget : QApplication::topLevelWidgets()) {
      if(widget->objectName() == "rellu_tool_tip" && widget->isVisible()) {
        return widget;
      }
    }
    return nullptr;
  }

  QRect find_box(const QImage& image) {
    auto box = QRect();
    for(auto y = 0; y != image.height(); ++y) {
      for(auto x = 0; x != image.width(); ++x) {
        if(image.pixelColor(x, y) == QColor(0xCF, 0xCF, 0xCF)) {
          box |= QRect(x, y, 1, 1);
        }
      }
    }
    return box;
  }

  void hover(QWidget& widget, QPoint position) {
    auto event = QHelpEvent(
      QEvent::ToolTip, position, widget.mapToGlobal(position));
    QCoreApplication::sendEvent(&widget, &event);
  }
}

TEST_SUITE("ToolTips") {
  TEST_CASE("tool_tip_filter") {
    auto filter = ToolTipFilter();
    QCoreApplication::instance()->installEventFilter(&filter);
    auto window = QWidget();
    window.resize(400, 300);
    auto button = new QWidget(&window);
    button->setGeometry(4, 44, 40, 40);
    button->setToolTip("Rectangle (R)");
    set_tool_tip_placement(*button, ToolTipPlacement::RIGHT);
    auto label = new QWidget(&window);
    label->setGeometry(100, 100, 30, 30);
    label->setToolTip("Artboards");
    window.show();
    QCoreApplication::processEvents();
    REQUIRE(get_tool_tip_placement(*label) == ToolTipPlacement::POINTER);
    hover(*button, QPoint(20, 20));
    auto tip = find_tip();
    REQUIRE(tip);
    auto image = tip->grab().toImage();
    auto box = find_box(image).translated(tip->pos());
    auto metrics = QFontMetricsF(QToolTip::font());
    REQUIRE(box.width() == 2 + 16 + static_cast<int>(
      std::ceil(metrics.horizontalAdvance("Rectangle (R)") - 1e-6)));
    REQUIRE(box.height() ==
      std::lround(2 + 12 + metrics.ascent() + metrics.descent()));
    REQUIRE(box.left() == button->mapToGlobal(QPoint(40, 0)).x());
    REQUIRE(std::abs(box.center().y() -
      button->mapToGlobal(QPoint(0, 20)).y()) <= 1);
    auto inside = box.topLeft() - tip->pos() + QPoint(3, 3);
    REQUIRE(image.pixelColor(inside) == QColor(0xF0, 0xF0, 0xF0));
    auto below = QPoint(box.center().x(), box.bottom() + 3) - tip->pos();
    REQUIRE(qAlpha(image.pixel(below)) > 0);
    auto above = QPoint(box.center().x(), box.top() - 3) - tip->pos();
    REQUIRE(qAlpha(image.pixel(above)) < qAlpha(image.pixel(below)));
    hover(*label, QPoint(10, 10));
    tip = find_tip();
    REQUIRE(tip);
    box = find_box(tip->grab().toImage()).translated(tip->pos());
    auto pointer = label->mapToGlobal(QPoint(10, 10));
    REQUIRE(box.left() ==
      static_cast<int>(std::lround(pointer.x() - box.width() / 2.0)));
    REQUIRE(box.top() == pointer.y() + 20);
    REQUIRE(tip->windowFlags().testFlag(Qt::WindowTransparentForInput));
    QCursor::setPos(label->mapToGlobal(QPoint(15, 15)));
    auto leave = QEvent(QEvent::Leave);
    QCoreApplication::sendEvent(label, &leave);
    REQUIRE(find_tip());
    QCursor::setPos(label->mapToGlobal(QPoint(60, 60)));
    QCoreApplication::sendEvent(label, &leave);
    REQUIRE(!find_tip());
    hover(*label, QPoint(10, 10));
    REQUIRE(find_tip());
    auto press = QMouseEvent(QEvent::MouseButtonPress, QPointF(5, 5),
      QPointF(5, 5), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &press);
    REQUIRE(!find_tip());
    QCoreApplication::instance()->removeEventFilter(&filter);
  }

  TEST_CASE("item_view_tool_tips") {
    auto filter = ToolTipFilter();
    QCoreApplication::instance()->installEventFilter(&filter);
    auto model = QStandardItemModel();
    auto item = new QStandardItem("Rectangle");
    item->setToolTip("Hide (Ctrl + ,)");
    model.appendRow(item);
    model.appendRow(new QStandardItem("Ellipse"));
    auto view = QListView();
    view.setModel(&model);
    view.resize(200, 200);
    view.show();
    QCoreApplication::processEvents();
    auto first = view.visualRect(model.index(0, 0)).center();
    hover(*view.viewport(), first);
    REQUIRE(find_tip());
    auto second = view.visualRect(model.index(1, 0)).center();
    auto move = QMouseEvent(QEvent::MouseMove, QPointF(second),
      QPointF(view.viewport()->mapToGlobal(second)), Qt::NoButton,
      Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &move);
    REQUIRE(!find_tip());
    hover(*view.viewport(), second);
    REQUIRE(!find_tip());
    QCoreApplication::instance()->removeEventFilter(&filter);
  }

  TEST_CASE("pointer_tip_position") {
    auto position = [] (double x, double y, int width) {
      return get_pointer_tip_position(
        QPointF(x / 1.5, y / 1.5), QSize(width, 46), 1.5);
    };
    REQUIRE(position(102, 218, 107) == QPoint(48, 248));
    REQUIRE(position(112, 222, 107) == QPoint(57, 252));
    REQUIRE(position(95, 77, 107) == QPoint(41, 107));
    REQUIRE(position(3437, 148, 250).x() == 3312);
    REQUIRE(position(3735, 77, 75) == QPoint(3698, 107));
  }
}

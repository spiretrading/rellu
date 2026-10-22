#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>
#include "Rellu/Ui/PixelHitFilter.hpp"

using namespace Rellu;

namespace {
  class PressWidget : public QWidget {
    public:
      using QWidget::QWidget;

      int m_presses = 0;

    protected:
      void mousePressEvent(QMouseEvent* event) override {
        ++m_presses;
        event->accept();
      }
  };

  void click(QWindow& window, QPointF position) {
    for(auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
      auto buttons = type == QEvent::MouseButtonPress ?
        Qt::MouseButtons(Qt::LeftButton) : Qt::MouseButtons(Qt::NoButton);
      auto event = QMouseEvent(type, position, position,
        window.mapToGlobal(position), Qt::LeftButton, buttons,
        Qt::NoModifier);
      QCoreApplication::sendEvent(&window, &event);
    }
  }
}

TEST_SUITE("PixelHitFilter") {
  TEST_CASE("get_pixel_hit") {
    auto root = QWidget();
    root.resize(100, 100);
    auto left = new QWidget(&root);
    left->setGeometry(0, 0, 50, 100);
    auto right = new QWidget(&root);
    right->setGeometry(50, 0, 50, 100);
    auto corner = new QWidget(right);
    corner->setGeometry(10, 90, 40, 10);
    REQUIRE(!get_pixel_hit(root, QPointF(20.5, 30.5)));
    REQUIRE(!get_pixel_hit(root, QPointF(49, 30)));
    REQUIRE(get_pixel_hit(root, QPointF(49.5, 30.25)) == QPointF(49, 30.25));
    REQUIRE(get_pixel_hit(root, QPointF(80.5, 99.5)) == QPointF(80.5, 99));
    REQUIRE(get_pixel_hit(root, QPointF(99.75, 99.5)) == QPointF(99, 99));
    REQUIRE(!get_pixel_hit(root, QPointF(100.25, 50)));
    REQUIRE(!get_pixel_hit(root, QPointF(-0.5, 50)));
    corner->hide();
    REQUIRE(get_pixel_hit(root, QPointF(99.75, 99.5)) == QPointF(99, 99));
  }

  TEST_CASE("pixel_hit_filter") {
    auto filter = PixelHitFilter();
    QCoreApplication::instance()->installEventFilter(&filter);
    auto root = QWidget();
    root.resize(100, 100);
    auto child = new PressWidget(&root);
    child->setGeometry(0, 50, 100, 50);
    root.show();
    QCoreApplication::processEvents();
    auto window = root.windowHandle();
    REQUIRE(window);
    click(*window, QPointF(99.5, 99.5));
    REQUIRE(child->m_presses == 1);
    click(*window, QPointF(50.5, 70.5));
    REQUIRE(child->m_presses == 2);
    click(*window, QPointF(50.5, 20.5));
    REQUIRE(child->m_presses == 2);
    QCoreApplication::instance()->removeEventFilter(&filter);
    click(*window, QPointF(99.5, 99.5));
    REQUIRE(child->m_presses == 2);
  }
}

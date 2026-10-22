#include <vector>
#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QWidget>
#include "Rellu/Ui/PaneResizer.hpp"

using namespace Rellu;

namespace {
  void send(QWidget& widget, QEvent::Type type, QPointF position, double x,
      Qt::MouseButton button, Qt::MouseButtons buttons) {
    auto global = QPointF(x, 100);
    auto event = QMouseEvent(
      type, position, global, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
  }

  void send(QWidget& widget, QEvent::Type type, double x,
      Qt::MouseButton button, Qt::MouseButtons buttons) {
    send(widget, type, QPointF(1, 5), x, button, buttons);
  }

  void move(QWidget& widget, double x) {
    send(widget, QEvent::MouseMove, x, Qt::NoButton, Qt::LeftButton);
  }
}

TEST_SUITE("PaneResizer") {
  TEST_CASE("pane_resizer") {
    auto parent = QWidget();
    parent.resize(800, 600);
    auto pane = new PaneFrame(&parent);
    pane->move(50, 0);
    pane->set_device_width(400);
    pane->setFixedHeight(580);
    auto resizer = PaneResizer(*pane, &parent);
    parent.show();
    QCoreApplication::processEvents();
    REQUIRE(resizer.isVisible());
    REQUIRE(resizer.geometry() ==
      QRect(450 - PaneResizer::BAND_WIDTH, 0, PaneResizer::BAND_WIDTH, 600));
    REQUIRE(!resizer.is_on_band(QPointF(-0.5, 10)));
    REQUIRE(resizer.is_on_band(QPointF(0, 10)));
    REQUIRE(resizer.is_on_band(QPointF(3.5, 10)));
    REQUIRE(!resizer.is_on_band(QPointF(4, 10)));
    move(resizer, 448);
    REQUIRE(resizer.cursor().shape() == Qt::SizeHorCursor);
    auto widths = std::vector<double>();
    resizer.set_width_handler([&] (auto width) {
      widths.push_back(width);
    });
    resizer.set_width_provider([] {
      return 1222.0 / 3;
    });
    send(resizer, QEvent::MouseButtonPress, 448, Qt::LeftButton,
      Qt::LeftButton);
    for(auto offset = 1; offset <= 4; ++offset) {
      move(resizer, 448 + offset);
    }
    REQUIRE(widths.empty());
    move(resizer, 453);
    move(resizer, 454);
    REQUIRE(widths == std::vector{1222.0 / 3 + 1, 1222.0 / 3 + 2});
    move(resizer, 478);
    REQUIRE(widths.back() == 1222.0 / 3 + 26);
    move(resizer, 440);
    REQUIRE(widths.back() == doctest::Approx(1222.0 / 3 - 12));
    send(resizer, QEvent::MouseButtonRelease, 440, Qt::LeftButton,
      Qt::NoButton);
    move(resizer, 300);
    REQUIRE(widths.size() == 4);
    widths.clear();
    send(resizer, QEvent::MouseButtonPress, 448, Qt::LeftButton,
      Qt::LeftButton);
    move(resizer, 451);
    move(resizer, 454);
    REQUIRE(widths.empty());
    move(resizer, 457);
    REQUIRE(widths == std::vector{1222.0 / 3 + 3});
    send(resizer, QEvent::MouseButtonRelease, 457, Qt::LeftButton,
      Qt::NoButton);
    widths.clear();
    send(resizer, QEvent::MouseButtonPress, 448, Qt::LeftButton,
      Qt::LeftButton);
    move(resizer, 444);
    move(resizer, 443);
    REQUIRE(widths == std::vector{1222.0 / 3 - 1});
    send(resizer, QEvent::MouseButtonRelease, 443, Qt::LeftButton,
      Qt::NoButton);
    widths.clear();
    send(resizer, QEvent::MouseButtonPress, 448, Qt::LeftButton,
      Qt::LeftButton);
    move(resizer, 488);
    REQUIRE(widths.empty());
    move(resizer, 489);
    REQUIRE(widths == std::vector{1222.0 / 3 + 1});
    send(resizer, QEvent::MouseButtonRelease, 489, Qt::LeftButton,
      Qt::NoButton);
    pane->set_device_width(300);
    REQUIRE(resizer.geometry().x() == 350 - PaneResizer::BAND_WIDTH);
    pane->hide();
    REQUIRE(!resizer.isVisible());
  }
  TEST_CASE("pane_resizer_over_ancestor") {
    auto window = QWidget();
    window.resize(800, 600);
    auto body = new QWidget(&window);
    body->setGeometry(0, 74, 800, 500);
    auto pane = new PaneFrame(body);
    pane->move(50, 0);
    pane->set_device_width(300);
    pane->setFixedHeight(500);
    auto resizer = PaneResizer(*pane, &window);
    window.show();
    QCoreApplication::processEvents();
    REQUIRE(resizer.geometry() == QRect(
      350 - PaneResizer::BAND_WIDTH, 74, PaneResizer::BAND_WIDTH, 526));
    auto enter = QEnterEvent(QPointF(1, 10), QPointF(1, 10), QPointF(1, 10));
    QCoreApplication::sendEvent(&resizer, &enter);
    REQUIRE(resizer.cursor().shape() == Qt::SizeHorCursor);
    window.resize(800, 640);
    QCoreApplication::processEvents();
    REQUIRE(resizer.geometry().height() == 566);
  }
  TEST_CASE("band_below_children") {
    auto parent = QWidget();
    parent.resize(800, 600);
    auto pane = new PaneFrame(&parent);
    pane->move(50, 0);
    pane->set_device_width(400);
    pane->setFixedHeight(580);
    auto resizer = PaneResizer(*pane, &parent);
    auto widths = std::vector<double>();
    resizer.set_width_handler([&] (auto width) {
      widths.push_back(width);
    });
    resizer.set_width_provider([] {
      return 400.0;
    });
    parent.show();
    QCoreApplication::processEvents();
    auto row = 599.5;
    send(parent, QEvent::MouseMove, QPointF(448, row), 448, Qt::NoButton,
      Qt::NoButton);
    REQUIRE(parent.cursor().shape() == Qt::SizeHorCursor);
    send(parent, QEvent::MouseButtonPress, QPointF(448, row), 448,
      Qt::LeftButton, Qt::LeftButton);
    for(auto x = 449; x <= 454; ++x) {
      send(parent, QEvent::MouseMove, QPointF(x, row), x, Qt::NoButton,
        Qt::LeftButton);
    }
    REQUIRE(widths == std::vector{401.0, 402.0});
    send(parent, QEvent::MouseButtonRelease, QPointF(454, row), 454,
      Qt::LeftButton, Qt::NoButton);
    REQUIRE(parent.cursor().shape() == Qt::ArrowCursor);
    send(parent, QEvent::MouseMove, QPointF(449, row), 449, Qt::NoButton,
      Qt::NoButton);
    REQUIRE(parent.cursor().shape() == Qt::SizeHorCursor);
    send(parent, QEvent::MouseMove, QPointF(300, row), 300, Qt::NoButton,
      Qt::NoButton);
    REQUIRE(parent.cursor().shape() == Qt::ArrowCursor);
    send(parent, QEvent::MouseMove, QPointF(449, row), 449, Qt::NoButton,
      Qt::NoButton);
    REQUIRE(parent.cursor().shape() == Qt::SizeHorCursor);
    auto enter = QEnterEvent(QPointF(1, 10), QPointF(1, 10), QPointF(1, 10));
    QCoreApplication::sendEvent(pane, &enter);
    REQUIRE(parent.cursor().shape() == Qt::ArrowCursor);
    send(parent, QEvent::MouseButtonPress, QPointF(300, row), 300,
      Qt::LeftButton, Qt::LeftButton);
    send(parent, QEvent::MouseMove, QPointF(320, row), 320, Qt::NoButton,
      Qt::LeftButton);
    REQUIRE(widths.size() == 2);
  }
}

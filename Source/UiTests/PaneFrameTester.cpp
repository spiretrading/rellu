#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QImage>
#include <QWidget>
#include "Rellu/Ui/PaneFrame.hpp"

using namespace Rellu;

TEST_SUITE("PaneFrame") {
  TEST_CASE("pane_frame") {
    auto pane = PaneFrame();
    auto page = new QWidget();
    pane.addWidget(page);
    pane.set_device_width(300);
    pane.resize(pane.width(), 50);
    REQUIRE(pane.get_device_width() == 300);
    REQUIRE(pane.width() == 300);
    REQUIRE(pane.contentsMargins().right() == PaneFrame::BORDER_WIDTH);
    pane.show();
    QCoreApplication::processEvents();
    REQUIRE(page->width() == 300 - PaneFrame::BORDER_WIDTH);
    auto image = pane.grab().toImage();
    REQUIRE(QColor(image.pixel(297, 10)) == PaneFrame::BACKGROUND);
    REQUIRE(QColor(image.pixel(298, 10)) == PaneFrame::BORDER);
    REQUIRE(QColor(image.pixel(299, 10)) == PaneFrame::BORDER);
    pane.set_device_width(-5);
    REQUIRE(pane.get_device_width() == 0);
  }
}

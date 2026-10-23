#include <doctest/doctest.h>
#include <QApplication>
#include <QColor>
#include <QEnterEvent>
#include <QImage>
#include <QPoint>
#include <QWidget>
#include "Rellu/Ui/CaptionButtons.hpp"

using namespace Rellu;

namespace {
  void enter(QWidget& widget) {
    auto position = QPointF(widget.width() / 2.0, widget.height() / 2.0);
    auto event = QEnterEvent(
      position, position, widget.mapToGlobal(position));
    QApplication::sendEvent(&widget, &event);
  }
}

TEST_SUITE("CaptionButtons") {
  TEST_CASE("make_caption_buttons") {
    auto window = QWidget();
    auto buttons = make_caption_buttons(window, window);
    REQUIRE(buttons.size() == 3);
    for(auto button : buttons) {
      REQUIRE(button->parentWidget() == &window);
      REQUIRE(button->size() ==
        QSize(CaptionButton::WIDTH, CaptionButton::HEIGHT));
      REQUIRE(!button->toolTip().isEmpty());
      REQUIRE(button->accessibleName() == button->toolTip());
    }
    REQUIRE(buttons[0]->toolTip() == "Minimize");
    REQUIRE(buttons[1]->toolTip() == "Maximize");
    REQUIRE(buttons[2]->toolTip() == "Close");
    update_maximize_tip(*buttons[1], true);
    REQUIRE(buttons[1]->toolTip() == "Restore Down");
    REQUIRE(buttons[1]->accessibleName() == "Restore Down");
    update_maximize_tip(*buttons[1], false);
    REQUIRE(buttons[1]->toolTip() == "Maximize");
  }

  TEST_CASE("hover_after_hide") {
    auto window = QWidget();
    window.resize(400, 300);
    auto buttons = make_caption_buttons(window, window);
    window.show();
    auto& close = *buttons[2];
    enter(close);
    close.setDown(true);
    REQUIRE(close.is_hovered());
    window.hide();
    REQUIRE(!close.is_hovered());
    REQUIRE(!close.isDown());
    window.show();
    REQUIRE(!close.is_hovered());
    REQUIRE(!close.isDown());
    enter(close);
    REQUIRE(close.is_hovered());
  }

  TEST_CASE("hover_colors") {
    auto window = QWidget();
    window.resize(400, 300);
    auto buttons = make_caption_buttons(window, window);
    window.show();
    auto& minimize = *buttons[0];
    auto& close = *buttons[2];
    auto glyph = QPoint(CaptionButton::WIDTH / 2, CaptionButton::HEIGHT / 2);
    auto corner = QPoint(2, 2);
    REQUIRE(minimize.grab().toImage().pixelColor(glyph) ==
      QColor(0x2D, 0x2D, 0x2D));
    enter(minimize);
    auto hovered = minimize.grab().toImage();
    REQUIRE(hovered.pixelColor(glyph) == QColor(0x00, 0x00, 0x00));
    REQUIRE(hovered.pixelColor(corner) != QColor(0xD6, 0xD6, 0xD6));
    REQUIRE(close.grab().toImage().pixelColor(corner) !=
      QColor(0xC5, 0x29, 0x1D));
    enter(close);
    REQUIRE(close.grab().toImage().pixelColor(corner) ==
      QColor(0xC5, 0x29, 0x1D));
  }

  TEST_CASE("close") {
    auto window = QWidget();
    auto buttons = make_caption_buttons(window, window);
    window.show();
    REQUIRE(window.isVisible());
    buttons[2]->click();
    REQUIRE(!window.isVisible());
  }
}

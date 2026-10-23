#include <algorithm>
#include <utility>
#include <doctest/doctest.h>
#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include "Rellu/Ui/XdAlert.hpp"

using namespace Rellu;

namespace {
  XdAlertContent make_content() {
    auto content = XdAlertContent();
    content.m_message = "Message";
    content.m_explanation = "A longer explanation of the message.";
    content.m_buttons = {"Save", "Don't Save", "Cancel"};
    return content;
  }

  int press(XdAlertContent content, int key) {
    auto alert = XdAlert(std::move(content), nullptr);
    auto event = QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&alert, &event);
    return alert.get_choice();
  }
}

TEST_SUITE("XdAlert") {
  TEST_CASE("keys") {
    auto content = make_content();
    content.m_default = 1;
    REQUIRE(press(content, Qt::Key_Escape) == 1);
    REQUIRE(press(content, Qt::Key_Return) == 1);
    content.m_escape = 2;
    REQUIRE(press(content, Qt::Key_Escape) == 2);
    REQUIRE(press(content, Qt::Key_Return) == 1);
    REQUIRE(press(content, Qt::Key_Enter) == 1);
  }

  TEST_CASE("width") {
    auto content = make_content();
    auto fixed = XdAlert(content, nullptr);
    REQUIRE(fixed.get_panel().minimumWidth() == 544);
    content.m_is_fitted = true;
    auto fitted = XdAlert(content, nullptr);
    auto width = 0;
    for(auto label : fitted.get_panel().findChildren<QLabel*>()) {
      width = std::max(width,
        QFontMetrics(label->font()).horizontalAdvance(label->text()));
    }
    REQUIRE(width > 0);
    REQUIRE(fitted.get_panel().minimumWidth() == width + 135);
    REQUIRE(fitted.get_panel().maximumWidth() == width + 135);
  }
}

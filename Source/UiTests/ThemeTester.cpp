#include <doctest/doctest.h>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QLineEdit>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/Theme.hpp"
#include "Rellu/Ui/Toolbar.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("Theme") {
  TEST_CASE("canvas_color") {
    auto document = Document();
    REQUIRE(get_canvas_color(document) == QColor(0xFF, 0xFF, 0xFF));
    auto ids = build_test_document(document);
    REQUIRE(get_canvas_color(document) == QColor(0xE4, 0xE4, 0xE4));
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_artboard);
      transaction.commit();
    }
    REQUIRE(!document.get_canvas().empty());
    REQUIRE(get_canvas_color(document) == QColor(0xFF, 0xFF, 0xFF));
    {
      auto transaction = Transaction(document);
      auto artboard = Artboard();
      artboard.m_frame = Rect(0, 0, 100, 100);
      transaction.append(NodeId(), make_header("Second"), artboard);
      transaction.commit();
    }
    REQUIRE(get_canvas_color(document) == QColor(0xE4, 0xE4, 0xE4));
  }
  TEST_CASE("apply_theme") {
    auto& application =
      *static_cast<QApplication*>(QCoreApplication::instance());
    auto style = application.style()->name();
    auto palette = QApplication::palette();
    auto font = QApplication::font();
    auto menu_font = QApplication::font("QMenu");
    auto menu_bar_font = QApplication::font("QMenuBar");
    auto style_sheet = application.styleSheet();
    apply_theme(application);
    auto field = QLineEdit();
    field.setObjectName("search_field");
    field.ensurePolished();
    REQUIRE(field.palette().color(QPalette::PlaceholderText) ==
      QColor(0x66, 0x66, 0x66));
    auto bar = AppBar();
    bar.resize(400, AppBar::HEIGHT);
    auto bar_image = bar.grab().toImage();
    REQUIRE(bar_image.pixelColor(200, AppBar::HEIGHT - 3) ==
      QColor(0xF5, 0xF5, 0xF5));
    REQUIRE(bar_image.pixelColor(200, AppBar::HEIGHT - 2) ==
      QColor(0xE4, 0xE4, 0xE4));
    REQUIRE(bar_image.pixelColor(200, AppBar::HEIGHT - 1) ==
      QColor(0xE4, 0xE4, 0xE4));
    auto toolbar = Toolbar(make_xd_tools(), make_xd_panels());
    toolbar.resize(Toolbar::WIDTH, 600);
    auto toolbar_image = toolbar.grab().toImage();
    REQUIRE(toolbar_image.pixelColor(Toolbar::WIDTH - 3, 300) ==
      QColor(0xF5, 0xF5, 0xF5));
    REQUIRE(toolbar_image.pixelColor(Toolbar::WIDTH - 2, 300) ==
      QColor(0xE4, 0xE4, 0xE4));
    REQUIRE(toolbar_image.pixelColor(Toolbar::WIDTH - 1, 300) ==
      QColor(0xE4, 0xE4, 0xE4));
    application.setStyleSheet(style_sheet);
    QApplication::setStyle(QStyleFactory::create(style));
    QApplication::setPalette(palette);
    QApplication::setFont(font);
    QApplication::setFont(menu_font, "QMenu");
    QApplication::setFont(menu_bar_font, "QMenuBar");
  }
}

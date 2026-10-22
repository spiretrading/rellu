#include <doctest/doctest.h>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QLineEdit>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/Theme.hpp"
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
    auto is_grayscale = [] (const QFont& font) {
      return (font.styleStrategy() & QFont::NoSubpixelAntialias) != 0;
    };
    REQUIRE(is_grayscale(QApplication::font()));
    REQUIRE(is_grayscale(QApplication::font("QMenu")));
    auto field = QLineEdit();
    field.setObjectName("search_field");
    field.ensurePolished();
    REQUIRE(is_grayscale(field.font()));
    REQUIRE(field.palette().color(QPalette::PlaceholderText) ==
      QColor(0x66, 0x66, 0x66));
    application.setStyleSheet(style_sheet);
    QApplication::setStyle(QStyleFactory::create(style));
    QApplication::setPalette(palette);
    QApplication::setFont(font);
    QApplication::setFont(menu_font, "QMenu");
    QApplication::setFont(menu_bar_font, "QMenuBar");
  }
}

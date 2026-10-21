#include <doctest/doctest.h>
#include <QColor>
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
}

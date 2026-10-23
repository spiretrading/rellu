#include <string>
#include <doctest/doctest.h>
#include <QColor>
#include <QImage>
#include "Rellu/Io/XdPreview.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  NodeHeader make_header(std::string name) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    return header;
  }

  NodeId add_artboard(Transaction& transaction, const Rect& frame,
      const Color& color) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    artboard.m_background = SolidFill(color);
    return transaction.append(NodeId(), make_header("Artboard"), artboard);
  }

  NodeId add_rectangle(Transaction& transaction, const NodeId& parent,
      const Rect& bounds, const Color& color) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(
      Rect(0, 0, bounds.m_width, bounds.m_height), CornerRadii(0, 0, 0, 0));
    shape.m_style.m_fill = SolidFill(color);
    auto header = make_header("Rectangle");
    header.m_transform = make_translation(bounds.m_x, bounds.m_y);
    return transaction.append(parent, header, shape);
  }
}

TEST_SUITE("XdPreview") {
  TEST_CASE("get_xd_preview_size") {
    REQUIRE(get_xd_preview_size(Rect(0, 0, 4096, 1024), 2048) ==
      QSize(2048, 512));
    REQUIRE(get_xd_preview_size(Rect(0, 0, 375, 812), 512) ==
      QSize(236, 512));
    REQUIRE(get_xd_preview_size(Rect(0, 0, 441, 16), 2048) ==
      QSize(441, 16));
    REQUIRE(get_xd_preview_size(Rect(0, 0, 5000, 1), 512) == QSize(512, 1));
  }

  TEST_CASE("get_xd_preview_source") {
    auto document = Document();
    REQUIRE(!get_xd_preview_source(document));
    auto transaction = Transaction(document);
    add_rectangle(transaction, NodeId(), Rect(10, 20, 30, 40),
      make_color(0, 0, 255));
    add_rectangle(transaction, NodeId(), Rect(100, 0, 20, 10),
      make_color(0, 0, 255));
    REQUIRE(get_xd_preview_source(document) ==
      XdPreviewSource(NodeId(), Rect(10, 0, 110, 60)));
    auto low = add_artboard(transaction, Rect(0, 554, 640, 528),
      make_color(0, 0, 0));
    auto top = add_artboard(transaction, Rect(500, 345, 441, 16),
      make_color(0, 0, 0));
    auto left = add_artboard(transaction, Rect(0, 345, 100, 100),
      make_color(0, 0, 0));
    REQUIRE(get_xd_preview_source(document) ==
      XdPreviewSource(left, Rect(0, 345, 100, 100)));
    transaction.remove(left);
    REQUIRE(get_xd_preview_source(document) ==
      XdPreviewSource(top, Rect(500, 345, 441, 16)));
    transaction.set_flows({Flow(low, "Flow 1")});
    REQUIRE(get_xd_preview_source(document) ==
      XdPreviewSource(low, Rect(0, 554, 640, 528)));
  }

  TEST_CASE("render_xd_previews") {
    auto document = Document();
    auto blank = render_xd_previews(document);
    REQUIRE(blank.m_preview.size() == QSize(512, 512));
    REQUIRE(blank.m_thumbnail.size() == QSize(512, 512));
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(100, 50, 1000, 4096),
      make_color(255, 0, 0));
    add_rectangle(transaction, artboard, Rect(100, 50, 500, 2048),
      make_color(0, 0, 255));
    auto previews = render_xd_previews(document);
    REQUIRE(previews.m_preview.size() == QSize(500, 2048));
    REQUIRE(previews.m_thumbnail.size() == QSize(125, 512));
    REQUIRE(previews.m_preview.pixelColor(100, 400) == QColor(0, 0, 255));
    REQUIRE(previews.m_preview.pixelColor(400, 1800) == QColor(255, 0, 0));
    REQUIRE(previews.m_thumbnail.pixelColor(20, 100) == QColor(0, 0, 255));
    auto loose = Document();
    auto loose_transaction = Transaction(loose);
    add_rectangle(loose_transaction, NodeId(), Rect(0, 0, 40, 20),
      make_color(0, 255, 0));
    add_rectangle(loose_transaction, NodeId(), Rect(60, 40, 20, 20),
      make_color(0, 255, 0));
    auto pasteboard = render_xd_previews(loose);
    REQUIRE(pasteboard.m_preview.size() == QSize(80, 60));
    REQUIRE(pasteboard.m_thumbnail.size() == QSize(80, 60));
    REQUIRE(pasteboard.m_preview.pixelColor(10, 10) == QColor(0, 255, 0));
    REQUIRE(pasteboard.m_preview.pixelColor(50, 10) == QColor(255, 255, 255));
  }
}

#include <QColor>
#include <QImage>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Scene/AssetRenderer.hpp"

using namespace Rellu;

namespace {
  NodeHeader make_header(const Matrix& transform = Matrix()) {
    auto header = NodeHeader();
    header.m_transform = transform;
    return header;
  }

  Shape make_shape(const Rect& bounds, Color color) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(bounds);
    shape.m_style.m_fill = SolidFill(color);
    return shape;
  }
}

TEST_SUITE("AssetRenderer") {
  TEST_CASE("asset_size") {
    REQUIRE(get_asset_size(Rect(0, 0, 100, 60), 1) == QSize(100, 60));
    REQUIRE(get_asset_size(Rect(0, 0, 104, 64), 0.75) == QSize(78, 48));
    REQUIRE(get_asset_size(Rect(0, 0, 10.5, 3.2), 2) == QSize(21, 7));
    REQUIRE(get_asset_size(Rect(0, 0, 10.004, 3), 1) == QSize(10, 3));
    REQUIRE(get_asset_size(Rect(0, 0, 0, 3), 1).isEmpty());
  }

  TEST_CASE("render") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto red = transaction.append(NodeId(),
      make_header(make_translation(50, 40)),
      make_shape(Rect(0, 0, 10, 5), make_color(255, 0, 0)));
    transaction.commit();
    auto renderer = AssetRenderer(document);
    auto image = renderer.render(red, Rect(50, 40, 10, 5), 2).convertToFormat(
      QImage::Format_RGBA8888);
    REQUIRE(image.size() == QSize(20, 10));
    REQUIRE(image.pixelColor(0, 0) == QColor(255, 0, 0));
    REQUIRE(image.pixelColor(19, 9) == QColor(255, 0, 0));
    auto padded = renderer.render(red, Rect(48, 40, 12, 5), 1).convertToFormat(
      QImage::Format_RGBA8888);
    REQUIRE(padded.size() == QSize(12, 5));
    REQUIRE(padded.pixelColor(0, 0).alpha() == 0);
    REQUIRE(padded.pixelColor(2, 0) == QColor(255, 0, 0));
    REQUIRE(renderer.render(red, Rect(0, 0, 0, 5), 1).isNull());
  }
}

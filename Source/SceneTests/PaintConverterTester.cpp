#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <variant>
#include <vector>
#include <QBuffer>
#include <QImage>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Scene/PaintConverter.hpp"

using namespace Rellu;

namespace {
  ImageResource make_resource(QSize size) {
    auto image = QImage(size, QImage::Format_RGBA8888);
    image.fill(QColor(10, 20, 30));
    auto bytes = QByteArray();
    auto buffer = QBuffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    auto data = std::vector<std::byte>(bytes.size());
    std::memcpy(data.data(), bytes.constData(), bytes.size());
    auto resource = ImageResource();
    resource.m_id = generate_node_id();
    resource.m_media_type = "image/png";
    resource.m_data = Shared(std::move(data));
    return resource;
  }

  void require_near(Point actual, Point expected) {
    CAPTURE(actual);
    CAPTURE(expected);
    REQUIRE(std::abs(actual.m_x - expected.m_x) < 1e-9);
    REQUIRE(std::abs(actual.m_y - expected.m_y) < 1e-9);
  }
}

TEST_SUITE("PaintConverter") {
  TEST_CASE("solid") {
    auto document = Document();
    auto converter = PaintConverter(document);
    auto paint = converter.convert(
      SolidFill(make_color(1, 2, 3)), make_rectangle(Rect(0, 0, 1, 1)));
    REQUIRE(std::get<SolidPaint>(*paint).m_color == make_color(1, 2, 3));
    REQUIRE(!converter.convert(NoFill(), make_rectangle(Rect(0, 0, 1, 1))));
  }

  TEST_CASE("gradient_units") {
    auto document = Document();
    auto converter = PaintConverter(document);
    auto fill = GradientFill();
    fill.m_geometry = LinearGradient(Point(0, 0.5), Point(1, 0.5));
    fill.m_stops = {GradientStop(0, make_color(255, 0, 0)),
      GradientStop(1, make_color(0, 0, 255))};
    fill.m_transform = make_translation(0.25, 0);
    auto outline = make_rectangle(Rect(10, 20, 40, 8));
    auto bounding = std::get<GradientPaint>(*converter.convert(fill, outline));
    REQUIRE(bounding.m_stops == fill.m_stops);
    REQUIRE(bounding.m_geometry ==
      GradientGeometry(LinearGradient(Point(20, 24), Point(60, 24))));
    REQUIRE(bounding.m_transform == Matrix());
    auto diagonal = fill;
    diagonal.m_geometry = LinearGradient(Point(1, 0), Point(0, 1));
    diagonal.m_transform = Matrix();
    auto slanted =
      std::get<GradientPaint>(*converter.convert(diagonal, outline));
    REQUIRE(slanted.m_geometry ==
      GradientGeometry(LinearGradient(Point(50, 20), Point(10, 28))));
    auto radial = fill;
    radial.m_geometry = RadialGradient();
    auto circular =
      std::get<GradientPaint>(*converter.convert(radial, outline));
    REQUIRE(circular.m_transform == make_translation(10, 20) *
      make_scale(40, 8) * make_translation(0.25, 0));
    fill.m_units = GradientUnits::USER_SPACE;
    auto user = std::get<GradientPaint>(*converter.convert(fill, outline));
    REQUIRE(user.m_transform == make_translation(0.25, 0));
    fill.m_stops.clear();
    REQUIRE(!converter.convert(fill, outline));
  }

  TEST_CASE("image") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto id = transaction.add_image(make_resource(QSize(4, 2)));
    transaction.commit();
    auto converter = PaintConverter(document);
    auto fill = ImageFill();
    fill.m_image = id;
    fill.m_size = Size(4, 2);
    auto paint = std::get<ImagePaint>(
      *converter.convert(fill, make_rectangle(Rect(0, 0, 8, 8))));
    REQUIRE(paint.m_image->size() == QSize(4, 2));
    REQUIRE(paint.m_image == converter.get_image(id));
    REQUIRE(paint.m_opacity == 1);
    fill.m_image = generate_node_id();
    REQUIRE(!converter.convert(fill, make_rectangle(Rect(0, 0, 8, 8))));
  }

  TEST_CASE("image_transform") {
    auto fill = ImageFill();
    fill.m_size = Size(200, 100);
    auto bounds = Rect(10, 20, 50, 50);
    auto size = QSize(400, 200);
    auto stretch = get_image_transform(fill, bounds, size);
    require_near(stretch.map(Point(0, 0)), Point(10, 20));
    require_near(stretch.map(Point(400, 200)), Point(60, 70));
    fill.m_scale_behavior = ScaleBehavior::COVER;
    auto cover = get_image_transform(fill, bounds, size);
    require_near(cover.map(Point(0, 0)), Point(-15, 20));
    require_near(cover.map(Point(400, 200)), Point(85, 70));
    fill.m_scale = 0.5;
    fill.m_offset = Point(5, -2);
    fill.m_is_flipped_horizontally = true;
    auto adjusted = get_image_transform(fill, bounds, size);
    require_near(adjusted.map(Point(0, 0)), Point(65, 30.5));
    require_near(adjusted.map(Point(400, 200)), Point(15, 55.5));
    fill.m_scale_behavior = ScaleBehavior::FILL;
    fill.m_scale = 1;
    fill.m_offset = Point(0, 0);
    fill.m_is_flipped_horizontally = false;
    fill.m_is_flipped_vertically = true;
    fill.m_horizontal_scale = 2;
    auto scaled = get_image_transform(fill, bounds, size);
    require_near(scaled.map(Point(0, 0)), Point(-15, 70));
    require_near(scaled.map(Point(400, 200)), Point(85, 20));
  }
}

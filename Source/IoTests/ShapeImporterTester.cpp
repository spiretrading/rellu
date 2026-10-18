#include <string>
#include <doctest/doctest.h>
#include "Rellu/Geometry/SvgPath.hpp"
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto SHAPE_ID = *parse_node_id("9fac4ba0-f4aa-4529-ae35-4f279c49c1bd");

  ShapeGeometry import_geometry(const std::string& shape) {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "shape",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd", "shape": )" + shape +
      "}]", document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(report.m_warnings.empty());
    return document.get(SHAPE_ID).get_shape()->m_geometry;
  }
}

TEST_SUITE("ShapeImporter") {
  TEST_CASE("rectangle") {
    REQUIRE(import_geometry(R"({"type": "rect", "x": 1, "y": 2, "width": 30,
      "height": 40})") == ShapeGeometry(
      RectangleShape(Rect(1, 2, 30, 40), CornerRadii(0, 0, 0, 0))));
    REQUIRE(import_geometry(R"({"type": "rect", "x": 0, "y": 0, "width": 30,
      "height": 40, "r": 5})") == ShapeGeometry(
      RectangleShape(Rect(0, 0, 30, 40), CornerRadii(5, 5, 5, 5))));
    REQUIRE(import_geometry(R"({"type": "rect", "x": 0, "y": 0, "width": 30,
      "height": 40, "r": [1, 2, 3, 4]})") == ShapeGeometry(
      RectangleShape(Rect(0, 0, 30, 40), CornerRadii(1, 2, 3, 4))));
  }

  TEST_CASE("ellipse") {
    REQUIRE(import_geometry(R"({"type": "circle", "cx": 5, "cy": 6,
      "r": 7})") == ShapeGeometry(EllipseShape(Point(5, 6), 7, 7)));
    REQUIRE(import_geometry(R"({"type": "ellipse", "cx": 6, "cy": 5.5,
      "rx": 6, "ry": 5.5})") ==
      ShapeGeometry(EllipseShape(Point(6, 5.5), 6, 5.5)));
  }

  TEST_CASE("line") {
    REQUIRE(import_geometry(R"({"type": "line", "x1": 0, "y1": 1, "x2": 840,
      "y2": 2})") == ShapeGeometry(LineShape(Point(0, 1), Point(840, 2))));
  }

  TEST_CASE("polygon") {
    auto geometry = import_geometry(R"({"type": "polygon",
      "uxdesign#cornerCount": 3, "uxdesign#width": 5, "uxdesign#height": 6,
      "uxdesign#cornerRadius": 1, "points": [{"x": 2.5, "y": 0},
      {"x": 5, "y": 6}, {"x": 0, "y": 6}]})");
    auto& polygon = std::get<PolygonShape>(geometry);
    REQUIRE(polygon.m_corner_count == 3);
    REQUIRE(polygon.m_size == Size(5, 6));
    REQUIRE(polygon.m_corner_radius == 1);
    REQUIRE(polygon.m_vertices ==
      std::vector<Point>({Point(2.5, 0), Point(5, 6), Point(0, 6)}));
  }

  TEST_CASE("path") {
    auto geometry = import_geometry(R"({"type": "path",
      "path": "M 0 0 L 10 0 L 10 10 Z", "winding": "evenodd"})");
    auto& path = std::get<PathShape>(geometry);
    REQUIRE(*path.m_path == *parse_svg_path("M 0 0 L 10 0 L 10 10 Z"));
    REQUIRE(path.m_fill_rule == FillRule::EVEN_ODD);
    auto other = std::get<PathShape>(import_geometry(R"({"type": "path",
      "path": "M 0 0 L 1 1"})"));
    REQUIRE(other.m_fill_rule == FillRule::NON_ZERO);
  }

  TEST_CASE("shared_paths") {
    auto document = Document();
    import_nodes(R"([{"type": "shape", "id": "1", "shape": {"type": "path",
      "path": "M 0 0 L 5 5"}}, {"type": "shape", "id": "2", "shape": {
      "type": "path", "path": "M 0 0 L 5 5"}}])", document);
    auto& canvas = document.get_canvas();
    auto& first = std::get<PathShape>(
      document.get(canvas[0]).get_shape()->m_geometry);
    auto& second = std::get<PathShape>(
      document.get(canvas[1]).get_shape()->m_geometry);
    REQUIRE(first.m_path.get_pointer() == second.m_path.get_pointer());
  }

  TEST_CASE("compound") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "shape",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd", "shape": {
      "type": "compound", "operation": "subtract", "path": "M 0 0 L 4 0 Z",
      "children": [{"type": "shape", "id": "a", "shape": {"type": "rect",
      "x": 0, "y": 0, "width": 4, "height": 4}}, {"type": "shape", "id": "b",
      "shape": {"type": "circle", "cx": 2, "cy": 2, "r": 1}}]}}])", document);
    REQUIRE(report.m_unconsumed.empty());
    auto& node = document.get(SHAPE_ID);
    auto& compound = std::get<CompoundShape>(node.get_shape()->m_geometry);
    REQUIRE(compound.m_operation == CompoundOperation::SUBTRACT);
    REQUIRE(*compound.m_path == *parse_svg_path("M 0 0 L 4 0 Z"));
    REQUIRE(node.m_children.size() == 2);
    REQUIRE(std::holds_alternative<EllipseShape>(
      document.get(node.m_children[1]).get_shape()->m_geometry));
  }

  TEST_CASE("compound_operations") {
    auto operation = [] (const std::string& name) {
      return std::get<CompoundShape>(import_geometry(R"({"type": "compound",
        "operation": ")" + name + R"(", "path": "M 0 0"})")).m_operation;
    };
    REQUIRE(operation("add") == CompoundOperation::UNION);
    REQUIRE(operation("intersect") == CompoundOperation::INTERSECT);
    REQUIRE(operation("exclude") == CompoundOperation::EXCLUDE);
  }

  TEST_CASE("unsupported") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "shape", "id": "1", "shape": {
      "type": "star", "points": 5}}, {"type": "shape", "id": "2",
      "shape": {"type": "path", "path": "M 0 0 Q"}}])", document);
    REQUIRE(report.m_warnings.at("Unsupported shape geometry") == 1);
    REQUIRE(report.m_warnings.at("Malformed path data") == 1);
    REQUIRE(report.m_unconsumed.at("<shape>.shape.type:str") == 1);
    REQUIRE(document.get_canvas().size() == 1);
  }

  TEST_CASE("video") {
    auto package = XdTestPackage();
    package.m_entries.emplace_back(
      "resources/d6608fe5b4dfdc23627c4e5c912872b5", QByteArray("\xFF\xD8\xFF"));
    package.m_pasteboard = parse_json_array(R"([{"type": "shape",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd", "shape": {"type": "rect",
      "x": 0, "y": 0, "width": 64, "height": 48, "meta": {"ux": {
      "mediaFill": {"type": "pattern", "pattern": {"width": 640,
      "height": 480, "meta": {"ux": {"scaleBehavior": "cover",
      "uid": "d6608fe5b4dfdc23627c4e5c912872b5"}}, "href": ""}}}}},
      "group": {"children": []}}])");
    auto document = Document();
    auto report = import_package(package, document);
    REQUIRE(report.m_unconsumed.empty());
    auto& fill = document.get(SHAPE_ID).get_shape()->m_style.m_fill;
    REQUIRE(std::get<ImageFill>(fill).m_size == Size(640, 480));
  }
}

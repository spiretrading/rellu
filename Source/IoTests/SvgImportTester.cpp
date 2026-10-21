#include <stdexcept>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/SvgPath.hpp"
#include "Rellu/Io/SvgImport.hpp"

using namespace Rellu;

namespace {
  const Node& get_root(const Document& document) {
    REQUIRE(document.get_canvas().size() == 1);
    return document.get(document.get_canvas().front());
  }

  const Node& get_child(
      const Document& document, const Node& node, std::size_t index) {
    REQUIRE(index < node.m_children.size());
    return document.get(node.m_children[index]);
  }

  const Shape& read_shape(const QString& markup, Document& document) {
    read_svg(markup, document);
    auto& root = get_root(document);
    REQUIRE(root.get_shape());
    return *root.get_shape();
  }
}

TEST_SUITE("SvgImport") {
  TEST_CASE("is_svg") {
    REQUIRE(is_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"/>)svg"));
    REQUIRE(is_svg(R"svg(<?xml version="1.0"?>
      <svg xmlns="http://www.w3.org/2000/svg"><rect width="1"
      height="1"/></svg>)svg"));
    REQUIRE(!is_svg("Some text"));
    REQUIRE(!is_svg("<svg><rect></svg>"));
    REQUIRE(!is_svg("<html><svg/></html>"));
    auto document = Document();
    REQUIRE_THROWS_AS(read_svg("<svg", document), std::invalid_argument);
  }

  TEST_CASE("shapes") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200"
      height="100"><rect id="Box" x="1" y="2" width="30" height="40" rx="5"/>
      <circle cx="5" cy="6" r="7"/><ellipse cx="8" cy="9" rx="10" ry="11"/>
      <line x1="0" y1="1" x2="2" y2="3"/>
      <polygon points="0,0 10,0 5,10" fill-rule="evenodd"/>
      <polyline points="0 0 10 10 20 0"/>
      <path data-name="Tick" d="M0 0 L10 10"/><title>Ignored</title>
      <g></g></svg>)svg", document);
    auto& root = get_root(document);
    REQUIRE(root.get_group());
    REQUIRE(root.m_header.m_name.empty());
    REQUIRE(!root.m_header.m_has_custom_name);
    REQUIRE(root.m_children.size() == 7);
    auto& box = get_child(document, root, 0);
    REQUIRE(box.m_header.m_name == "Box");
    REQUIRE(box.m_header.m_has_custom_name);
    REQUIRE(box.m_header.m_transform == make_translation(1, 2));
    REQUIRE(box.get_shape()->m_geometry == ShapeGeometry(RectangleShape(
      Rect(0, 0, 30, 40), CornerRadii(5, 5, 5, 5))));
    auto& circle = get_child(document, root, 1);
    REQUIRE(circle.m_header.m_transform == make_translation(-2, -1));
    REQUIRE(circle.get_shape()->m_geometry ==
      ShapeGeometry(EllipseShape(Point(7, 7), 7, 7)));
    auto& ellipse = get_child(document, root, 2);
    REQUIRE(ellipse.m_header.m_transform == make_translation(-2, -2));
    REQUIRE(ellipse.get_shape()->m_geometry ==
      ShapeGeometry(EllipseShape(Point(10, 11), 10, 11)));
    auto& line = get_child(document, root, 3);
    REQUIRE(line.m_header.m_transform == make_translation(0, 1));
    REQUIRE(line.get_shape()->m_geometry ==
      ShapeGeometry(LineShape(Point(0, 0), Point(2, 2))));
    auto& polygon = std::get<PathShape>(
      get_child(document, root, 4).get_shape()->m_geometry);
    REQUIRE(polygon.m_fill_rule == FillRule::EVEN_ODD);
    REQUIRE(*polygon.m_path == *parse_svg_path("M0 0L10 0L5 10L0 0Z"));
    REQUIRE(*std::get<PathShape>(get_child(document, root, 5).get_shape()->
      m_geometry).m_path == *parse_svg_path("M0 0L10 10L20 0"));
    auto& tick = get_child(document, root, 6);
    REQUIRE(tick.m_header.m_name == "Tick");
    REQUIRE(*std::get<PathShape>(tick.get_shape()->m_geometry).m_path ==
      *parse_svg_path("M0 0 L10 10"));
  }

  TEST_CASE("paint") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><g fill="#f00"
      stroke="rgb(0, 0, 255)" stroke-width="3" opacity="0.5"><rect
      width="1" height="1" fill-opacity="0.5" style="stroke-linecap:round;
      stroke-linejoin:bevel;stroke-dasharray:4 2"/><rect width="1"
      height="1" fill="none" stroke="none" visibility="hidden"/></g></svg>)svg",
      document);
    auto& group = get_root(document);
    REQUIRE(group.m_header.m_opacity == 0.5);
    auto& first = *get_child(document, group, 0).get_shape();
    REQUIRE(first.m_style.m_fill == Fill(SolidFill(make_color(255, 0, 0,
      128 / 255.0))));
    REQUIRE(first.m_style.m_stroke);
    auto& stroke = *first.m_style.m_stroke;
    REQUIRE(stroke.m_color == make_color(0, 0, 255));
    REQUIRE(stroke.m_width == 3);
    REQUIRE(stroke.m_cap == StrokeCap::ROUND);
    REQUIRE(stroke.m_join == StrokeJoin::BEVEL);
    REQUIRE(stroke.m_dashes == std::vector<double>({4, 2}));
    auto& second = get_child(document, group, 1);
    REQUIRE(!second.m_header.m_is_visible);
    REQUIRE(second.get_shape()->m_style.m_fill ==
      Fill(NoFill(SolidFill(Color(0, 0, 0, 0)))));
    REQUIRE(!second.get_shape()->m_style.m_stroke->m_is_enabled);
    REQUIRE(second.get_shape()->m_style.m_stroke->m_color ==
      make_color(112, 112, 112));
  }

  TEST_CASE("gradients") {
    auto document = Document();
    auto& shape = read_shape(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      xmlns:xlink="http://www.w3.org/1999/xlink"><defs><linearGradient
      id="a"><stop offset="0" stop-color="#000"/><stop offset="100%"
      stop-color="#fff" stop-opacity="0.5"/></linearGradient><radialGradient
      id="b" xlink:href="#a" cx="10" cy="20" r="30" fx="5"
      gradientUnits="userSpaceOnUse"
      gradientTransform="translate(1 2)"/></defs><circle r="5"
      fill="url(#b)"/></svg>)svg", document);
    auto& fill = std::get<GradientFill>(shape.m_style.m_fill);
    REQUIRE(fill.m_units == GradientUnits::USER_SPACE);
    REQUIRE(fill.m_geometry == GradientGeometry(
      RadialGradient(Point(10, 20), 30, Point(5, 20))));
    REQUIRE(fill.m_transform == make_translation(6, 7));
    REQUIRE(fill.m_stops == std::vector<GradientStop>({
      GradientStop(0, make_color(0, 0, 0)),
      GradientStop(1, make_color(255, 255, 255, 128 / 255.0))}));
    auto other = Document();
    auto& linear = std::get<GradientFill>(read_shape(R"svg(<svg
      xmlns="http://www.w3.org/2000/svg"><linearGradient id="g" x1="0"
      y1="0" x2="0" y2="1"><stop stop-color="red"/></linearGradient><rect
      width="5" height="5" fill="url(#g)"/></svg>)svg", other).m_style.m_fill);
    REQUIRE(linear.m_units == GradientUnits::OBJECT_BOUNDING_BOX);
    REQUIRE(linear.m_geometry ==
      GradientGeometry(LinearGradient(Point(0, 0), Point(0, 1))));
    auto user = Document();
    auto& radial = std::get<GradientFill>(read_shape(R"svg(<svg
      xmlns="http://www.w3.org/2000/svg"><radialGradient id="r" cx="50"
      cy="60" r="30" gradientUnits="userSpaceOnUse"><stop offset="0.2"
      stop-color="yellow"/></radialGradient><circle cx="50" cy="60" r="30"
      fill="url(#r)"/></svg>)svg", user).m_style.m_fill);
    REQUIRE(radial.m_units == GradientUnits::OBJECT_BOUNDING_BOX);
    REQUIRE(radial.m_geometry == GradientGeometry(
      RadialGradient(Point(0.5, 0.5), 0.5, Point(0.5, 0.5))));
    REQUIRE(radial.m_stops.front().m_offset ==
      static_cast<double>(0.2f));
  }

  TEST_CASE("transforms") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      xmlns:xlink="http://www.w3.org/1999/xlink"><defs><rect id="r"
      width="4" height="4" fill="#00f"/></defs><g id="Moved"
      transform="translate(10, 20) scale(2)"><rect width="1"
      height="1" transform="rotate(90)"/></g><use xlink:href="#r" x="3"
      y="4" transform="translate(1 1)" fill="#f00"/></svg>)svg", document);
    auto& root = get_root(document);
    REQUIRE(root.m_children.size() == 2);
    auto& moved = get_child(document, root, 0);
    REQUIRE(moved.m_header.m_name == "Moved");
    REQUIRE(moved.m_header.m_transform ==
      make_translation(10, 20) * make_scale(2, 2));
    auto& rotated = get_child(document, moved, 0);
    REQUIRE(rotated.m_header.m_transform == Matrix(0, 1, -1, 0, 0, 0));
    REQUIRE(rotated.m_header.m_rotation == 90);
    auto& used = get_child(document, root, 1);
    REQUIRE(used.m_header.m_transform == make_translation(4, 5));
    REQUIRE(used.get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 255))));
  }

  TEST_CASE("single_element") {
    auto document = Document();
    auto& shape = read_shape(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      width="40" height="40"><path d="M5 5 H35 V35 H5 Z"
      fill="#336699"/></svg>)svg", document);
    REQUIRE(shape.m_style.m_fill == Fill(SolidFill(make_color(51, 102,
      153))));
  }
}

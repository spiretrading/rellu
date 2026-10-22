#include <cmath>
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

  TEST_CASE("user_space_radial_gradients") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs>
      <radialGradient id="g" cx="100" cy="50" r="60"
      gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="#f00"/>
      <stop offset="1" stop-color="#00f"/></radialGradient></defs><rect
      x="20" y="20" width="200" height="80" fill="url(#g)"/><rect x="250"
      y="20" width="80" height="160" fill="url(#g)"/></svg>)svg", document);
    auto& root = get_root(document);
    auto fill = [&] (std::size_t index) {
      return std::get<GradientFill>(
        get_child(document, root, index).get_shape()->m_style.m_fill);
    };
    auto wide = fill(0);
    REQUIRE(wide.m_units == GradientUnits::OBJECT_BOUNDING_BOX);
    REQUIRE(wide.m_geometry == GradientGeometry(RadialGradient(
      Point(0.4f, 0.375), 0.75, Point(0.4f, 0.375))));
    REQUIRE(wide.m_transform == Matrix(0.4f, 0, 0, 1, 0.3f, 0));
    auto tall = fill(1);
    REQUIRE(tall.m_geometry == GradientGeometry(RadialGradient(
      Point(-1.875, 0.1875), 0.375, Point(-1.875, 0.1875))));
    REQUIRE(tall.m_transform == Matrix(2, 0, 0, 1, -0.5, 0));
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

  TEST_CASE("stylesheets") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><style><![CDATA[
      /* Comment */ rect { fill: #00f; stroke: #0f0 }
      .a { fill: #f00 } g > .b { opacity: 0.5 }
      #c { fill: #ff0 !important } @media print { rect { fill: #fff } }
      ]]></style><rect class="a" width="1" height="1" fill="#000"/>
      <g><rect class="a b" width="1" height="1"/></g>
      <rect id="c" class="a" width="1" height="1" style="fill: #0ff"/>
      <rect width="1" height="1" style="fill: #f0f"/></svg>)svg", document);
    auto& root = get_root(document);
    REQUIRE(root.m_children.size() == 4);
    auto fill = [&] (const Node& node) {
      return std::get<SolidFill>(node.get_shape()->m_style.m_fill).m_color;
    };
    auto& first = get_child(document, root, 0);
    REQUIRE(fill(first) == make_color(255, 0, 0));
    REQUIRE(first.get_shape()->m_style.m_stroke->m_color ==
      make_color(0, 255, 0));
    auto& group = get_child(document, root, 1);
    auto& second = get_child(document, group, 0);
    REQUIRE(fill(second) == make_color(255, 0, 0));
    REQUIRE(second.m_header.m_opacity == 0.5);
    REQUIRE(fill(get_child(document, root, 2)) == make_color(255, 255, 0));
    REQUIRE(fill(get_child(document, root, 3)) == make_color(255, 0, 255));
  }

  TEST_CASE("blending") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><g
      style="mix-blend-mode: multiply; isolation: isolate"><rect width="1"
      height="1" style="mix-blend-mode: color-dodge"/></g></svg>)svg",
      document);
    auto& group = get_root(document);
    REQUIRE(group.m_header.m_blend_mode == BlendMode::NORMAL);
    REQUIRE(!group.m_header.m_is_isolated);
    auto& rectangle = get_child(document, group, 0);
    REQUIRE(rectangle.m_header.m_blend_mode == BlendMode::NORMAL);
  }

  TEST_CASE("text") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text
      transform="translate(5 5)" x="10" y="20" font-family="'Arial', serif"
      font-size="16" font-weight="bold" fill="#f00" text-anchor="middle">
      Hello <tspan fill="#00f" font-style="italic"
      text-decoration="underline">World</tspan></text></svg>)svg", document);
    auto& node = get_root(document);
    auto text = node.get_text();
    REQUIRE(text);
    REQUIRE(*text->m_content == u"Hello World");
    REQUIRE(node.m_header.m_name == "Hello World");
    REQUIRE(node.m_header.m_transform == make_translation(15, 25));
    REQUIRE(std::holds_alternative<PositionedFrame>(text->m_frame));
    REQUIRE(text->m_paragraph_style.m_alignment == TextAlignment::CENTER);
    REQUIRE(text->m_runs.size() == 2);
    REQUIRE(text->m_runs[0].m_length == 6);
    REQUIRE(text->m_runs[1].m_length == 5);
    auto& hello = text->m_runs[0].m_style;
    REQUIRE(hello.m_font.m_family == "Arial");
    REQUIRE(hello.m_font.m_style == "Bold");
    REQUIRE(hello.m_font.m_size == 16);
    REQUIRE(hello.m_color == make_color(255, 0, 0));
    auto& world = text->m_runs[1].m_style;
    REQUIRE(text->m_base_style == world);
    REQUIRE(world.m_color == make_color(0, 0, 255));
    REQUIRE(world.m_font.m_style == "Bold Italic");
    REQUIRE(world.m_is_underlined);
  }

  TEST_CASE("multiline_text") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text><tspan
      x="4" y="10">One</tspan><tspan x="4" y="30">Two</tspan><tspan x="4"
      y="70" letter-spacing="2" baseline-shift="super">Four</tspan></text>
      </svg>)svg", document);
    auto& node = get_root(document);
    auto text = node.get_text();
    REQUIRE(text);
    REQUIRE(*text->m_content == u"One\nTwo\n\nFour");
    REQUIRE(node.m_header.m_transform == make_translation(4, 10));
    REQUIRE(text->m_paragraph_style.m_line_height == 20);
    REQUIRE(text->m_runs.front().m_style.m_font.m_family ==
      "Helvetica Neue");
    REQUIRE(text->m_runs.front().m_style.m_font.m_size == 20);
    REQUIRE(text->m_runs.size() == 2);
    REQUIRE(text->m_runs[0].m_length == 9);
    auto& four = text->m_runs[1].m_style;
    REQUIRE(four.m_script == TextScript::SUPERSCRIPT);
    REQUIRE(four.m_font.m_size == std::round(20 / 0.5833 * 1e5) / 1e5);
    REQUIRE(four.m_letter_spacing == 100);
    auto other = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text x="1"
      y="2">A<tspan dx="3" dy="5">B</tspan><tspan xml:space="preserve">
      C  </tspan></text></svg>)svg", other);
    REQUIRE(*get_root(other).get_text()->m_content == u"AB      C ");
  }

  TEST_CASE("images") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      xmlns:xlink="http://www.w3.org/1999/xlink"><defs><pattern id="p"
      width="1" height="1" viewBox="0 0 1 1"><image width="1" height="1"
      xlink:href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB)svg"
      R"svg(CAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="/>
      </pattern></defs><image id="Photo" x="5" y="6" width="20" height="10"
      xlink:href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB)svg"
      R"svg(CAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="/>
      <rect width="8" height="8" fill="url(#p)"/><image width="5" height="5"
      xlink:href="missing.png"/></svg>)svg", document);
    auto& resources = document.get_resources();
    REQUIRE(resources.m_images.size() == 1);
    auto& image = resources.m_images.begin()->second;
    REQUIRE(image.m_media_type == "image/png");
    auto& root = get_root(document);
    REQUIRE(root.m_children.size() == 2);
    auto& photo = get_child(document, root, 0);
    REQUIRE(photo.m_header.m_name == "Photo");
    REQUIRE(photo.m_header.m_transform == make_translation(5, 6));
    auto& shape = *photo.get_shape();
    REQUIRE(shape.m_geometry ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 20, 10))));
    auto& fill = std::get<ImageFill>(shape.m_style.m_fill);
    REQUIRE(fill.m_image == image.m_id);
    REQUIRE(fill.m_size == Size(1, 1));
    REQUIRE(!shape.m_style.m_stroke);
    auto& patterned = std::get<ImageFill>(
      get_child(document, root, 1).get_shape()->m_style.m_fill);
    REQUIRE(patterned.m_image == image.m_id);
    REQUIRE(patterned.m_scale_behavior == ScaleBehavior::COVER);
  }

  TEST_CASE("clip_paths") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs><clipPath
      id="c"><circle cx="5" cy="5" r="5"/><rect x="1" y="2" width="3"
      height="4" transform="translate(1 1)"/></clipPath><mask id="m"><rect
      width="1" height="1" fill="#fff"/></mask></defs><rect id="Masked"
      width="10" height="10" transform="translate(20 30)"
      clip-path="url(#c)"/><rect width="2" height="2" mask="url(#m)"/>
      </svg>)svg", document);
    auto& root = get_root(document);
    REQUIRE(root.m_children.size() == 2);
    auto& masked = get_child(document, root, 0);
    REQUIRE(masked.m_header.m_transform == make_translation(20, 30));
    auto& mask = *masked.get_group()->m_mask;
    REQUIRE(mask.m_shapes.size() == 2);
    REQUIRE(mask.m_shapes[0].m_geometry ==
      ShapeGeometry(EllipseShape(Point(5, 5), 5, 5)));
    REQUIRE(mask.m_shapes[0].m_transform == make_translation(0, 0));
    REQUIRE(mask.m_shapes[1].m_transform == make_translation(2, 3));
    auto& content = get_child(document, masked, 0);
    REQUIRE(content.m_header.m_name == "Masked");
    REQUIRE(content.m_header.m_transform == Matrix());
    auto& unmasked = get_child(document, root, 1);
    REQUIRE(unmasked.get_shape());
  }

  TEST_CASE("symbols") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      xmlns:xlink="http://www.w3.org/1999/xlink"><symbol id="s"
      viewBox="0 0 10 10"><rect width="20" height="20"/></symbol><use
      xlink:href="#s" x="5" width="20" height="20"/></svg>)svg", document);
    auto& masked = get_root(document);
    REQUIRE(masked.get_group()->m_mask);
    REQUIRE(masked.m_header.m_transform ==
      make_translation(5, 0) * make_scale(2, 2));
    auto& mask = *masked.get_group()->m_mask;
    REQUIRE(mask.m_shapes.size() == 1);
    REQUIRE(mask.m_shapes[0].m_geometry ==
      ShapeGeometry(RectangleShape(Rect(0, 0, 10, 10))));
  }

  TEST_CASE("shadows") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs><filter
      id="d"><feOffset dy="3" input="SourceAlpha"/><feGaussianBlur
      stdDeviation="3" result="b"/><feFlood flood-opacity="0.161"/>
      <feComposite operator="in" in2="b"/><feComposite in="SourceGraphic"/>
      </filter><filter id="i"><feOffset dx="1" dy="2" input="SourceAlpha"/>
      <feGaussianBlur stdDeviation="4" result="b"/><feFlood
      flood-color="#f00"/><feComposite operator="out" in="SourceGraphic"/>
      <feComposite operator="in" in2="b"/><feComposite operator="in"
      in2="SourceGraphic"/></filter><filter id="s"><feOffset dx="0" dy="2"
      in="SourceAlpha" result="o"/><feGaussianBlur stdDeviation="2" in="o"
      result="b"/><feColorMatrix in="b" result="m" values="0 0 0 0 0
      0 0 0 0 0 0 0 0 0 0 0 0 0 0.5 0"/><feMerge><feMergeNode in="m"/>
      <feMergeNode in="SourceGraphic"/></feMerge></filter><filter id="blur">
      <feGaussianBlur stdDeviation="5"/></filter></defs><rect width="1"
      height="1" filter="url(#d)"/><rect width="1" height="1" fill="#0f0"
      filter="url(#i)"/><rect width="1" height="1" filter="url(#s)"/>
      <rect width="1" height="1" filter="url(#blur)"/><g id="Shadowed"
      filter="url(#d)" transform="translate(4 5)"><rect width="1"
      height="1" transform="translate(1 1)"/></g></svg>)svg", document);
    auto& root = get_root(document);
    REQUIRE(root.m_children.size() == 5);
    auto style = [&] (std::size_t index) {
      return get_child(document, root, index).get_shape()->m_style;
    };
    REQUIRE(style(0).m_effects == std::vector<Effect>({DropShadow(
      Point(0, 3), 3, make_color(0, 0, 0, 0.161))}));
    REQUIRE(std::holds_alternative<SolidFill>(style(0).m_fill));
    REQUIRE(style(1).m_effects == std::vector<Effect>({InnerShadow(
      Point(1, 2), 4, make_color(255, 0, 0))}));
    REQUIRE(std::get<NoFill>(style(1).m_fill).m_paint ==
      FillPaint(SolidFill(make_color(0, 255, 0))));
    REQUIRE(style(2).m_effects == std::vector<Effect>({DropShadow(
      Point(0, 2), 2, Color(0, 0, 0, 0.5))}));
    REQUIRE(style(3).m_effects.empty());
    auto& shadowed = get_child(document, root, 4);
    REQUIRE(shadowed.m_header.m_name == "Shadowed");
    REQUIRE(shadowed.m_header.m_transform == make_translation(5, 6));
    REQUIRE(shadowed.get_shape()->m_style.m_effects.size() == 1);
  }

  TEST_CASE("group_rules") {
    auto document = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><g id="Outer"
      stroke="#f00" stroke-width="2" opacity="0.5"><rect width="10"
      height="10" stroke="none" fill="#00f"/><rect x="-1" y="-1"
      width="12" height="12" fill="none"/></g><g stroke="#00f"
      stroke-width="4"><circle cx="5" cy="5" r="5" stroke="none"/><circle
      cx="5" cy="5" r="3" fill="none"/></g><g fill="none"><rect x="0.5"
      y="0.5" width="9" height="9"/><path fill="#0f0"
      d="M9 1V9H1V1H9M10 0H0V10H10V0Z"/>
      </g></svg>)svg", document);
    auto& root = get_root(document);
    REQUIRE(root.m_children.size() == 3);
    auto& outer = get_child(document, root, 0);
    REQUIRE(outer.m_header.m_name == "Outer");
    REQUIRE(outer.m_header.m_opacity == 0.5);
    REQUIRE(outer.get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 255))));
    auto& outside = *outer.get_shape()->m_style.m_stroke;
    REQUIRE(outside.m_alignment == StrokeAlignment::OUTSIDE);
    REQUIRE(outside.m_color == make_color(255, 0, 0));
    REQUIRE(outside.m_width == 2);
    auto& inside = *get_child(document, root, 1).get_shape()->m_style.m_stroke;
    REQUIRE(inside.m_alignment == StrokeAlignment::INSIDE);
    REQUIRE(inside.m_width == 4);
    auto& framed = *get_child(document, root, 2).get_shape();
    REQUIRE(framed.m_style.m_stroke->m_is_enabled);
    REQUIRE(framed.m_style.m_stroke->m_alignment == StrokeAlignment::CENTER);
    REQUIRE(framed.m_style.m_stroke->m_width == 1);
    REQUIRE(framed.m_style.m_stroke->m_color == make_color(0, 255, 0));
  }

  TEST_CASE("switches") {
    auto document = Document();
    read_svg(R"svg(<!DOCTYPE svg [<!ENTITY ns_ai
      "http://ns.adobe.com/AdobeIllustrator/10.0/">]><svg
      xmlns="http://www.w3.org/2000/svg" xmlns:i="&ns_ai;"><switch>
      <foreignObject width="1" height="1"/><g i:extraneous="self"
      fill="#f00"><rect width="1" height="1"/><rect width="2" height="2"/>
      </g></switch><switch><rect width="3" height="3"/></switch></svg>)svg",
      document);
    auto& root = get_root(document);
    REQUIRE(root.m_children.size() == 3);
    REQUIRE(get_child(document, root, 0).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(255, 0, 0))));
    auto& fallback = get_child(document, root, 2);
    REQUIRE(fallback.get_group());
    REQUIRE(fallback.m_children.empty());
  }

  TEST_CASE("unsupported_features") {
    auto clipped = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200"
      height="200"><defs><clipPath id="c"><circle cx="100" cy="100"
      r="60"/></clipPath></defs><rect x="20" y="20" width="160"
      height="160" fill="#22aa66" clip-path="url(#c)"/></svg>)svg", clipped);
    auto& group = get_root(clipped);
    REQUIRE(group.m_header.m_name == "Group");
    REQUIRE(!group.m_header.m_has_custom_name);
    REQUIRE(group.m_header.m_transform == make_translation(20, 20));
    auto& mask = *group.get_group()->m_mask;
    REQUIRE(mask.m_shapes.size() == 1);
    REQUIRE(mask.m_shapes[0].m_transform == make_translation(20, 20));
    REQUIRE(mask.m_shapes[0].m_geometry ==
      ShapeGeometry(EllipseShape(Point(60, 60), 60, 60)));
    REQUIRE(get_child(clipped, group, 0).m_header.m_transform == Matrix());
    auto patterned = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs><pattern
      id="p" width="20" height="20" patternUnits="userSpaceOnUse"><rect
      width="10" height="10" fill="#ff0000"/></pattern></defs><rect x="20"
      y="20" width="160" height="120" fill="url(#p)"/></svg>)svg",
      patterned);
    REQUIRE(get_root(patterned).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 0))));
    auto dashed = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><rect x="20"
      y="20" width="120" height="80" fill="none" stroke="#000000"
      stroke-width="4" stroke-dasharray="10 5" stroke-dashoffset="7"/>
      </svg>)svg", dashed);
    auto& dashes = *get_root(dashed).get_shape()->m_style.m_stroke;
    REQUIRE(dashes.m_dashes == std::vector<double>({10, 5}));
    REQUIRE(dashes.m_dash_offset == 7);
    auto graded = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs>
      <linearGradient id="lg" x1="0" y1="0" x2="1" y2="0"><stop offset="0"
      stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/>
      </linearGradient></defs><rect x="20" y="20" width="160" height="100"
      fill="none" stroke="url(#lg)" stroke-width="10"/></svg>)svg", graded);
    auto& style = get_root(graded).get_shape()->m_style;
    REQUIRE(style.m_fill == Fill(NoFill(SolidFill(Color(0, 0, 0, 0)))));
    REQUIRE(style.m_stroke->m_is_enabled);
    REQUIRE(style.m_stroke->m_color == make_color(112, 112, 112));
    REQUIRE(style.m_stroke->m_width == 10);
    auto text = Document();
    read_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text x="10"
      y="40" font-family="Arial" font-size="24" fill="#333">Hello <tspan
      font-weight="bold">World</tspan></text></svg>)svg", text);
    auto& hello = *get_root(text).get_text();
    REQUIRE(hello.m_base_style.m_font.m_style == "Bold");
    REQUIRE(hello.m_runs.front().m_style.m_font.m_style == "Regular");
    REQUIRE(hello.m_style.m_fill == Fill(SolidFill(make_color(51, 51, 51))));
    REQUIRE(!hello.m_style.m_stroke->m_is_enabled);
    REQUIRE(hello.m_style.m_stroke->m_color == Color(0, 0, 0, 0));
    REQUIRE(hello.m_style.m_stroke->m_width == 1);
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

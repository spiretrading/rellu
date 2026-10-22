#include <string>
#include <doctest/doctest.h>
#include "Rellu/Io/XdValues.hpp"
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto SHAPE_ID = *parse_node_id("9fac4ba0-f4aa-4529-ae35-4f279c49c1bd");

  std::string make_shape(const std::string& style) {
    return R"([{"type": "shape", "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd",
      "style": )" + style + R"(, "shape": {"type": "rect", "x": 0, "y": 0,
      "width": 10, "height": 10}}])";
  }

  const Style& get_style(const Document& document) {
    return document.get(SHAPE_ID).get_shape()->m_style;
  }
}

TEST_SUITE("StyleImporter") {
  TEST_CASE("solid_fill") {
    auto package = XdTestPackage();
    package.m_resources = parse_json(R"({"documentLibrary": {"elements": [
      {"id": "92d3f5d2-977c-413e-852c-4ef73e7354c9",
      "type": "application/vnd.adobe.element.color+dcx", "name": "Accent",
      "representations": [{"content": {"mode": "RGB",
      "value": {"r": 0, "g": 102, "b": 255}}}]}]}})");
    package.m_pasteboard = parse_json_array(make_shape(R"({"fill": {
      "type": "solid", "color": {"mode": "RGB", "value": {"r": 255, "g": 0,
      "b": 51}, "alpha": 0.5,
      "linkId": "92d3f5d2-977c-413e-852c-4ef73e7354c9"}}})"));
    auto document = Document();
    auto report = import_package(package, document);
    REQUIRE(report.m_unconsumed.empty());
    auto fill = std::get<SolidFill>(get_style(document).m_fill);
    REQUIRE(fill.m_color == make_color(255, 0, 51, 0.5));
    REQUIRE(fill.m_swatch ==
      *parse_node_id("92d3f5d2-977c-413e-852c-4ef73e7354c9"));
  }

  TEST_CASE("unresolved_swatch") {
    auto document = Document();
    import_nodes(make_shape(R"({"fill": {"type": "solid",
      "linkId": "afc3949c-826b-46b3-93fb-5f32fba65dc7",
      "color": {"mode": "RGB", "value": {"r": 0, "g": 0, "b": 0}}}})"),
      document);
    REQUIRE(std::get<SolidFill>(get_style(document).m_fill).m_swatch.
      is_nil());
  }

  TEST_CASE("no_fill") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"fill": {"type": "none",
      "uxdesign#type": "solid", "color": {"mode": "RGB", "value": {"r": 1,
      "g": 2, "b": 3}}}})"), document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(get_style(document).m_fill ==
      Fill(NoFill(FillPaint(SolidFill(make_color(1, 2, 3))))));
  }

  TEST_CASE("disabled_fills") {
    auto import_fill = [] (const std::string& fill) {
      auto document = Document();
      auto report = import_nodes(
        make_shape(R"({"fill": )" + fill + "}"), document);
      REQUIRE(report.m_unconsumed.empty());
      REQUIRE(report.m_warnings.empty());
      return get_style(document).m_fill;
    };
    REQUIRE(import_fill(R"({"type": "none", "color": {"mode": "RGB",
      "value": {"r": 4, "g": 5, "b": 6}}})") ==
      Fill(NoFill(FillPaint(SolidFill(make_color(4, 5, 6))))));
    REQUIRE(import_fill(R"({"type": "none"})") == Fill(NoFill()));
    auto gradient = import_fill(R"({"type": "none",
      "uxdesign#type": "gradient", "gradient": {"meta": {"ux": {
      "gradientResources": {"type": "linear", "stops": [{"offset": 0,
      "color": {"mode": "RGB", "value": {"r": 255, "g": 255, "b": 255}}},
      {"offset": 1, "color": {"mode": "RGB", "value": {"r": 0, "g": 0,
      "b": 0}}}]}}}, "x1": 0, "y1": 0, "x2": 1, "y2": 1,
      "units": "objectBoundingBox"}})");
    auto& none = std::get<NoFill>(gradient);
    REQUIRE(none.m_paint);
    REQUIRE(std::get<GradientFill>(*none.m_paint).m_stops.size() == 2);
    REQUIRE(enable_fill(gradient) == Fill(std::get<GradientFill>(
      *none.m_paint)));
  }

  TEST_CASE("linear_gradient") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"fill": {"type": "gradient",
      "gradient": {"meta": {"ux": {"gradientResources": {"type": "linear",
      "stops": [{"offset": 0, "color": {"mode": "RGB", "value": {"r": 255,
      "g": 255, "b": 255}, "alpha": 0}}, {"offset": 1, "color": {
      "mode": "RGB", "value": {"r": 0, "g": 0, "b": 0}}}]}}}, "x1": 0.5,
      "y1": 1, "x2": 0.5, "y2": 0, "units": "objectBoundingBox"}}})"),
      document);
    REQUIRE(report.m_unconsumed.empty());
    auto fill = std::get<GradientFill>(get_style(document).m_fill);
    REQUIRE(fill.m_units == GradientUnits::OBJECT_BOUNDING_BOX);
    REQUIRE(fill.m_geometry ==
      GradientGeometry(LinearGradient(Point(0.5, 1), Point(0.5, 0))));
    REQUIRE(fill.m_stops == std::vector<GradientStop>({
      GradientStop(0, Color(1, 1, 1, 0)), GradientStop(1, Color(0, 0, 0, 1))}));
    REQUIRE(fill.m_transform.is_identity());
  }

  TEST_CASE("radial_gradient") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"fill": {"type": "gradient",
      "gradient": {"meta": {"ux": {"gradientResources": {"type": "radial",
      "stops": []}}}, "cx": 0.25, "cy": 0.5, "r": 1.5, "fx": 0.1, "fy": 0.2,
      "units": "userSpaceOnUse"}}})"), document);
    REQUIRE(report.m_unconsumed.empty());
    auto fill = std::get<GradientFill>(get_style(document).m_fill);
    REQUIRE(fill.m_units == GradientUnits::USER_SPACE);
    REQUIRE(fill.m_geometry == GradientGeometry(
      RadialGradient(Point(0.25, 0.5), 1.5, Point(0.1, 0.2))));
  }

  TEST_CASE("angular_gradient") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"fill": {"type": "gradient",
      "gradient": {"meta": {"ux": {"gradientResources": {"type": "angular",
      "stops": [{"offset": 0, "color": {"mode": "RGB", "value": {"r": 52,
      "g": 120, "b": 246}}}, {"offset": 1, "color": {"mode": "RGB",
      "value": {"r": 26, "g": 60, "b": 123}}}]}, "x": 0.5, "y": 0.5,
      "rotation": 45}}, "units": "objectBoundingBox"}}})"), document);
    REQUIRE(report.m_unconsumed.empty());
    auto fill = std::get<GradientFill>(get_style(document).m_fill);
    REQUIRE(fill.m_geometry ==
      GradientGeometry(AngularGradient(Point(0.5, 0.5), 45)));
    REQUIRE(fill.m_stops.size() == 2);
  }

  TEST_CASE("pattern_fill") {
    auto package = XdTestPackage();
    package.m_entries.emplace_back(
      "resources/d3f2e8d04131be1354c699cd2ab595a0", QByteArray("\x89PNG"));
    package.m_pasteboard = parse_json_array(make_shape(R"({"fill": {
      "type": "pattern", "pattern": {"width": 400, "height": 300,
      "href": "C:\\image.png", "meta": {"ux": {"scaleBehavior": "cover",
      "uid": "d3f2e8d04131be1354c699cd2ab595a0", "offsetX": 0.1,
      "offsetY": -0.2, "scale": 1.5, "scaleX": 2, "scaleY": 3,
      "flipX": true, "hrefLastModifiedDate": 1636665130}}}}})"));
    auto document = Document();
    auto report = import_package(package, document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(report.m_warnings.empty());
    auto fill = std::get<ImageFill>(get_style(document).m_fill);
    REQUIRE(fill.m_image ==
      *parse_node_id("d3f2e8d04131be1354c699cd2ab595a0"));
    REQUIRE(fill.m_size == Size(400, 300));
    REQUIRE(fill.m_scale_behavior == ScaleBehavior::COVER);
    REQUIRE(fill.m_offset == Point(0.1, -0.2));
    REQUIRE(fill.m_scale == 1.5);
    REQUIRE(fill.m_horizontal_scale == 2);
    REQUIRE(fill.m_vertical_scale == 3);
    REQUIRE(fill.m_is_flipped_horizontally);
    REQUIRE(!fill.m_is_flipped_vertically);
  }

  TEST_CASE("missing_image") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"fill": {"type": "pattern",
      "pattern": {"width": 4, "height": 3, "meta": {"ux": {
      "scaleBehavior": "fill",
      "uid": "0f98f74c15623bc04a2ed0cae370d5ed"}}}}})"),
      document);
    REQUIRE(report.m_warnings.at("Missing image resource") == 1);
    REQUIRE(std::holds_alternative<ImageFill>(get_style(document).m_fill));
  }

  TEST_CASE("stroke") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"stroke": {"type": "solid",
      "color": {"mode": "RGB", "value": {"r": 255, "g": 170, "b": 0}},
      "width": 2, "align": "outside", "cap": "round", "join": "bevel",
      "miterLimit": 10, "dash": [6, 3], "dashOffset": 7}})"), document);
    REQUIRE(report.m_unconsumed.empty());
    auto& stroke = *get_style(document).m_stroke;
    REQUIRE(stroke.m_color == make_color(255, 170, 0));
    REQUIRE(stroke.m_width == 2);
    REQUIRE(stroke.m_alignment == StrokeAlignment::OUTSIDE);
    REQUIRE(stroke.m_cap == StrokeCap::ROUND);
    REQUIRE(stroke.m_join == StrokeJoin::BEVEL);
    REQUIRE(stroke.m_miter_limit == 10);
    REQUIRE(stroke.m_dashes == std::vector<double>({6, 3}));
    REQUIRE(stroke.m_dash_offset == 7);
    REQUIRE(stroke.m_is_enabled);
  }

  TEST_CASE("disabled_stroke") {
    auto document = Document();
    import_nodes(make_shape(R"({"stroke": {"type": "none", "color": {
      "mode": "RGB", "value": {"r": 0, "g": 0, "b": 0}}, "width": 1}})"),
      document);
    auto& stroke = *get_style(document).m_stroke;
    REQUIRE(!stroke.m_is_enabled);
    REQUIRE(stroke.m_alignment == StrokeAlignment::CENTER);
    REQUIRE(stroke.m_cap == StrokeCap::BUTT);
    REQUIRE(stroke.m_join == StrokeJoin::MITER);
    REQUIRE(stroke.m_miter_limit == 4);
  }

  TEST_CASE("effects") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"filters": [
      {"type": "dropShadow", "global": true, "params": {"dropShadows": [
      {"dx": 0, "dy": 3, "r": 6, "color": {"mode": "RGB", "value": {"r": 0,
      "g": 0, "b": 0}, "alpha": 0.25}}, {"dx": 1, "dy": 1, "r": 0,
      "color": {"mode": "RGB", "value": {"r": 255, "g": 255, "b": 255}}}]},
      "visible": false}, {"type": "dropShadow", "global": true, "params": {
      "dropShadows": [{"dx": 4, "dy": 4, "r": 4, "color": {"mode": "RGB",
      "value": {"r": 0, "g": 0, "b": 0}}}]}},
      {"type": "uxdesign#innerShadow", "global": true,
      "params": {"innerShadows": [{"dx": 2, "dy": 2, "r": 3, "color": {
      "mode": "RGB", "value": {"r": 0, "g": 0, "b": 0}}}]}},
      {"type": "uxdesign#blur", "global": true, "params": {"blurAmount": 10,
      "brightnessAmount": -5, "fillOpacity": 0.4,
      "backgroundEffect": true}}]})"), document);
    REQUIRE(report.m_unconsumed.empty());
    auto& effects = get_style(document).m_effects;
    REQUIRE(effects.size() == 3);
    REQUIRE(effects[0] == Effect(DropShadow(Point(0, 3), 6,
      Color(0, 0, 0, 0.25), false)));
    REQUIRE(effects[1] == Effect(InnerShadow(Point(2, 2), 3,
      Color(0, 0, 0, 1), true)));
    REQUIRE(effects[2] == Effect(Blur(10, -5, 0.4, true, true)));
  }

  TEST_CASE("unsupported_filter") {
    auto document = Document();
    auto report = import_nodes(make_shape(R"({"filters": [
      {"type": "uxdesign#glow", "params": {}}]})"), document);
    REQUIRE(get_style(document).m_effects.empty());
    REQUIRE(report.m_unconsumed.at("<shape>.style.filters[].type:str") == 1);
  }

  TEST_CASE("artboard_background") {
    auto package = XdTestPackage();
    package.m_frames = parse_json(R"({"39933d05-e40a-4c79-a8ec-deda4884ac82": {
      "x": 0, "y": 0, "width": 10, "height": 10, "name": "A"}})");
    package.m_artboards.push_back(parse_json(R"({"type": "artboard",
      "id": "39933d05-e40a-4c79-a8ec-deda4884ac82", "style": {"fill": {
      "type": "solid", "color": {"mode": "RGB", "value": {"r": 255, "g": 255,
      "b": 255}}}}, "artboard": {"children": [],
      "ref": "39933d05-e40a-4c79-a8ec-deda4884ac82"}})"));
    auto document = Document();
    import_package(package, document);
    auto artboard = document.get(document.get_canvas().front()).get_artboard();
    REQUIRE(artboard->m_background ==
      Fill(SolidFill(Color(1, 1, 1, 1), SwatchId())));
  }
}

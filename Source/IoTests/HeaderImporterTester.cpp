#include <doctest/doctest.h>
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto RECTANGLE_ID =
    *parse_node_id("9fac4ba0-f4aa-4529-ae35-4f279c49c1bd");
}

TEST_SUITE("HeaderImporter") {
  TEST_CASE("header") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "shape", "name": "Box",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd", "visible": false,
      "transform": {"a": 2, "b": 0, "c": 0, "d": 3, "tx": 10, "ty": 20},
      "style": {"opacity": 0.5, "blendMode": "multiply",
      "isolation": "isolate"},
      "meta": {"ux": {"locked": true, "markedForExport": true,
      "customConstraints": true, "constraintLeft": true,
      "constraintBottom": true, "constraintWidth": true,
      "hasCustomName": true, "nameL10N": "SHAPE_RECTANGLE", "rotation": 0,
      "aspectLock": {"width": 2, "height": 1}, "fixed": true}},
      "visualBounds": {"x": 0, "y": 0, "width": 1, "height": 1},
      "shape": {"type": "rect", "x": 0, "y": 0, "width": 1,
      "height": 1}}])", document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(report.m_warnings.empty());
    REQUIRE(document.get_canvas() == std::vector<NodeId>({RECTANGLE_ID}));
    auto& header = document.get(RECTANGLE_ID).m_header;
    REQUIRE(header.m_name == "Box");
    REQUIRE(!header.m_is_visible);
    REQUIRE(header.m_is_locked);
    REQUIRE(header.m_is_marked_for_export);
    REQUIRE(header.m_transform == Matrix(2, 0, 0, 3, 10, 20));
    REQUIRE(header.m_opacity == 0.5);
    REQUIRE(header.m_blend_mode == BlendMode::MULTIPLY);
    REQUIRE(header.m_is_isolated);
    auto& constraints = header.m_constraints;
    REQUIRE(constraints.m_is_enabled);
    REQUIRE(constraints.m_is_custom);
    REQUIRE(constraints.m_left);
    REQUIRE(!constraints.m_right);
    REQUIRE(!constraints.m_top);
    REQUIRE(constraints.m_bottom);
    REQUIRE(constraints.m_width);
    REQUIRE(!constraints.m_height);
    REQUIRE(header.m_is_fixed_position);
    REQUIRE(header.m_aspect_lock == Size(2, 1));
  }

  TEST_CASE("custom_name") {
    auto document = Document();
    import_nodes(R"([
      {"type": "shape", "name": "Rectangle 4",
       "id": "00000000-0000-0000-0000-000000000001",
       "meta": {"ux": {"nameL10N": "SHAPE_RECTANGLE"}},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 1, "height": 1}},
      {"type": "shape", "name": "Logo",
       "id": "00000000-0000-0000-0000-000000000002", "meta": {"ux": {}},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 1, "height": 1}},
      {"type": "shape", "name": "Rectangle 5",
       "id": "00000000-0000-0000-0000-000000000003",
       "meta": {"ux": {"nameL10N": "SHAPE_RECTANGLE",
         "hasCustomName": true}},
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 1, "height": 1}},
      {"type": "shape", "id": "00000000-0000-0000-0000-000000000004",
       "shape": {"type": "rect", "x": 0, "y": 0, "width": 1,
         "height": 1}}])", document);
    auto is_custom = [&] (const char* id) {
      return document.get(*parse_node_id(id)).m_header.m_has_custom_name;
    };
    REQUIRE(!is_custom("00000000-0000-0000-0000-000000000001"));
    REQUIRE(is_custom("00000000-0000-0000-0000-000000000002"));
    REQUIRE(is_custom("00000000-0000-0000-0000-000000000003"));
    REQUIRE(!is_custom("00000000-0000-0000-0000-000000000004"));
  }

  TEST_CASE("defaults") {
    auto document = Document();
    import_nodes(R"([{"type": "shape",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd",
      "meta": {"ux": {"constraintsDisabled": true}},
      "shape": {"type": "rect", "x": 0, "y": 0, "width": 1,
      "height": 1}}])", document);
    auto& header = document.get(RECTANGLE_ID).m_header;
    REQUIRE(header.m_name.empty());
    REQUIRE(header.m_is_visible);
    REQUIRE(!header.m_is_locked);
    REQUIRE(header.m_transform.is_identity());
    REQUIRE(header.m_opacity == 1);
    REQUIRE(header.m_blend_mode == BlendMode::NORMAL);
    REQUIRE(!header.m_is_isolated);
    REQUIRE(!header.m_constraints.m_is_enabled);
    REQUIRE(!header.m_is_fixed_position);
    REQUIRE(!header.m_aspect_lock);
  }

  TEST_CASE("unsupported_values") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "group",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd",
      "transform": {"0": [1, 0, 0, 0]},
      "style": {"blendMode": "pass-through", "isolation": "auto"}}])",
      document);
    auto& header = document.get(RECTANGLE_ID).m_header;
    REQUIRE(header.m_transform.is_identity());
    REQUIRE(header.m_blend_mode == BlendMode::NORMAL);
    REQUIRE(report.m_warnings.at("Unsupported transform") == 1);
    REQUIRE(report.m_unconsumed.at("<group>.transform.0[]") == 1);
    REQUIRE(report.m_unconsumed.at("<group>.style.blendMode:str") == 1);
    REQUIRE(report.m_unconsumed.at("<group>.style.isolation:str") == 1);
  }

  TEST_CASE("transform_3d") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "group",
      "id": "9fac4ba0-f4aa-4529-ae35-4f279c49c1bd",
      "transform": {"0": [1, 0, 0, -269.5], "1": [0, 0.98, 0.17, 463.25],
      "2": [0, -0.17, 0.98, 3.5], "3": [0, 0, 0, 1]},
      "meta": {"ux": {"rotationX": -10, "rotationY": 5,
      "perspectiveCenter": {"x": 92, "y": 21.5}}}}])", document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(report.m_warnings.empty());
    auto& header = document.get(RECTANGLE_ID).m_header;
    REQUIRE(header.m_transform == Matrix(1, 0, 0, 0.98, -269.5, 463.25));
    REQUIRE(header.m_transform_3d);
    auto& transform = *header.m_transform_3d;
    REQUIRE(transform.m_matrix == std::array<double, 16>({1, 0, 0, -269.5, 0,
      0.98, 0.17, 463.25, 0, -0.17, 0.98, 3.5, 0, 0, 0, 1}));
    REQUIRE(transform.m_rotation_x == -10);
    REQUIRE(transform.m_rotation_y == 5);
    REQUIRE(transform.m_perspective_center == Point(92, 21.5));
  }

  TEST_CASE("derived_id") {
    auto document = Document();
    import_nodes(R"([{"type": "group", "id": "8"}, {"type": "group",
      "id": "9"}])", document);
    auto& canvas = document.get_canvas();
    REQUIRE(canvas.size() == 2);
    REQUIRE(canvas[0] != canvas[1]);
    auto other = Document();
    import_nodes(R"([{"type": "group", "id": "8"}])", other);
    REQUIRE(other.get_canvas().front() == canvas[0]);
  }
}

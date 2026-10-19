#include <string>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Ui/PropertyDescriptor.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  QString get_text(
      const std::vector<PropertyDescriptor>& properties, const char* path) {
    auto property = find_property(properties, path);
    REQUIRE(property);
    return to_text(property->m_value);
  }

  bool is_read_only(
      const std::vector<PropertyDescriptor>& properties, const char* path) {
    auto property = find_property(properties, path);
    REQUIRE(property);
    return property->m_is_read_only;
  }

  std::vector<std::string> get_sections(
      const std::vector<PropertyDescriptor>& properties) {
    auto paths = std::vector<std::string>();
    for(auto& property : properties) {
      paths.push_back(property.m_path);
    }
    return paths;
  }

  NodeId add_shape(Document& document, const NodeId& parent,
      ShapeGeometry geometry) {
    auto shape = Shape();
    shape.m_geometry = std::move(geometry);
    auto transaction = Transaction(document);
    auto id = transaction.append(parent, make_header("Shape"), shape);
    transaction.commit();
    return id;
  }
}

TEST_SUITE("PropertyDescriptor") {
  TEST_CASE("artboard") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_artboard);
    REQUIRE(get_sections(properties) == std::vector<std::string>({"align",
      "boolean", "component", "transform", "layout", "appearance", "fill",
      "grid"}));
    REQUIRE(get_text(properties, "transform.x") == "100");
    REQUIRE(get_text(properties, "transform.y") == "50");
    REQUIRE(get_text(properties, "transform.width") == "375");
    REQUIRE(get_text(properties, "transform.height") == "812");
    REQUIRE(get_text(properties, "transform.orientation") == "Portrait");
    REQUIRE(!find_property(properties, "transform.rotation"));
    REQUIRE(get_text(properties, "layout.responsive") == "No");
    REQUIRE(get_text(properties, "layout.scrolling") == "None");
    REQUIRE(!find_property(properties, "appearance.opacity"));
    REQUIRE(get_text(properties, "fill") == "Solid");
    REQUIRE(get_text(properties, "fill.color") == "#FFFFFF");
    REQUIRE(get_text(properties, "grid.visible") == "No");
    REQUIRE(get_text(properties, "grid.type") == "Layout");
  }

  TEST_CASE("group") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_card);
    REQUIRE(get_sections(properties) == std::vector<std::string>({"align",
      "boolean", "component", "transform", "layout", "appearance", "fill",
      "stroke", "export"}));
    REQUIRE(find_property(properties, "component")->m_label == "Component");
    REQUIRE(get_text(properties, "transform.x") == "10");
    REQUIRE(get_text(properties, "transform.y") == "20");
    REQUIRE(get_text(properties, "transform.width") == "200");
    REQUIRE(get_text(properties, "transform.height") == "50");
    REQUIRE(get_text(properties, "transform.rotation") == "0");
    REQUIRE(get_text(properties, "transform.scroll") == "None");
    REQUIRE(get_text(properties, "appearance.opacity") == "1");
    REQUIRE(get_text(properties, "appearance.blend_mode") == "Pass Through");
    REQUIRE(find_property(properties, "fill")->m_is_mixed);
    REQUIRE(!is_read_only(properties, "fill.enabled"));
    REQUIRE(get_text(properties, "stroke.enabled") == "No");
    REQUIRE(get_text(properties, "export.marked") == "No");
  }

  TEST_CASE("component_instance") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_instance);
    REQUIRE(find_property(properties, "component")->m_label ==
      "Component (Instance)");
    REQUIRE(get_text(properties, "component.name") == "Button");
    REQUIRE(get_text(properties, "component.state") == "Default State");
    REQUIRE(get_text(properties, "transform.width") == "80");
    REQUIRE(get_text(properties, "fill.color") == "#0000FF");
  }

  TEST_CASE("shape") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_background).m_header;
      header.m_opacity = 0.5;
      header.m_blend_mode = BlendMode::MULTIPLY;
      header.m_transform = make_translation(4, 6) * make_rotation(0.5) *
        make_scale(2, 3);
      transaction.replace_header(header);
      auto shape = *document.get(ids.m_background).get_shape();
      shape.m_style.m_effects.push_back(
        DropShadow(Point(0, 3), 6, make_color(0, 0, 0, 0.25)));
      shape.m_style.m_effects.push_back(Blur(8, 0, 1, true));
      transaction.replace_payload(ids.m_background, shape);
      transaction.commit();
    }
    auto properties = extract_properties(document, ids.m_background);
    REQUIRE(get_sections(properties) == std::vector<std::string>({"align",
      "boolean", "component", "transform", "layout", "appearance", "geometry",
      "fill", "stroke", "effects", "export"}));
    REQUIRE(get_text(properties, "transform.x") == "4");
    REQUIRE(get_text(properties, "transform.y") == "6");
    REQUIRE(get_text(properties, "transform.width") == "100");
    REQUIRE(get_text(properties, "transform.rotation") == "28.65");
    REQUIRE(get_text(properties, "transform.fixed_position") == "No");
    REQUIRE(get_text(properties, "appearance.opacity") == "0.5");
    REQUIRE(get_text(properties, "appearance.blend_mode") == "Multiply");
    REQUIRE(get_text(properties, "geometry.radii") ==
      "Same radius for all corners");
    REQUIRE(get_text(properties, "geometry.radii.all") == "0");
    REQUIRE(get_text(properties, "fill") == "Solid");
    REQUIRE(get_text(properties, "fill.enabled") == "Yes");
    REQUIRE(get_text(properties, "fill.color") == "#FF0000");
    REQUIRE(get_text(properties, "stroke.enabled") == "No");
    REQUIRE(get_text(properties, "stroke.color") == "#707070");
    REQUIRE(get_text(properties, "stroke.width") == "1");
    REQUIRE(get_text(properties, "stroke.alignment") == "Center");
    REQUIRE(get_text(properties, "effects.inner_shadow") == "No");
    REQUIRE(!find_property(properties, "effects.inner_shadow.x"));
    REQUIRE(get_text(properties, "effects.drop_shadow") == "Yes");
    REQUIRE(get_text(properties, "effects.drop_shadow.x") == "0");
    REQUIRE(get_text(properties, "effects.drop_shadow.y") == "3");
    REQUIRE(get_text(properties, "effects.drop_shadow.blur") == "6");
    REQUIRE(get_text(properties, "effects.drop_shadow.color") ==
      "#000000 25%");
    REQUIRE(get_text(properties, "effects.blur") == "Yes");
    REQUIRE(get_text(properties, "effects.blur.type") == "Background Blur");
    REQUIRE(get_text(properties, "effects.blur.amount") == "8");
    REQUIRE(get_text(properties, "export.marked") == "No");
  }

  TEST_CASE("ellipse") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_ellipse);
    REQUIRE(get_sections(properties) == std::vector<std::string>({"align",
      "boolean", "component", "transform", "layout", "appearance", "fill",
      "stroke", "effects", "export"}));
    REQUIRE(get_text(properties, "transform.x") == "0");
    REQUIRE(get_text(properties, "transform.width") == "10");
    REQUIRE(get_text(properties, "transform.height") == "6");
    REQUIRE(get_text(properties, "fill") == "None");
    REQUIRE(get_text(properties, "fill.enabled") == "No");
    REQUIRE(!find_property(properties, "fill.color"));
  }

  TEST_CASE("line") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto line = add_shape(
      document, ids.m_artboard, LineShape(Point(0, 0), Point(200, 40)));
    auto properties = extract_properties(document, line);
    REQUIRE(get_sections(properties) == std::vector<std::string>({"align",
      "boolean", "component", "transform", "layout", "appearance", "stroke",
      "effects", "export"}));
    REQUIRE(is_read_only(properties, "stroke.alignment"));
    REQUIRE(is_read_only(properties, "stroke.join"));
    REQUIRE(!is_read_only(properties, "stroke.cap"));
    REQUIRE(!find_property(properties, "effects.inner_shadow"));
    REQUIRE(find_property(properties, "effects.drop_shadow"));
  }

  TEST_CASE("polygon") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto polygon = PolygonShape();
    polygon.m_corner_count = 6;
    polygon.m_size = Size(80, 80);
    auto id = add_shape(document, ids.m_artboard, polygon);
    auto properties = extract_properties(document, id);
    REQUIRE(get_text(properties, "geometry.corners") == "6");
    REQUIRE(get_text(properties, "geometry.corner_radius") == "0");
    REQUIRE(get_text(properties, "geometry.star_ratio") == "1");
    REQUIRE(is_read_only(properties, "geometry.star_ratio"));
    REQUIRE(!find_property(properties, "geometry.radii"));
  }

  TEST_CASE("boolean_group") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto compound = CompoundShape();
    compound.m_operation = CompoundOperation::SUBTRACT;
    auto id = add_shape(document, ids.m_artboard, compound);
    auto properties = extract_properties(document, id);
    REQUIRE(get_text(properties, "boolean.operation") == "Subtract");
    REQUIRE(!find_property(properties, "geometry"));
  }

  TEST_CASE("text") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_label);
    REQUIRE(get_sections(properties) == std::vector<std::string>({"align",
      "boolean", "component", "transform", "layout", "text", "appearance",
      "fill", "stroke", "effects", "export"}));
    REQUIRE(get_text(properties, "text.family") == "Roboto");
    REQUIRE(get_text(properties, "text.style") == "Medium");
    REQUIRE(get_text(properties, "text.size") == "14");
    REQUIRE(get_text(properties, "text.line_height") == "17");
    REQUIRE(get_text(properties, "text.alignment") == "Left");
    REQUIRE(get_text(properties, "text.frame") == "Fixed Size");
    REQUIRE(get_text(properties, "text.transform") == "None");
    REQUIRE(get_text(properties, "text.underline") == "No");
    REQUIRE(get_text(properties, "transform.height") == "40");
    REQUIRE(!is_read_only(properties, "transform.width"));
    REQUIRE(find_property(properties, "stroke.enabled"));
    REQUIRE(!find_property(properties, "stroke.width"));
  }

  TEST_CASE("point_text") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto text = make_text(u"Point");
    text.m_frame = PositionedFrame();
    auto header = make_header("Point");
    header.m_transform = make_translation(20, 40);
    auto transaction = Transaction(document);
    auto id = transaction.append(ids.m_artboard, header, text);
    transaction.commit();
    auto properties = extract_properties(document, id);
    auto bounds = estimate_text_bounds(text);
    REQUIRE(std::get<double>(find_property(properties, "transform.y")->
      m_value) == 40 + bounds.m_y);
    REQUIRE(bounds.m_y < 0);
    REQUIRE(get_text(properties, "text.frame") == "Auto Width");
    REQUIRE(is_read_only(properties, "transform.width"));
    REQUIRE(is_read_only(properties, "transform.height"));
  }

  TEST_CASE("reference") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_reference);
    REQUIRE(get_sections(properties) == std::vector<std::string>({"align",
      "boolean", "component", "transform", "layout", "appearance", "geometry",
      "fill", "stroke", "effects", "export"}));
    REQUIRE(get_text(properties, "fill.color") == "#0000FF");
    REQUIRE(get_text(properties, "stroke.enabled") == "Yes");
    REQUIRE(get_text(properties, "stroke.width") == "2");
    REQUIRE(get_text(properties, "stroke.color") == "#000000");
    REQUIRE(get_text(properties, "transform.width") == "80");
  }

  TEST_CASE("missing_node") {
    auto document = Document();
    build_test_document(document);
    REQUIRE(extract_properties(document, generate_node_id()).empty());
    REQUIRE(extract_properties(document, NodeId()).empty());
  }

  TEST_CASE("paths") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_card);
    auto x = find_property(properties, "transform.x");
    REQUIRE(x);
    REQUIRE(x->m_path == "transform.x");
    REQUIRE(x->m_label == "X");
    REQUIRE(std::get<double>(x->m_value) == 10);
    REQUIRE(!find_property(properties, "transform.z"));
    REQUIRE(!find_property(properties, "transform."));
    REQUIRE(!find_property(properties, "trans"));
  }

  TEST_CASE("blend_mode") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_background);
    auto choice = std::get<Choice>(
      find_property(properties, "appearance.blend_mode")->m_value);
    REQUIRE(choice.m_options.mid(0, 4) ==
      QStringList({"Normal", "Darken", "Multiply", "Color Burn"}));
    choice.m_index = 2;
    REQUIRE(get_blend_mode(choice) == BlendMode::MULTIPLY);
    choice.m_index = 15;
    REQUIRE(get_blend_mode(choice) == BlendMode::LUMINOSITY);
    choice.m_index = 16;
    REQUIRE(!get_blend_mode(choice));
    auto group = std::get<Choice>(find_property(
      extract_properties(document, ids.m_card),
      "appearance.blend_mode")->m_value);
    REQUIRE(get_blend_mode(group) == BlendMode::NORMAL);
    group.m_index = 3;
    REQUIRE(get_blend_mode(group) == BlendMode::MULTIPLY);
  }

  TEST_CASE("value_text") {
    REQUIRE(to_text(PropertyValue()) == "");
    REQUIRE(to_text(true) == "Yes");
    REQUIRE(to_text(false) == "No");
    REQUIRE(to_text(42) == "42");
    REQUIRE(to_text(1.5) == "1.5");
    REQUIRE(to_text(2.0) == "2");
    REQUIRE(to_text(-0.001) == "0");
    REQUIRE(to_text(1.0 / 3) == "0.33");
    REQUIRE(to_text(QString("text")) == "text");
    REQUIRE(to_text(make_color(255, 128, 0)) == "#FF8000");
    REQUIRE(to_text(make_color(255, 128, 0, 0.5)) == "#FF8000 50%");
    REQUIRE(to_text(Point(1, -2.5)) == "1, -2.5");
    REQUIRE(to_text(Size(3, 4)) == "3 x 4");
    REQUIRE(to_text(Choice(1, QStringList({"A", "B"}))) == "B");
    REQUIRE(to_text(Choice(5, QStringList({"A", "B"}))) == "5");
  }

  TEST_CASE("header_properties") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_background).m_header;
      header.m_aspect_lock = Size(100, 50);
      header.m_is_fixed_position = true;
      header.m_constraints.m_is_enabled = true;
      header.m_constraints.m_is_custom = true;
      header.m_constraints.m_left = true;
      transaction.replace_header(header);
      transaction.commit();
    }
    auto properties = extract_properties(document, ids.m_background);
    REQUIRE(get_text(properties, "transform.lock_aspect") == "Yes");
    REQUIRE(!is_read_only(properties, "transform.lock_aspect"));
    REQUIRE(get_text(properties, "transform.fixed_position") == "Yes");
    REQUIRE(!is_read_only(properties, "transform.fixed_position"));
    REQUIRE(get_text(properties, "layout.responsive.mode") == "Manual");
    REQUIRE(get_text(properties, "layout.responsive.left") == "Yes");
    REQUIRE(get_text(properties, "layout.responsive.right") == "No");
    REQUIRE(!is_read_only(properties, "layout.responsive.width"));
    REQUIRE(get_text(properties, "fill.paint") == "Solid");
    auto artboard = extract_properties(document, ids.m_artboard);
    REQUIRE(get_text(artboard, "transform.lock_aspect") == "No");
    REQUIRE(get_text(artboard, "layout.responsive") == "No");
    REQUIRE(!find_property(artboard, "layout.responsive.mode"));
    REQUIRE(to_text(Fill(GradientFill())) == "Gradient");
  }
}
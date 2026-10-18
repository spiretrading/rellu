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

  std::vector<QString> get_sections(
      const std::vector<PropertyDescriptor>& properties) {
    auto labels = std::vector<QString>();
    for(auto& property : properties) {
      labels.push_back(property.m_label);
    }
    return labels;
  }
}

TEST_SUITE("PropertyDescriptor") {
  TEST_CASE("artboard") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_artboard);
    REQUIRE(get_sections(properties) == std::vector<QString>({"Layer",
      "Transform", "Appearance", "Artboard", "Background"}));
    REQUIRE(get_text(properties, "layer.name") == "Home");
    REQUIRE(get_text(properties, "layer.kind") == "Artboard");
    REQUIRE(get_text(properties, "layer.children") == "3");
    REQUIRE(get_text(properties, "transform.x") == "100");
    REQUIRE(get_text(properties, "transform.y") == "50");
    REQUIRE(get_text(properties, "transform.width") == "375");
    REQUIRE(get_text(properties, "transform.height") == "812");
    REQUIRE(get_text(properties, "fill") == "Solid");
    REQUIRE(get_text(properties, "fill.color") == "#FFFFFF");
    REQUIRE(get_text(properties, "artboard.guides") == "0");
  }

  TEST_CASE("group") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_card);
    REQUIRE(get_sections(properties) == std::vector<QString>({"Layer",
      "Transform", "Appearance", "Roles"}));
    REQUIRE(get_text(properties, "layer.kind") == "Group");
    REQUIRE(get_text(properties, "layer.children") == "2");
    REQUIRE(get_text(properties, "transform.x") == "10");
    REQUIRE(get_text(properties, "transform.y") == "20");
    REQUIRE(get_text(properties, "transform.width") == "200");
    REQUIRE(get_text(properties, "transform.height") == "50");
    REQUIRE(get_text(properties, "transform.rotation") == "0");
    REQUIRE(!find_property(properties, "transform.scale"));
    REQUIRE(get_text(properties, "appearance.opacity") == "1");
    REQUIRE(get_text(properties, "appearance.blend_mode") == "Normal");
    REQUIRE(get_text(properties, "roles") == "None");
  }

  TEST_CASE("component_instance") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_instance);
    REQUIRE(get_text(properties, "layer.kind") == "Component");
    REQUIRE(get_text(properties, "roles.component.name") == "Button");
    REQUIRE(get_text(properties, "roles.component.state") == "Default");
    REQUIRE(get_text(properties, "roles.component.size") == "80 x 30");
    REQUIRE(get_text(properties, "transform.width") == "80");
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
    REQUIRE(get_sections(properties) == std::vector<QString>({"Layer",
      "Transform", "Appearance", "Geometry", "Fill", "Stroke", "Effects"}));
    REQUIRE(get_text(properties, "layer.kind") == "Rectangle");
    REQUIRE(get_text(properties, "transform.x") == "4");
    REQUIRE(get_text(properties, "transform.y") == "6");
    REQUIRE(get_text(properties, "transform.width") == "100");
    REQUIRE(get_text(properties, "transform.rotation") == "28.65");
    REQUIRE(get_text(properties, "transform.scale") == "2, 3");
    REQUIRE(get_text(properties, "appearance.opacity") == "0.5");
    REQUIRE(get_text(properties, "appearance.blend_mode") == "Multiply");
    REQUIRE(get_text(properties, "geometry.radii.top_left") == "0");
    REQUIRE(get_text(properties, "fill") == "Solid");
    REQUIRE(get_text(properties, "fill.color") == "#FF0000");
    REQUIRE(get_text(properties, "stroke") == "None");
    REQUIRE(get_text(properties, "effects") == "2");
    REQUIRE(find_property(properties, "effects.0")->m_label == "Drop Shadow");
    REQUIRE(get_text(properties, "effects.0.offset") == "0, 3");
    REQUIRE(get_text(properties, "effects.0.color") == "#000000 25%");
    REQUIRE(find_property(properties, "effects.1")->m_label ==
      "Background Blur");
    REQUIRE(get_text(properties, "effects.1.amount") == "8");
  }

  TEST_CASE("geometry") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_ellipse);
    REQUIRE(get_text(properties, "layer.name") == "Ellipse");
    REQUIRE(get_text(properties, "layer.kind") == "Ellipse");
    REQUIRE(get_text(properties, "geometry.center") == "5, 5");
    REQUIRE(get_text(properties, "geometry.radii") == "5 x 3");
    REQUIRE(get_text(properties, "transform.x") == "0");
    REQUIRE(get_text(properties, "transform.width") == "10");
    REQUIRE(get_text(properties, "transform.height") == "6");
    REQUIRE(get_text(properties, "fill") == "None");
  }

  TEST_CASE("text") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_label);
    REQUIRE(get_sections(properties) == std::vector<QString>({"Layer",
      "Transform", "Appearance", "Text", "Fill", "Stroke", "Effects"}));
    REQUIRE(get_text(properties, "layer.name") == "Hello world");
    REQUIRE(get_text(properties, "layer.kind") == "Text");
    REQUIRE(get_text(properties, "text.content") ==
      "Hello world Second line");
    REQUIRE(get_text(properties, "text.length") == "23");
    REQUIRE(get_text(properties, "text.font") == "Roboto Medium");
    REQUIRE(get_text(properties, "text.size") == "14");
    REQUIRE(get_text(properties, "text.runs") == "1");
    REQUIRE(get_text(properties, "text.alignment") == "Left");
    REQUIRE(get_text(properties, "text.line_height") == "Auto");
    REQUIRE(get_text(properties, "text.frame") == "Area");
    REQUIRE(get_text(properties, "text.frame_size") == "200 x 40");
    REQUIRE(get_text(properties, "text.stored_layout") == "No");
    REQUIRE(get_text(properties, "transform.height") == "40");
  }

  TEST_CASE("reference") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto properties = extract_properties(document, ids.m_reference);
    REQUIRE(get_sections(properties) == std::vector<QString>({"Layer",
      "Transform", "Appearance", "Geometry", "Fill", "Stroke", "Effects",
      "Reference"}));
    REQUIRE(get_text(properties, "layer.name") == "Fill");
    REQUIRE(get_text(properties, "layer.kind") == "Rectangle (reference)");
    REQUIRE(get_text(properties, "fill.color") == "#0000FF");
    REQUIRE(get_text(properties, "stroke.width") == "2");
    REQUIRE(get_text(properties, "stroke.alignment") == "Center");
    REQUIRE(get_text(properties, "reference.source") == "Fill");
    REQUIRE(get_text(properties, "reference.resolved") == "Yes");
    REQUIRE(get_text(properties, "reference.transform") == "No");
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
}

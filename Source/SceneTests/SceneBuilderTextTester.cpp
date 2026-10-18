#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

using namespace Rellu;

namespace {
  const auto TOLERANCE = 1.0 / 32;

  NodeHeader make_header(const Matrix& transform) {
    auto header = NodeHeader();
    header.m_transform = transform;
    return header;
  }

  Text make_text(Color color) {
    auto text = Text();
    text.m_content = Shared(std::u16string(u"Text"));
    text.m_base_style.m_font = Font("Arial", "Regular", "ArialMT", 16);
    text.m_base_style.m_color = color;
    return text;
  }

  const FillCommand& get_fill(const DrawList& list, std::size_t index) {
    return std::get<FillCommand>(list.at(index));
  }

  const Color& get_color(const FillCommand& command) {
    return std::get<SolidPaint>(command.m_paint).m_color;
  }
}

TEST_SUITE("SceneBuilder") {
  TEST_CASE("text") {
    auto document = Document();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), make_header(make_translation(5, 30)),
      make_text(make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto& fill = get_fill(list, 0);
    REQUIRE(fill.m_transform == make_translation(5, 30));
    REQUIRE(get_color(fill) == make_color(255, 0, 0));
    auto bounds = get_bounds(*fill.m_path);
    REQUIRE(bounds.get_left() >= 0);
    REQUIRE(bounds.get_bottom() <= 0.5);
    REQUIRE(bounds.get_top() < -10);
    REQUIRE(!fill.m_clip);
  }

  TEST_CASE("rasterized_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), make_header(make_translation(5, 30)),
      make_text(make_color(0, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto device = make_scale(2, 2) * make_translation(-1, -2);
    builder.set_device_transform(device);
    REQUIRE(builder.get_device_transform() == device);
    auto list = builder.build();
    REQUIRE(list.size() > 1);
    REQUIRE(list.size() <= 16);
    for(auto& command : list) {
      auto& fill = std::get<FillCommand>(command);
      REQUIRE(fill.m_transform == *invert(device));
      REQUIRE(get_color(fill).m_alpha > 0);
      REQUIRE(get_color(fill).m_alpha <= 1);
      for(auto& segment : fill.m_path->get_segments()) {
        for(auto& point : segment.m_points) {
          REQUIRE(point.m_x == static_cast<int>(point.m_x));
          REQUIRE(point.m_y == static_cast<int>(point.m_y));
        }
      }
    }
    builder.set_device_transform(std::nullopt);
    REQUIRE(builder.build().size() == 1);
  }

  TEST_CASE("outlined_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), make_header(make_translation(5, 30)),
      make_text(make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    builder.set_device_transform(make_scale(20, 20));
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto& fill = get_fill(list, 0);
    REQUIRE(fill.m_transform == make_translation(5, 30));
    REQUIRE(get_color(fill) == make_color(255, 0, 0));
  }

  TEST_CASE("single_precision_text") {
    auto build = [] (double y) {
      auto document = Document();
      auto transaction = Transaction(document);
      transaction.append(NodeId(), make_header(make_translation(0, y)),
        make_text(make_color(0, 0, 0)));
      transaction.commit();
      auto builder = SceneBuilder(document, TOLERANCE);
      builder.set_device_transform(make_translation(0, -4455.5));
      auto paths = std::vector<Path>();
      for(auto& command : builder.build()) {
        paths.push_back(*std::get<FillCommand>(command).m_path);
      }
      return paths;
    };
    REQUIRE(build(4567.999889373779) == build(4568));
    REQUIRE(build(4567.9) != build(4568));
  }

  TEST_CASE("text_effects") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(make_color(0, 0, 0));
    auto shadow = DropShadow();
    shadow.m_offset = Point(0, 2);
    shadow.m_blur = 3;
    shadow.m_color = Color(0, 0, 0, 0.5);
    text.m_style.m_effects.push_back(shadow);
    auto inner = InnerShadow();
    inner.m_offset = Point(1, 0);
    inner.m_blur = 1;
    inner.m_color = Color(1, 0, 0, 1);
    text.m_style.m_effects.push_back(inner);
    transaction.append(NodeId(), make_header(make_scale(2, 2)), text);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 5);
    auto& outer = std::get<BeginLayerCommand>(list[0]);
    REQUIRE(outer.m_drop_shadows.size() == 1);
    REQUIRE(outer.m_drop_shadows[0].m_offset == Point(0, 4));
    REQUIRE(outer.m_drop_shadows[0].m_radius == 6);
    REQUIRE(outer.m_inner_shadows.empty());
    auto& layer = std::get<BeginLayerCommand>(list[1]);
    REQUIRE(layer.m_inner_shadows.size() == 1);
    REQUIRE(layer.m_inner_shadows[0].m_offset == Point(2, 0));
    REQUIRE(layer.m_drop_shadows.empty());
    REQUIRE(std::holds_alternative<FillCommand>(list[2]));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[3]));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[4]));
  }

  TEST_CASE("text_clip") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(make_color(0, 0, 0));
    text.m_frame = AreaFrame(Size(20, 10));
    transaction.append(NodeId(), make_header(make_translation(5, 6)), text);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    REQUIRE(get_fill(list, 0).m_clip == Rect(5, 6, 20, 10));
  }
}

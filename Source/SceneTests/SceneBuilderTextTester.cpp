#include <variant>
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
    auto bounds = get_bounds(fill.m_path);
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
      for(auto& segment : fill.m_path.get_segments()) {
        for(auto& point : segment.m_points) {
          REQUIRE(point.m_x == static_cast<int>(point.m_x));
          REQUIRE(point.m_y == static_cast<int>(point.m_y));
        }
      }
    }
    builder.set_device_transform(std::nullopt);
    REQUIRE(builder.build().size() == 1);
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

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Scene/Compositing.hpp"

using namespace Rellu;

namespace {
  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  FillCommand make_fill(Color color, Matrix transform = Matrix()) {
    return FillCommand(share(make_rectangle(Rect(0, 0, 10, 10))), transform,
      SolidPaint(color), Rect(0, 0, 100, 100));
  }

  const Color& get_color(const DrawCommand& command) {
    return std::get<SolidPaint>(std::get<FillCommand>(command).m_paint).m_color;
  }
}

TEST_SUITE("Compositing") {
  TEST_CASE("plain_style") {
    auto list = DrawList();
    add_styled(list, make_fill(make_color(255, 0, 0)),
      make_fill(make_color(0, 0, 255)), {});
    REQUIRE(list.size() == 2);
    REQUIRE(get_color(list[0]) == make_color(255, 0, 0));
    REQUIRE(get_color(list[1]) == make_color(0, 0, 255));
    add_styled(list, std::nullopt, std::nullopt, {});
    REQUIRE(list.size() == 2);
  }

  TEST_CASE("effect_order") {
    auto list = DrawList();
    auto effects = std::vector<Effect>({
      DropShadow(Point(0, 3), 6, make_color(0, 0, 0, 0.16)),
      DropShadow(Point(1, 1), 2, make_color(0, 0, 0, 0.5), false),
      InnerShadow(Point(0, -2), 1, make_color(0, 0, 0)),
      Blur(10, 20, 0.5, true),
      Blur(2)});
    add_styled(list, make_fill(make_color(255, 0, 0, 0.8), make_scale(2, 2)),
      make_fill(make_color(0, 0, 255), make_scale(2, 2)), effects);
    REQUIRE(list.size() == 7);
    auto& outer = std::get<BeginLayerCommand>(list[0]);
    REQUIRE(outer.m_drop_shadows.size() == 1);
    REQUIRE(outer.m_drop_shadows[0].m_offset == Point(0, 6));
    REQUIRE(outer.m_drop_shadows[0].m_radius == 12);
    REQUIRE(outer.m_inner_shadows.empty());
    REQUIRE(outer.m_blur == 4);
    REQUIRE(outer.m_clip == Rect(0, 0, 100, 100));
    auto& backdrop = std::get<BackdropBlurCommand>(list[1]);
    REQUIRE(backdrop.m_radius == 20);
    REQUIRE(backdrop.m_brightness == 0.2);
    auto& inner = std::get<BeginLayerCommand>(list[2]);
    REQUIRE(inner.m_inner_shadows.size() == 1);
    REQUIRE(inner.m_inner_shadows[0].m_offset == Point(0, -4));
    REQUIRE(inner.m_drop_shadows.empty());
    REQUIRE(get_color(list[3]) == make_color(255, 0, 0, 0.4));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[4]));
    REQUIRE(get_color(list[5]) == make_color(0, 0, 255));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[6]));
  }

  TEST_CASE("backdrop_brightness") {
    auto get_brightness = [] (double amount) {
      auto list = DrawList();
      add_styled(list, make_fill(make_color(255, 255, 255, 0)), std::nullopt,
        {Blur(5, amount, 0, true)});
      for(auto& command : list) {
        if(auto backdrop = std::get_if<BackdropBlurCommand>(&command)) {
          return backdrop->m_brightness;
        }
      }
      return 2.0;
    };
    REQUIRE(get_brightness(0) == 0);
    REQUIRE(get_brightness(3) == 8.0 / 255);
    REQUIRE(get_brightness(-3) == -8.0 / 255);
    REQUIRE(get_brightness(7) == 18.0 / 255);
    REQUIRE(get_brightness(100) == get_brightness(50));
    REQUIRE(get_brightness(-100) == -127.0 / 255);
  }

  TEST_CASE("mask") {
    auto mask = Mask();
    mask.m_shapes.push_back(MaskShape(make_translation(5, 5),
      RectangleShape(Rect(0, 0, 20, 10), CornerRadii(0, 0, 0, 0))));
    auto clip = std::optional<Rect>(Rect(0, 0, 30, 30));
    REQUIRE(!make_mask(mask, make_translation(1, 2), 0.1, clip));
    REQUIRE(clip == Rect(6, 7, 20, 10));
    auto rotated = std::optional<Rect>();
    auto layer = make_mask(mask, make_rotation(0.5), 0.1, rotated);
    REQUIRE(layer);
    REQUIRE(!rotated);
    REQUIRE(layer->m_transform == make_rotation(0.5) * make_translation(5, 5));
    mask.m_shapes.push_back(MaskShape(Matrix(),
      EllipseShape(Point(0, 0), 4, 4)));
    auto united = make_mask(mask, Matrix(), 0.01, rotated);
    REQUIRE(united);
    REQUIRE(contains(*united->m_path, Point(0, 0)));
    REQUIRE(contains(*united->m_path, Point(20, 10)));
    REQUIRE(!contains(*united->m_path, Point(20, 2)));
  }

  TEST_CASE("opacity") {
    auto header = NodeHeader();
    header.m_opacity = 0.5;
    auto single = DrawList({make_fill(make_color(255, 0, 0))});
    add_composite(single, 0, header, std::nullopt, std::nullopt);
    REQUIRE(single.size() == 1);
    REQUIRE(get_color(single[0]) == make_color(255, 0, 0, 0.5));
    auto pair = DrawList({make_fill(make_color(255, 0, 0))});
    pair.push_back(make_fill(make_color(0, 255, 0)));
    pair.push_back(make_fill(make_color(0, 0, 255)));
    add_composite(pair, 1, header, std::nullopt, Rect(0, 0, 5, 5));
    REQUIRE(pair.size() == 5);
    REQUIRE(get_color(pair[0]) == make_color(255, 0, 0));
    auto& layer = std::get<BeginLayerCommand>(pair[1]);
    REQUIRE(layer.m_opacity == 0.5);
    REQUIRE(layer.m_clip == Rect(0, 0, 5, 5));
    REQUIRE(std::holds_alternative<EndLayerCommand>(pair[4]));
    add_composite(pair, 1, header, std::nullopt, std::nullopt);
    REQUIRE(pair.size() == 5);
    REQUIRE(std::get<BeginLayerCommand>(pair[1]).m_opacity == 0.25);
    header.m_opacity = 1;
    add_composite(pair, 0, header, std::nullopt, std::nullopt);
    REQUIRE(pair.size() == 5);
  }

  TEST_CASE("blend_mode") {
    auto header = NodeHeader();
    header.m_blend_mode = BlendMode::MULTIPLY;
    auto list = DrawList({make_fill(make_color(255, 0, 0))});
    add_composite(list, 0, header, std::nullopt, std::nullopt);
    REQUIRE(list.size() == 3);
    REQUIRE(std::get<BeginLayerCommand>(list[0]).m_blend_mode ==
      BlendMode::MULTIPLY);
    auto group = NodeHeader();
    group.m_opacity = 0.5;
    add_composite(list, 0, group, std::nullopt, std::nullopt);
    REQUIRE(list.size() == 5);
    REQUIRE(std::get<BeginLayerCommand>(list[0]).m_opacity == 0.5);
    REQUIRE(std::get<BeginLayerCommand>(list[1]).m_opacity == 1);
  }

  TEST_CASE("isolation") {
    auto header = NodeHeader();
    header.m_is_isolated = true;
    auto plain = DrawList({make_fill(make_color(255, 0, 0))});
    plain.push_back(make_fill(make_color(0, 255, 0)));
    add_composite(plain, 0, header, std::nullopt, std::nullopt);
    REQUIRE(plain.size() == 2);
    auto blending = DrawList({make_fill(make_color(255, 0, 0))});
    auto multiply = NodeHeader();
    multiply.m_blend_mode = BlendMode::MULTIPLY;
    blending.push_back(make_fill(make_color(0, 255, 0)));
    add_composite(blending, 1, multiply, std::nullopt, std::nullopt);
    add_composite(blending, 0, header, std::nullopt, std::nullopt);
    REQUIRE(blending.size() == 6);
    auto& layer = std::get<BeginLayerCommand>(blending[0]);
    REQUIRE(layer.m_blend_mode == BlendMode::NORMAL);
    REQUIRE(layer.m_opacity == 1);
  }

  TEST_CASE("layer_mask") {
    auto list = DrawList({make_fill(make_color(255, 0, 0))});
    add_composite(list, 0, NodeHeader(),
      LayerMask(share(make_circle(Point(5, 5), 5)), Matrix()), std::nullopt);
    REQUIRE(list.size() == 3);
    REQUIRE(std::get<BeginLayerCommand>(list[0]).m_mask);
  }
}

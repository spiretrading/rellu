#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/Drawing.hpp"

using namespace Rellu;

namespace {
  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  DrawCommand make_fill(const Rect& rect,
      std::optional<Rect> clip = std::nullopt) {
    return FillCommand(share(make_rectangle(rect)), Matrix(),
      SolidPaint(make_color(255, 0, 0)), clip);
  }

  BeginLayerCommand make_shadow(Point offset, double radius) {
    auto layer = BeginLayerCommand();
    layer.m_drop_shadows.push_back(
      Shadow(offset, radius, make_color(0, 0, 0, 0.5)));
    return layer;
  }

  auto make_list(std::vector<DrawCommand> commands) {
    return std::make_shared<const DrawList>(std::move(commands));
  }

  bool is_touching(const std::vector<DrawRegion>& regions, const Rect& rect) {
    return std::any_of(regions.begin(), regions.end(),
      [&] (const auto& region) {
        return region.m_bounds.intersects(rect);
      });
  }
}

TEST_SUITE("Drawing") {
  TEST_CASE("query") {
    auto first = make_list({make_fill(Rect(0, 0, 10, 10)),
      make_fill(Rect(100, 0, 10, 10)), make_shadow(Point(0, 40), 2),
      make_fill(Rect(200, 0, 10, 10)), EndLayerCommand()});
    auto second = make_list({make_fill(Rect(0, 100, 10, 10))});
    auto drawing = Drawing();
    drawing.set_lists({first, second});
    REQUIRE(drawing.get_lists().size() == 2);
    auto commands = DrawSequence();
    auto area = drawing.query(Rect(95, 0, 20, 20), 1, commands);
    REQUIRE(area == Rect(95, 0, 20, 20));
    REQUIRE(commands == DrawSequence({&(*first)[1]}));
    drawing.query(Rect(195, 35, 30, 20), 1, commands);
    REQUIRE(commands ==
      DrawSequence({&(*first)[2], &(*first)[3], &(*first)[4]}));
    drawing.query(Rect(0, 0, 300, 300), 1, commands);
    REQUIRE(commands == DrawSequence({&(*first)[0], &(*first)[1],
      &(*first)[2], &(*first)[3], &(*first)[4], &(*second)[0]}));
    drawing.query(Rect(500, 500, 1, 1), 1, commands);
    REQUIRE(commands.empty());
  }

  TEST_CASE("backdrop") {
    auto list = make_list({make_fill(Rect(0, 0, 100, 100)),
      make_fill(Rect(300, 0, 10, 10)), BackdropBlurCommand(
        share(make_rectangle(Rect(40, 40, 20, 20))), Matrix(), 4, 0,
        std::nullopt)});
    auto drawing = Drawing();
    drawing.set_lists({list});
    auto commands = DrawSequence();
    auto area = drawing.query(Rect(55, 55, 2, 2), 1, commands);
    REQUIRE(area.contains(Rect(28, 28, 44, 44)));
    REQUIRE(commands == DrawSequence({&(*list)[0], &(*list)[2]}));
  }

  TEST_CASE("readers") {
    auto blur = [] (const Rect& rect) {
      return BackdropBlurCommand(share(make_rectangle(rect)), Matrix(), 2, 0,
        std::nullopt);
    };
    auto first = make_list({make_fill(Rect(0, 0, 400, 100)),
      blur(Rect(0, 0, 50, 50)), blur(Rect(40, 0, 50, 50))});
    auto second = make_list({blur(Rect(300, 0, 50, 50)),
      make_shadow(Point(0, 0), 2), blur(Rect(150, 0, 50, 50)),
      EndLayerCommand()});
    auto drawing = Drawing();
    drawing.set_lists({first, second});
    auto readers = drawing.get_readers(
      {DrawRegion(Rect(10, 10, 5, 5), 0)}, 100);
    REQUIRE(readers.size() == 2);
    REQUIRE(is_touching(readers, Rect(0, 0, 50, 50)));
    REQUIRE(is_touching(readers, Rect(80, 0, 10, 50)));
    REQUIRE(!is_touching(readers, Rect(150, 0, 50, 50)));
    REQUIRE(!is_touching(readers, Rect(300, 0, 50, 50)));
    REQUIRE(drawing.get_readers(
      {DrawRegion(Rect(160, 10, 5, 5), 0)}, 100).empty());
    REQUIRE(drawing.get_readers(
      {DrawRegion(Rect(310, 10, 5, 5), 0)}, 100).size() == 1);
  }

  TEST_CASE("damage") {
    auto a = make_fill(Rect(0, 0, 10, 10));
    auto c = make_fill(Rect(0, 200, 10, 10));
    auto first = make_list({a, make_fill(Rect(100, 0, 10, 10))});
    auto second = make_list({c});
    auto drawing = Drawing();
    auto damage = drawing.set_lists({first, second});
    REQUIRE(is_touching(damage, Rect(0, 0, 10, 10)));
    REQUIRE(is_touching(damage, Rect(0, 200, 10, 10)));
    auto edited = make_list({a, make_fill(Rect(150, 0, 10, 10))});
    damage = drawing.set_lists({edited, second});
    REQUIRE(is_touching(damage, Rect(100, 0, 10, 10)));
    REQUIRE(is_touching(damage, Rect(150, 0, 10, 10)));
    REQUIRE(!is_touching(damage, Rect(0, 0, 10, 10)));
    REQUIRE(!is_touching(damage, Rect(0, 200, 10, 10)));
    damage = drawing.set_lists({second});
    REQUIRE(is_touching(damage, Rect(0, 0, 10, 10)));
    REQUIRE(!is_touching(damage, Rect(0, 200, 10, 10)));
    REQUIRE(drawing.set_lists({second}).empty());
    damage = drawing.set_lists({make_list({c}), second});
    REQUIRE(is_touching(damage, Rect(0, 200, 10, 10)));
  }

  TEST_CASE("bounds") {
    auto drawing = Drawing();
    REQUIRE(drawing.get_bounds().is_empty());
    drawing.set_lists({make_list({make_fill(Rect(0, 0, 10, 10), Rect(5, 5,
      100, 100)), make_shadow(Point(50, 50), 4),
      make_fill(Rect(20, 0, 10, 10)), EndLayerCommand()})});
    REQUIRE(drawing.get_bounds() == Rect(5, 0, 25, 10));
    REQUIRE(drawing.get_region(0).m_bounds.contains(Rect(70, 50, 10, 10)));
  }

  TEST_CASE("bounds_hint") {
    auto fill = FillCommand(share(make_rectangle(Rect(0, 0, 10, 10))),
      make_scale(2, 2), SolidPaint(make_color(255, 0, 0)), std::nullopt,
      std::nullopt, Rect(1, 2, 3, 4));
    auto drawing = Drawing();
    drawing.set_lists({make_list({fill})});
    REQUIRE(drawing.get_bounds() == Rect(2, 4, 6, 8));
    REQUIRE(drawing.get_region(0).m_bounds == Rect(2, 4, 6, 8));
  }

  TEST_CASE("pixels") {
    auto region = DrawRegion(Rect(1.5, 2.25, 3, 4), 2);
    REQUIRE(to_pixels(region, make_translation(1, 0) * make_scale(2, 2)) ==
      QRect(2, 2, 10, 13));
    REQUIRE(to_pixels(DrawRegion(Rect(0, 0, 0, 0), 2), Matrix()).isEmpty());
  }
}

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"

using namespace Rellu;

namespace {
  auto rasterize(Path path, QSize size, Matrix view = Matrix(),
      std::optional<Rect> clip = std::nullopt) {
    auto list = DrawList();
    list.push_back(FillCommand(std::move(path), Matrix(),
      SolidPaint(make_color(0, 0, 0)), clip));
    auto scene = StripScene();
    auto rasterizer = StripRasterizer();
    rasterizer.rasterize(list, view, size, scene);
    return scene;
  }

  auto get_coverage(const StripScene& scene, QSize size) {
    auto coverage = std::vector<double>(size.width() * size.height());
    for(auto& strip : scene.m_strips) {
      for(auto y = 0; y != int(strip.m_height); ++y) {
        for(auto x = 0; x != int(strip.m_width); ++x) {
          auto value = 1.0;
          if(strip.m_column >= 0) {
            auto texel = scene.m_atlas[
              std::size_t(strip.m_row) * STRIP_ATLAS_WIDTH +
                std::size_t(strip.m_column) + x];
            value = ((texel >> (8 * y)) & 0xFF) / 255.0;
          }
          coverage[(int(strip.m_y) + y) * size.width() + int(strip.m_x) + x] +=
            value;
        }
      }
    }
    return coverage;
  }

  auto get_maximum_error(const std::vector<double>& coverage, QSize size,
      auto expected) {
    auto error = 0.0;
    for(auto y = 0; y != size.height(); ++y) {
      for(auto x = 0; x != size.width(); ++x) {
        error = std::max(error,
          std::abs(coverage[y * size.width() + x] - expected(x, y)));
      }
    }
    return error;
  }

  auto get_sampled_coverage(const Path& path, int x, int y) {
    auto count = 0;
    for(auto i = 0; i != 64; ++i) {
      for(auto j = 0; j != 64; ++j) {
        if(contains(path, Point(x + (j + 0.5) / 64, y + (i + 0.5) / 64))) {
          ++count;
        }
      }
    }
    return count / 4096.0;
  }

  auto count_spans(const StripScene& scene) {
    return std::count_if(scene.m_strips.begin(), scene.m_strips.end(),
      [] (const auto& strip) {
        return strip.m_column < 0;
      });
  }
}

TEST_SUITE("StripRasterizer") {
  TEST_CASE("pixel_aligned_rectangle") {
    auto size = QSize(16, 64);
    auto scene = rasterize(make_rectangle(Rect(1, 4, 14, 56)), size);
    auto coverage = get_coverage(scene, size);
    REQUIRE(get_maximum_error(coverage, size, [] (int x, int y) {
      return x >= 1 && x < 15 && y >= 4 && y < 60 ? 1.0 : 0.0;
    }) == 0);
    REQUIRE(count_spans(scene) == 1);
  }

  TEST_CASE("vertical_span_merging") {
    auto size = QSize(16, 64);
    auto scene = rasterize(make_rectangle(Rect(1, 0.5, 14, 63)), size);
    auto coverage = get_coverage(scene, size);
    REQUIRE(get_maximum_error(coverage, size, [] (int x, int y) {
      if(x < 1 || x >= 15) {
        return 0.0;
      } else if(y == 0 || y == 63) {
        return 0.5;
      }
      return 1.0;
    }) < 0.5 / 255);
    REQUIRE(count_spans(scene) == 1);
    auto span = std::find_if(scene.m_strips.begin(), scene.m_strips.end(),
      [] (const auto& strip) {
        return strip.m_column < 0;
      });
    REQUIRE(span->m_y == 4);
    REQUIRE(span->m_height == 56);
  }

  TEST_CASE("exact_area") {
    auto size = QSize(8, 8);
    auto triangle = Path();
    triangle.move_to(Point(0, 0));
    triangle.line_to(Point(6, 0));
    triangle.line_to(Point(0, 6));
    triangle.close();
    auto coverage = get_coverage(rasterize(triangle, size), size);
    REQUIRE(get_maximum_error(coverage, size, [] (int x, int y) {
      if(x + y < 5) {
        return 1.0;
      } else if(x + y == 5) {
        return 0.5;
      }
      return 0.0;
    }) < 0.5 / 255);
    auto shallow = Path();
    shallow.move_to(Point(0, 1));
    shallow.line_to(Point(8, 1));
    shallow.line_to(Point(8, 2));
    shallow.close();
    auto shallow_coverage = get_coverage(rasterize(shallow, size), size);
    REQUIRE(get_maximum_error(shallow_coverage, size, [] (int x, int y) {
      if(y != 1) {
        return 0.0;
      }
      return (2 * x + 1) / 16.0;
    }) < 0.5 / 255);
  }

  TEST_CASE("fill_rules") {
    auto size = QSize(12, 12);
    for(auto rule : {FillRule::NON_ZERO, FillRule::EVEN_ODD}) {
      auto path = Path(rule);
      for(auto& rectangle : {Rect(1, 1, 6, 6), Rect(4.5, 4.5, 6, 6)}) {
        path.move_to(rectangle.get_top_left());
        path.line_to(Point(rectangle.get_right(), rectangle.get_top()));
        path.line_to(rectangle.get_bottom_right());
        path.line_to(Point(rectangle.get_left(), rectangle.get_bottom()));
        path.close();
      }
      auto coverage = get_coverage(rasterize(path, size), size);
      REQUIRE(get_maximum_error(coverage, size, [&] (int x, int y) {
        auto first = x >= 1 && x < 7 && y >= 1 && y < 7 ? 1.0 : 0.0;
        auto second = std::max(0.0, std::min(x + 1.0, 10.5) -
          std::max(double(x), 4.5)) * std::max(0.0,
            std::min(y + 1.0, 10.5) - std::max(double(y), 4.5));
        if(rule == FillRule::NON_ZERO) {
          return std::min(1.0, first + second);
        }
        return std::abs(first - second);
      }) < 0.5 / 255);
    }
  }

  TEST_CASE("clipping") {
    auto size = QSize(10, 10);
    auto scene = rasterize(make_rectangle(Rect(-50, -50, 100, 100)), size);
    auto coverage = get_coverage(scene, size);
    REQUIRE(get_maximum_error(coverage, size, [] (int x, int y) {
      return 1.0;
    }) == 0);
    REQUIRE(scene.m_strips.size() == 1);
    auto wedge = Path();
    wedge.move_to(Point(-20, 1.5));
    wedge.line_to(Point(40, 6));
    wedge.line_to(Point(-20, 10.5));
    wedge.close();
    auto wedge_coverage = get_coverage(rasterize(wedge, size), size);
    REQUIRE(get_maximum_error(wedge_coverage, size, [&] (int x, int y) {
      return get_sampled_coverage(wedge, x, y);
    }) < 1.0 / 40);
    REQUIRE(get_coverage(rasterize(make_rectangle(Rect(10, 0, 5, 5)), size),
      size) == std::vector<double>(100));
  }

  TEST_CASE("clip") {
    auto size = QSize(12, 10);
    auto clip = Rect(2.5, 1.25, 6, 5.5);
    auto overlap = [] (double low, double high, int pixel) {
      return std::max(0.0,
        std::min(high, pixel + 1.0) - std::max(low, 1.0 * pixel));
    };
    auto coverage = get_coverage(
      rasterize(make_rectangle(Rect(-5, -5, 30, 30)), size, Matrix(), clip),
      size);
    REQUIRE(get_maximum_error(coverage, size, [&] (int x, int y) {
      return overlap(clip.get_left(), clip.get_right(), x) *
        overlap(clip.get_top(), clip.get_bottom(), y);
    }) < 0.5 / 255);
    auto wedge = Path();
    wedge.move_to(Point(-20, 1.5));
    wedge.line_to(Point(10, 5.5));
    wedge.line_to(Point(-20, 9.5));
    wedge.close();
    auto wedge_coverage =
      get_coverage(rasterize(wedge, size, Matrix(), clip), size);
    REQUIRE(get_maximum_error(wedge_coverage, size, [&] (int x, int y) {
      auto count = 0;
      for(auto i = 0; i != 64; ++i) {
        for(auto j = 0; j != 64; ++j) {
          auto point = Point(x + (j + 0.5) / 64, y + (i + 0.5) / 64);
          if(clip.contains(point) && contains(wedge, point)) {
            ++count;
          }
        }
      }
      return count / 4096.0;
    }) < 1.0 / 40);
    auto scaled = get_coverage(rasterize(make_rectangle(Rect(-5, -5, 30, 30)),
      size, make_scale(2, 2), Rect(1, 1, 2, 1.5)), size);
    REQUIRE(get_maximum_error(scaled, size, [] (int x, int y) {
      return x >= 2 && x < 6 && y >= 2 && y < 5 ? 1.0 : 0.0;
    }) == 0);
    REQUIRE(rasterize(make_rectangle(Rect(0, 0, 4, 4)), size, Matrix(),
      Rect(6, 6, 2, 2)).m_strips.empty());
  }

  TEST_CASE("view") {
    auto size = QSize(20, 20);
    auto scene = rasterize(make_rectangle(Rect(1, 2, 3, 4)), size,
      make_translation(1, 1) * make_scale(2, 2));
    auto coverage = get_coverage(scene, size);
    REQUIRE(get_maximum_error(coverage, size, [] (int x, int y) {
      return x >= 3 && x < 9 && y >= 5 && y < 13 ? 1.0 : 0.0;
    }) == 0);
  }
}

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"

using namespace Rellu;

namespace {
  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  auto rasterize(Path path, QSize size, Matrix view = Matrix(),
      std::optional<Rect> clip = std::nullopt) {
    auto list = DrawList();
    list.push_back(FillCommand(share(std::move(path)), Matrix(),
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
  }

  TEST_CASE("short_path") {
    auto size = QSize(8, 8);
    auto shallow = Path();
    shallow.move_to(Point(0, 1));
    shallow.line_to(Point(8, 1));
    shallow.line_to(Point(8, 2));
    shallow.close();
    auto coverage = get_coverage(rasterize(shallow, size), size);
    REQUIRE(get_maximum_error(coverage, size, [] (int x, int y) {
      if(y != 1) {
        return 0.0;
      }
      auto samples = 4 * ((x + 1) / 2);
      return std::min(16.0 * samples, 255.0) / 255;
    }) < 0.5 / 255);
  }

  TEST_CASE("fill_rules") {
    auto size = QSize(96, 96);
    for(auto rule : {FillRule::NON_ZERO, FillRule::EVEN_ODD}) {
      auto path = Path(rule);
      for(auto& rectangle : {Rect(8, 8, 48, 48), Rect(36.5, 36, 48, 48)}) {
        path.move_to(rectangle.get_top_left());
        path.line_to(Point(rectangle.get_right(), rectangle.get_top()));
        path.line_to(rectangle.get_bottom_right());
        path.line_to(Point(rectangle.get_left(), rectangle.get_bottom()));
        path.close();
      }
      auto coverage = get_coverage(rasterize(path, size), size);
      REQUIRE(get_maximum_error(coverage, size, [&] (int x, int y) {
        auto first = x >= 8 && x < 56 && y >= 8 && y < 56 ? 1.0 : 0.0;
        auto second = std::max(0.0, std::min(x + 1.0, 84.5) -
          std::max(double(x), 36.5)) * (y >= 36 && y < 84 ? 1.0 : 0.0);
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

  TEST_CASE("supersampling") {
    auto size = QSize(8, 8);
    auto coverage =
      get_coverage(rasterize(make_circle(Point(4, 4), 4), size), size);
    auto values = std::vector<int>();
    for(auto value : coverage) {
      values.push_back(static_cast<int>(std::lround(255 * value)));
    }
    REQUIRE(values == std::vector{
        0,  80, 191, 239, 255, 191,  64,   0,
       80, 255, 255, 255, 255, 255, 255,  64,
      192, 255, 255, 255, 255, 255, 255, 192,
      255, 255, 255, 255, 255, 255, 255, 239,
      255, 255, 255, 255, 255, 255, 255, 240,
      192, 255, 255, 255, 255, 255, 255, 192,
       80, 255, 255, 255, 255, 255, 255,  64,
        0,  80, 192, 240, 255, 192,  64,   0});
  }

  TEST_CASE("analytic_snapping") {
    auto size = QSize(64, 16);
    auto trapezoid = Path();
    trapezoid.move_to(Point(2, 1.4));
    trapezoid.line_to(Point(60, 1.4));
    trapezoid.line_to(Point(62, 12));
    trapezoid.line_to(Point(0, 12));
    trapezoid.close();
    auto coverage = get_coverage(rasterize(trapezoid, size), size);
    REQUIRE(std::abs(coverage[64 + 30] - 0.5) < 0.5 / 255);
    REQUIRE(coverage[2 * 64 + 30] == 1);
    auto rectangle = get_coverage(
      rasterize(make_rectangle(Rect(2, 1.4, 58, 10)), size), size);
    REQUIRE(std::abs(rectangle[64 + 30] - 0.6) < 0.5 / 255);
    auto draw = [&] (const Rect& bounds) {
      auto list = DrawList();
      list.push_back(FillCommand(share(make_rectangle(bounds)), Matrix(),
        GradientPaint(LinearGradient(), {GradientStop(0, make_color(0, 0, 0)),
          GradientStop(1, make_color(255, 255, 255))}, Matrix())));
      auto scene = StripScene();
      auto rasterizer = StripRasterizer();
      rasterizer.rasterize(list, Matrix(), size, scene);
      return get_coverage(scene, size);
    };
    auto painted = draw(Rect(2, 1.4, 58, 10));
    REQUIRE(std::abs(painted[64 + 30] - 0.5) < 0.5 / 255);
    auto small = draw(Rect(2, 1.4, 10, 10));
    REQUIRE(std::abs(small[64 + 6] - 0.6) < 0.5 / 255);
  }

  TEST_CASE("hairline") {
    auto size = QSize(16, 16);
    auto draw = [&] (Point start, Point end, double width) {
      auto line = make_line(start, end);
      auto list = DrawList();
      auto fill = FillCommand(share(outline_stroke(line, StrokeGeometry(width,
        StrokeAlignment::CENTER, StrokeCap::BUTT, StrokeJoin::MITER, 4, {}, 0),
        0.01)), Matrix(), SolidPaint(make_color(0, 0, 0)));
      fill.m_stroke = StrokeLine(share(line), width, StrokeCap::BUTT);
      list.push_back(fill);
      auto scene = StripScene();
      auto rasterizer = StripRasterizer();
      rasterizer.rasterize(list, Matrix(), size, scene);
      return get_coverage(scene, size);
    };
    auto horizontal = draw(Point(1, 2.5), Point(15, 2.5), 1);
    for(auto x = 2; x != 14; ++x) {
      REQUIRE(horizontal[16 + x] == 0);
      REQUIRE(horizontal[2 * 16 + x] == 1);
      REQUIRE(horizontal[3 * 16 + x] == 0);
    }
    auto thin = draw(Point(1, 2.5), Point(15, 2.5), 0.5);
    REQUIRE(std::lround(255 * thin[2 * 16 + 8]) == 127);
    auto diagonal = draw(Point(1, 1.25), Point(13, 7.25), 1);
    for(auto x = 3; x != 11; ++x) {
      auto sum = 0.0;
      for(auto y = 0; y != 16; ++y) {
        sum += diagonal[y * 16 + x];
      }
      REQUIRE(std::abs(sum - 1) < 2.0 / 255);
    }
    auto thick = draw(Point(1, 2.5), Point(15, 2.5), 3);
    REQUIRE(thick[16 + 8] == 1);
    REQUIRE(thick[3 * 16 + 8] == 1);
  }

  TEST_CASE("stroke_outline") {
    auto size = QSize(48, 16);
    auto draw = [&] (const Path& line, double width) {
      auto list = DrawList();
      auto fill = FillCommand(share(outline_stroke(line, StrokeGeometry(width,
        StrokeAlignment::CENTER, StrokeCap::BUTT, StrokeJoin::MITER, 4, {}, 0),
        0.01)), Matrix(), SolidPaint(make_color(0, 0, 0)));
      fill.m_stroke = StrokeLine(share(line), width, StrokeCap::BUTT);
      list.push_back(fill);
      auto scene = StripScene();
      auto rasterizer = StripRasterizer();
      rasterizer.rasterize(list, Matrix(), size, scene);
      return get_coverage(scene, size);
    };
    auto snapped = draw(make_line(Point(1, 4.07), Point(45, 4.07)), 2);
    for(auto x = 2; x != 44; ++x) {
      REQUIRE(snapped[3 * 48 + x] == 1);
      REQUIRE(snapped[4 * 48 + x] == 1);
      REQUIRE(snapped[5 * 48 + x] == 0);
    }
    auto exact = draw(make_line(Point(1, 4.07), Point(15, 4.07)), 2);
    for(auto x = 2; x != 14; ++x) {
      REQUIRE(std::abs(exact[3 * 48 + x] - 0.93) < 1.0 / 255);
      REQUIRE(exact[4 * 48 + x] == 1);
      REQUIRE(std::abs(exact[5 * 48 + x] - 0.07) < 1.0 / 255);
    }
    auto diagonal = draw(make_line(Point(2, 2), Point(4, 4)), 2);
    for(auto value : diagonal) {
      REQUIRE((std::lround(value * 255) + 1) % 16 <= 1);
    }
  }

  TEST_CASE("open_frame") {
    auto size = QSize(16, 16);
    auto draw = [&] (const Path& line) {
      auto list = DrawList();
      auto fill = FillCommand(share(outline_stroke(line, StrokeGeometry(2,
        StrokeAlignment::CENTER, StrokeCap::BUTT, StrokeJoin::MITER, 4, {}, 0),
        0.01)), Matrix(), SolidPaint(make_color(0, 0, 0)));
      fill.m_stroke = StrokeLine(share(line), 2, StrokeCap::BUTT);
      list.push_back(fill);
      auto scene = StripScene();
      auto rasterizer = StripRasterizer();
      rasterizer.rasterize(list, Matrix(), size, scene);
      return get_coverage(scene, size);
    };
    auto open = Path();
    open.move_to(Point(3, 1));
    open.line_to(Point(3, 8.13));
    open.line_to(Point(13, 8.13));
    open.line_to(Point(13, 1));
    auto snapped = draw(open);
    REQUIRE(std::abs(snapped[7 * 16 + 8] - 0.75) < 1.0 / 255);
    REQUIRE(std::abs(snapped[9 * 16 + 8] - 0.25) < 1.0 / 255);
    auto closed = open;
    closed.close();
    auto exact = draw(closed);
    REQUIRE(std::abs(exact[7 * 16 + 8] - 0.87) < 1.0 / 255);
    REQUIRE(std::abs(exact[9 * 16 + 8] - 0.13) < 1.0 / 255);
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

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/ImageComparison.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/PathRenderer.hpp"
#include "Rellu/Rendering/RenderPlan.hpp"

using namespace Rellu;

namespace {
  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  using Coverage = std::function<double (int, int)>;

  struct Layer {
    Coverage m_coverage;
    Color m_color;
  };

  class TestRenderer : public Renderer {
    public:
      DrawList m_list;
      Matrix m_view;
      QColor m_background = QColor(0, 0, 0, 0);

      void initialize(QRhi& rhi, QRhiRenderPassDescriptor& descriptor,
          int sample_count) override {
        m_rhi = &rhi;
        m_paths.initialize(rhi, descriptor, sample_count);
      }

      void release() override {
        m_paths.release();
      }

      void render(QRhiCommandBuffer& buffer, QRhiRenderTarget& target,
          double scale) override {
        auto batch = m_rhi->nextResourceUpdateBatch();
        m_paths.prepare(*batch, m_list, make_scale(scale, scale) * m_view,
          target.pixelSize());
        m_paths.render(buffer, target, m_background, batch);
      }

      const StripScene& get_scene() const {
        return m_paths.get_scene();
      }

    private:
      QRhi* m_rhi = nullptr;
      PathRenderer m_paths;
  };

  auto fill(Path path, Color color, Matrix transform = Matrix()) {
    return DrawCommand(FillCommand(share(std::move(path)), transform,
      SolidPaint(color)));
  }

  auto get_overlap(double low, double high, int pixel) {
    return std::max(0.0, std::min(high, pixel + 1.0) - std::max(low, 1.0 *
      pixel));
  }

  Coverage make_rectangle_coverage(const Rect& rect) {
    return [=] (int x, int y) {
      return get_overlap(rect.get_left(), rect.get_right(), x) *
        get_overlap(rect.get_top(), rect.get_bottom(), y);
    };
  }

  Coverage make_sampled_coverage(std::function<bool (Point)> inside,
      const Rect& bounds, int samples = 32) {
    auto area = inflate(bounds, 1);
    return [=] (int x, int y) {
      if(!area.intersects(Rect(x, y, 1, 1))) {
        return 0.0;
      }
      auto count = 0;
      for(auto i = 0; i != samples; ++i) {
        for(auto j = 0; j != samples; ++j) {
          if(inside(Point(x + (j + 0.5) / samples, y + (i + 0.5) / samples))) {
            ++count;
          }
        }
      }
      return double(count) / (samples * samples);
    };
  }

  Coverage make_path_coverage(Path path, Matrix transform) {
    auto inverse = *invert(transform);
    auto bounds = get_bounds(path, transform);
    return make_sampled_coverage([=] (Point point) {
      return contains(path, inverse.map(point));
    }, bounds);
  }

  Coverage make_supersampled_coverage(Path path, Matrix transform) {
    auto inverse = *invert(transform);
    auto bounds = get_bounds(path, transform);
    return make_sampled_coverage([=] (Point point) {
      return contains(path, inverse.map(point));
    }, bounds, 4);
  }

  Coverage make_analytic_coverage(const Path& path, const Matrix& transform) {
    auto snapped = Path(path.get_fill_rule());
    auto snap = [&] (Point point) {
      auto mapped = transform.map(point);
      return Point(mapped.m_x, std::floor(4 * mapped.m_y + 0.5) / 4);
    };
    for(auto& segment : path.get_segments()) {
      if(segment.m_type == SegmentType::MOVE) {
        snapped.move_to(snap(segment.m_points[0]));
      } else if(segment.m_type == SegmentType::LINE) {
        snapped.line_to(snap(segment.m_points[0]));
      } else if(segment.m_type == SegmentType::CLOSE) {
        snapped.close();
      }
    }
    return make_path_coverage(snapped, Matrix());
  }

  auto make_image(QSize size, QColor background,
      const std::vector<Layer>& layers) {
    auto image = QImage(size, QImage::Format_RGBA8888_Premultiplied);
    for(auto y = 0; y != size.height(); ++y) {
      for(auto x = 0; x != size.width(); ++x) {
        auto alpha = background.alphaF();
        auto pixel = std::array<double, 4>({background.redF() * alpha,
          background.greenF() * alpha, background.blueF() * alpha, alpha});
        for(auto& layer : layers) {
          auto coverage = layer.m_coverage(x, y) * layer.m_color.m_alpha;
          auto source = std::array<double, 4>({layer.m_color.m_red,
            layer.m_color.m_green, layer.m_color.m_blue, 1});
          for(auto channel = 0; channel != 4; ++channel) {
            pixel[channel] =
              source[channel] * coverage + pixel[channel] * (1 - coverage);
          }
        }
        auto line = image.scanLine(y) + 4 * x;
        for(auto channel = 0; channel != 4; ++channel) {
          line[channel] = static_cast<uchar>(std::lround(255 * pixel[channel]));
        }
      }
    }
    return image;
  }

  auto render(TestRenderer& renderer, QSize size, double scale = 1) {
    auto offscreen = OffscreenRenderer(renderer);
    return offscreen.render(size, scale);
  }

  void require_match(const QImage& actual, const QImage& expected,
      ImageTolerance tolerance) {
    auto comparison = compare_images(actual, expected, tolerance);
    CAPTURE(comparison.m_mismatches);
    CAPTURE(comparison.m_maximum_difference);
    CAPTURE(comparison.m_mean_difference);
    REQUIRE(comparison.m_is_match);
  }
  using Pixel = std::array<double, 4>;

  Pixel get_pixel(const QImage& image, int x, int y) {
    auto line = image.constScanLine(y) + 4 * x;
    return Pixel({line[0] / 255.0, line[1] / 255.0, line[2] / 255.0,
      line[3] / 255.0});
  }

  Pixel premultiply(const Color& color) {
    return Pixel({color.m_red * color.m_alpha, color.m_green * color.m_alpha,
      color.m_blue * color.m_alpha, color.m_alpha});
  }

  void require_pixel(const QImage& image, int x, int y, const Pixel& expected,
      double tolerance) {
    auto actual = get_pixel(image, x, y);
    CAPTURE(x);
    CAPTURE(y);
    for(auto channel = 0; channel != 4; ++channel) {
      CAPTURE(channel);
      CAPTURE(actual[channel] * 255);
      CAPTURE(expected[channel] * 255);
      REQUIRE(std::abs(actual[channel] - expected[channel]) <=
        tolerance / 255);
    }
  }

  double get_luminosity(const std::array<double, 3>& c) {
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
  }

  std::array<double, 3> set_luminosity(std::array<double, 3> c, double l) {
    auto d = l - get_luminosity(c);
    for(auto& value : c) {
      value += d;
    }
    auto luminosity = get_luminosity(c);
    auto low = std::min({c[0], c[1], c[2]});
    auto high = std::max({c[0], c[1], c[2]});
    for(auto& value : c) {
      if(low < 0) {
        value = luminosity +
          (value - luminosity) * luminosity / (luminosity - low);
      }
      if(high > 1) {
        value = luminosity +
          (value - luminosity) * (1 - luminosity) / (high - luminosity);
      }
    }
    return c;
  }

  std::array<double, 3> set_saturation(std::array<double, 3> c, double s) {
    auto low = std::min({c[0], c[1], c[2]});
    auto high = std::max({c[0], c[1], c[2]});
    for(auto& value : c) {
      value = high > low ? (value - low) * s / (high - low) : 0;
    }
    return c;
  }

  double get_saturation(const std::array<double, 3>& c) {
    return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
  }

  double blend_channel(BlendMode mode, double b, double s) {
    auto hard_light = [] (double b, double s) {
      if(s <= 0.5) {
        return b * 2 * s;
      }
      return b + (2 * s - 1) - b * (2 * s - 1);
    };
    if(mode == BlendMode::MULTIPLY) {
      return b * s;
    } else if(mode == BlendMode::SCREEN) {
      return b + s - b * s;
    } else if(mode == BlendMode::OVERLAY) {
      return hard_light(s, b);
    } else if(mode == BlendMode::DARKEN) {
      return std::min(b, s);
    } else if(mode == BlendMode::LIGHTEN) {
      return std::max(b, s);
    } else if(mode == BlendMode::COLOR_DODGE) {
      if(b == 0) {
        return 0;
      } else if(s == 1) {
        return 1;
      }
      return std::min(1.0, b / (1 - s));
    } else if(mode == BlendMode::COLOR_BURN) {
      if(b == 1) {
        return 1;
      } else if(s == 0) {
        return 0;
      }
      return 1 - std::min(1.0, (1 - b) / s);
    } else if(mode == BlendMode::HARD_LIGHT) {
      return hard_light(b, s);
    } else if(mode == BlendMode::SOFT_LIGHT) {
      if(s <= 0.5) {
        return b - (1 - 2 * s) * b * (1 - b);
      }
      auto d = b <= 0.25 ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
      return b + (2 * s - 1) * (d - b);
    } else if(mode == BlendMode::DIFFERENCE) {
      return std::abs(b - s);
    } else if(mode == BlendMode::EXCLUSION) {
      return b + s - 2 * b * s;
    }
    return s;
  }

  std::array<double, 3> blend(BlendMode mode, const std::array<double, 3>& b,
      const std::array<double, 3>& s) {
    if(mode == BlendMode::HUE) {
      return set_luminosity(
        set_saturation(s, get_saturation(b)), get_luminosity(b));
    } else if(mode == BlendMode::SATURATION) {
      return set_luminosity(
        set_saturation(b, get_saturation(s)), get_luminosity(b));
    } else if(mode == BlendMode::COLOR) {
      return set_luminosity(s, get_luminosity(b));
    } else if(mode == BlendMode::LUMINOSITY) {
      return set_luminosity(b, get_luminosity(s));
    }
    return std::array<double, 3>({blend_channel(mode, b[0], s[0]),
      blend_channel(mode, b[1], s[1]), blend_channel(mode, b[2], s[2])});
  }

  Pixel composite(BlendMode mode, const Color& backdrop, const Color& source) {
    auto b = std::array<double, 3>(
      {backdrop.m_red, backdrop.m_green, backdrop.m_blue});
    auto s = std::array<double, 3>(
      {source.m_red, source.m_green, source.m_blue});
    auto mixed = blend(mode, b, s);
    auto ab = backdrop.m_alpha;
    auto as = source.m_alpha;
    auto result = Pixel();
    for(auto channel = 0; channel != 3; ++channel) {
      result[channel] = as * (1 - ab) * s[channel] +
        ab * (1 - as) * b[channel] + as * ab * mixed[channel];
    }
    result[3] = as + ab - as * ab;
    return result;
  }

  double get_box_coverage(int pixel, int edge, double radius) {
    auto size = std::max(1, static_cast<int>(std::floor(
      radius * 3 * std::sqrt(2 * std::numbers::pi) / 4 + 0.5)));
    auto half = size / 2;
    auto windows = std::array<std::pair<int, int>, 3>();
    if(size % 2 == 1) {
      windows.fill(std::pair(-half, half));
    } else {
      windows = std::array<std::pair<int, int>, 3>({std::pair(-half, half - 1),
        std::pair(-half + 1, half), std::pair(-half, half)});
    }
    auto weights = std::vector<double>({1});
    auto first = 0;
    for(auto& window : windows) {
      auto width = window.second - window.first + 1;
      auto next = std::vector<double>(weights.size() + width - 1, 0);
      for(auto i = std::size_t(0); i != weights.size(); ++i) {
        for(auto j = 0; j != width; ++j) {
          next[i + j] += weights[i] / width;
        }
      }
      weights = std::move(next);
      first += window.first;
    }
    auto coverage = 0.0;
    for(auto i = std::size_t(0); i != weights.size(); ++i) {
      if(pixel + first + static_cast<int>(i) >= edge) {
        coverage += weights[i];
      }
    }
    return coverage;
  }

  auto begin_layer(double opacity = 1, BlendMode mode = BlendMode::NORMAL) {
    auto layer = BeginLayerCommand();
    layer.m_opacity = opacity;
    layer.m_blend_mode = mode;
    return layer;
  }
}

TEST_SUITE("PathRenderer") {
  TEST_CASE("empty") {
    auto renderer = TestRenderer();
    renderer.m_background = QColor(10, 20, 30);
    auto image = render(renderer, QSize(16, 8));
    require_match(image, make_image(QSize(16, 8), QColor(10, 20, 30), {}),
      ImageTolerance(0, 0));
  }

  TEST_CASE("crisp_edges") {
    auto renderer = TestRenderer();
    renderer.m_background = QColor(255, 255, 255);
    auto color = make_color(40, 90, 200);
    renderer.m_list.push_back(fill(make_rectangle(Rect(3, 5, 17, 9)), color));
    renderer.m_list.push_back(fill(make_rectangle(Rect(22, 1, 1, 20)), color));
    auto offscreen = OffscreenRenderer(renderer);
    auto expected = make_image(QSize(32, 24), QColor(255, 255, 255),
      {Layer(make_rectangle_coverage(Rect(3, 5, 17, 9)), color),
        Layer(make_rectangle_coverage(Rect(22, 1, 1, 20)), color)});
    require_match(
      offscreen.render(QSize(32, 24), 1), expected, ImageTolerance(0, 0));
    auto scaled = make_image(QSize(64, 48), QColor(255, 255, 255),
      {Layer(make_rectangle_coverage(Rect(6, 10, 34, 18)), color),
        Layer(make_rectangle_coverage(Rect(44, 2, 2, 40)), color)});
    require_match(
      offscreen.render(QSize(64, 48), 2), scaled, ImageTolerance(0, 0));
  }

  TEST_CASE("fractional_rectangles") {
    auto renderer = TestRenderer();
    auto rectangles = std::vector<Rect>({Rect(1.25, 2.5, 6.5, 3.75),
      Rect(10.1, 0.3, 0.4, 12.6), Rect(13.6, 7.9, 9.8, 0.35),
      Rect(-3.5, 14.5, 9, 30), Rect(20.7, 15.2, 30, 3.3)});
    auto layers = std::vector<Layer>();
    auto color = make_color(200, 30, 60);
    for(auto& rectangle : rectangles) {
      renderer.m_list.push_back(fill(make_rectangle(rectangle), color));
      layers.push_back(Layer(make_rectangle_coverage(rectangle), color));
    }
    auto expected = make_image(QSize(24, 20), QColor(0, 0, 0, 0), layers);
    require_match(
      render(renderer, QSize(24, 20)), expected, ImageTolerance(1, 0));
  }

  TEST_CASE("hairlines") {
    auto renderer = TestRenderer();
    renderer.m_background = QColor(255, 255, 255);
    auto color = make_color(0, 0, 0);
    auto layers = std::vector<Layer>();
    for(auto& rectangle : {Rect(2, 2.5, 20, 1), Rect(2, 6, 20, 1),
        Rect(2, 9.25, 20, 0.5), Rect(2.5, 12, 1, 10), Rect(6.3, 12, 0.1, 10)}) {
      renderer.m_list.push_back(fill(make_rectangle(rectangle), color));
      layers.push_back(Layer(make_rectangle_coverage(rectangle), color));
    }
    auto line = make_rotation(std::numbers::pi / 7, Point(16, 17));
    auto diagonal = Rect(9, 16.5, 14, 1);
    renderer.m_list.push_back(fill(make_rectangle(diagonal), color, line));
    layers.push_back(
      Layer(make_analytic_coverage(make_rectangle(diagonal), line), color));
    auto expected = make_image(QSize(24, 24), QColor(255, 255, 255), layers);
    require_match(
      render(renderer, QSize(24, 24)), expected, ImageTolerance(4, 0));
  }

  TEST_CASE("circle") {
    auto renderer = TestRenderer();
    auto color = make_color(20, 120, 40);
    auto circle = make_circle(Point(15.3, 14.6), 11.7);
    renderer.m_list.push_back(fill(circle, color));
    auto ring = make_circle(Point(40, 14), 10);
    for(auto& segment : make_circle(Point(40, 14), 9).get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        ring.move_to(points[0]);
      } else if(segment.m_type == SegmentType::CUBIC) {
        ring.cubic_to(points[0], points[1], points[2]);
      }
    }
    ring.set_fill_rule(FillRule::EVEN_ODD);
    renderer.m_list.push_back(fill(ring, color));
    auto expected = make_image(QSize(56, 30), QColor(0, 0, 0, 0),
      {Layer(make_supersampled_coverage(circle, Matrix()), color),
        Layer(make_supersampled_coverage(ring, Matrix()), color)});
    auto image = render(renderer, QSize(56, 30));
    auto comparison = compare_images(image, expected, ImageTolerance(17, 0.02));
    CAPTURE(comparison.m_maximum_difference);
    CAPTURE(comparison.m_mean_difference);
    REQUIRE(comparison.m_is_match);
    REQUIRE(comparison.m_mean_difference < 1);
  }

  TEST_CASE("fill_rules") {
    auto make_shapes = [] (FillRule rule) {
      auto path = Path(rule);
      for(auto& rectangle : {Rect(2, 2, 12, 12), Rect(8, 8, 12, 12)}) {
        path.move_to(rectangle.get_top_left());
        path.line_to(Point(rectangle.get_right(), rectangle.get_top()));
        path.line_to(rectangle.get_bottom_right());
        path.line_to(Point(rectangle.get_left(), rectangle.get_bottom()));
        path.close();
      }
      path.move_to(Point(24, 2));
      path.line_to(Point(24, 20));
      path.line_to(Point(38, 20));
      path.line_to(Point(38, 2));
      path.close();
      path.move_to(Point(27, 5));
      path.line_to(Point(35, 5));
      path.line_to(Point(35, 17));
      path.line_to(Point(27, 17));
      path.close();
      path.move_to(Point(42, 3));
      path.line_to(Point(58, 19.5));
      path.line_to(Point(58, 3));
      path.line_to(Point(42, 19.5));
      path.close();
      path.move_to(Point(30, 21.5));
      for(auto i = 1; i != 5; ++i) {
        auto angle = 4 * std::numbers::pi * i / 5 - std::numbers::pi / 2;
        path.line_to(Point(30 + 9.5 * std::cos(angle),
          31 + 9.5 * std::sin(angle)));
      }
      path.close();
      return path;
    };
    auto color = make_color(90, 30, 160, 0.75);
    auto renderer = TestRenderer();
    auto offscreen = OffscreenRenderer(renderer);
    for(auto rule : {FillRule::NON_ZERO, FillRule::EVEN_ODD}) {
      CAPTURE(int(rule));
      auto shapes = make_shapes(rule);
      renderer.m_list.clear();
      renderer.m_list.push_back(fill(shapes, color));
      auto expected = make_image(QSize(60, 42), QColor(0, 0, 0, 0),
        {Layer(make_supersampled_coverage(shapes, Matrix()), color)});
      auto image = offscreen.render(QSize(60, 42), 1);
      require_match(image, expected, ImageTolerance(13, 0.005));
      auto inside = qAlpha(image.pixel(11, 11));
      auto hole = qAlpha(image.pixel(31, 11));
      auto star = qAlpha(image.pixel(30, 31));
      REQUIRE(hole == 0);
      if(rule == FillRule::NON_ZERO) {
        REQUIRE(inside == 191);
        REQUIRE(star == 191);
      } else {
        REQUIRE(inside == 0);
        REQUIRE(star == 0);
      }
    }
  }

  TEST_CASE("transforms") {
    auto renderer = TestRenderer();
    renderer.m_background = QColor(255, 255, 255);
    auto rotation = make_rotation(0.4, Point(12, 12));
    auto rectangle = make_rectangle(Rect(5, 7, 14, 10));
    auto skew = Matrix(1, 0.3, -0.5, 0.8, 30, 4);
    auto ellipse = make_ellipse(Point(8, 10), 7, 4);
    auto view = make_translation(1.5, -0.75);
    renderer.m_view = view;
    renderer.m_list.push_back(
      fill(rectangle, make_color(230, 120, 0), rotation));
    renderer.m_list.push_back(fill(ellipse, make_color(0, 60, 230), skew));
    auto offscreen = OffscreenRenderer(renderer);
    auto expected = [&] (double scale) {
      auto pixels = make_scale(scale, scale) * view;
      return make_image(
        QSize(int(48 * scale), int(26 * scale)), QColor(255, 255, 255), {
          Layer(make_analytic_coverage(rectangle, pixels * rotation),
            make_color(230, 120, 0)),
          Layer(make_supersampled_coverage(ellipse, pixels * skew),
            make_color(0, 60, 230))});
    };
    require_match(offscreen.render(QSize(48, 26), 1), expected(1),
      ImageTolerance(17, 0.02));
    require_match(offscreen.render(QSize(96, 52), 2), expected(2),
      ImageTolerance(17, 0.02));
  }

  TEST_CASE("blending") {
    auto renderer = TestRenderer();
    renderer.m_background = QColor(240, 230, 220);
    auto layers = std::vector<Layer>();
    auto add = [&] (const Rect& rectangle, Color color) {
      renderer.m_list.push_back(fill(make_rectangle(rectangle), color));
      layers.push_back(Layer(make_rectangle_coverage(rectangle), color));
    };
    add(Rect(2, 2, 14, 10), make_color(255, 0, 0, 0.5));
    add(Rect(8, 5, 14, 10), make_color(0, 0, 255, 0.4));
    add(Rect(5.5, 8.5, 12, 9), make_color(20, 200, 40, 0.8));
    add(Rect(12, 1, 6, 16), make_color(255, 255, 255, 0.1));
    add(Rect(1, 14, 22, 3), make_color(0, 0, 0, 0));
    auto offscreen = OffscreenRenderer(renderer);
    auto expected = make_image(QSize(24, 20), QColor(240, 230, 220), layers);
    require_match(
      offscreen.render(QSize(24, 20), 1), expected, ImageTolerance(2, 0));
    renderer.m_background = QColor(0, 0, 0, 0);
    auto transparent = make_image(QSize(24, 20), QColor(0, 0, 0, 0), layers);
    require_match(
      offscreen.render(QSize(24, 20), 1), transparent, ImageTolerance(2, 0));
  }

  TEST_CASE("clipping") {
    auto renderer = TestRenderer();
    auto color = make_color(255, 128, 0);
    auto layers = std::vector<Layer>();
    auto rotation = make_rotation(0.3, Point(0, 10));
    auto triangle = Path();
    triangle.move_to(Point(-30, -5));
    triangle.line_to(Point(10, 9.5));
    triangle.line_to(Point(-30, 25));
    triangle.close();
    renderer.m_list.push_back(fill(triangle, color));
    layers.push_back(
      Layer(make_analytic_coverage(triangle, Matrix()), color));
    auto large = make_rectangle(Rect(-100, 14.5, 300, 300));
    renderer.m_list.push_back(fill(large, color, rotation));
    layers.push_back(Layer(make_analytic_coverage(large, rotation), color));
    auto expected = make_image(QSize(30, 22), QColor(0, 0, 0, 0), layers);
    require_match(
      render(renderer, QSize(30, 22)), expected, ImageTolerance(6, 0));
  }

  TEST_CASE("atlas_growth") {
    auto renderer = TestRenderer();
    auto color = make_color(0, 0, 0, 0.5);
    auto get_cell = [] (int x, int y) {
      return Rect(4 * x + 0.5, 4 * y + 0.25, 2.5, 3.25);
    };
    for(auto y = 0; y != 80; ++y) {
      for(auto x = 0; x != 80; ++x) {
        renderer.m_list.push_back(
          fill(make_rectangle(get_cell(x, y)), color));
      }
    }
    auto image = render(renderer, QSize(640, 640), 2);
    REQUIRE(renderer.get_scene().get_atlas_height() > 16);
    auto coverage = [&] (int x, int y) {
      auto cell = get_cell(x / 8, y / 8);
      return make_rectangle_coverage(Rect(2 * cell.m_x, 2 * cell.m_y,
        2 * cell.m_width, 2 * cell.m_height))(x, y);
    };
    auto expected = make_image(
      QSize(640, 640), QColor(0, 0, 0, 0), {Layer(coverage, color)});
    require_match(image, expected, ImageTolerance(1, 0));
  }

  TEST_CASE("blend_modes") {
    auto pairs = std::vector<std::array<Color, 2>>({
      {make_color(204, 77, 128), make_color(51, 153, 230, 0.75)},
      {make_color(26, 230, 102), make_color(179, 51, 153)},
      {make_color(240, 200, 30), make_color(20, 40, 220, 0.5)}});
    auto renderer = TestRenderer();
    auto offscreen = OffscreenRenderer(renderer);
    for(auto mode : {BlendMode::MULTIPLY, BlendMode::SCREEN,
        BlendMode::OVERLAY, BlendMode::DARKEN, BlendMode::LIGHTEN,
        BlendMode::COLOR_DODGE, BlendMode::COLOR_BURN, BlendMode::HARD_LIGHT,
        BlendMode::SOFT_LIGHT, BlendMode::DIFFERENCE, BlendMode::EXCLUSION,
        BlendMode::HUE, BlendMode::SATURATION, BlendMode::COLOR,
        BlendMode::LUMINOSITY}) {
      for(auto& pair : pairs) {
        CAPTURE(static_cast<int>(mode));
        auto backdrop = pair[0];
        auto translucent = backdrop;
        translucent.m_alpha = 0.5;
        renderer.m_list.clear();
        renderer.m_list.push_back(
          fill(make_rectangle(Rect(0, 0, 4, 4)), backdrop));
        renderer.m_list.push_back(
          fill(make_rectangle(Rect(4, 0, 4, 4)), translucent));
        renderer.m_list.push_back(begin_layer(1, mode));
        renderer.m_list.push_back(
          fill(make_rectangle(Rect(0, 0, 12, 4)), pair[1]));
        renderer.m_list.push_back(EndLayerCommand());
        auto image = offscreen.render(QSize(12, 4), 1);
        require_pixel(image, 1, 1, composite(mode, backdrop, pair[1]), 3);
        require_pixel(image, 5, 1, composite(mode, translucent, pair[1]), 3);
        require_pixel(image, 9, 1, premultiply(pair[1]), 1);
      }
    }
  }

  TEST_CASE("layer_opacity") {
    auto renderer = TestRenderer();
    auto red = make_color(255, 0, 0);
    auto blue = make_color(0, 0, 255);
    renderer.m_list.push_back(begin_layer(0.5));
    renderer.m_list.push_back(fill(make_rectangle(Rect(0, 0, 8, 4)), red));
    renderer.m_list.push_back(fill(make_rectangle(Rect(4, 0, 8, 4)), blue));
    renderer.m_list.push_back(EndLayerCommand());
    renderer.m_list.push_back(begin_layer(0.5));
    renderer.m_list.push_back(begin_layer(0.5));
    renderer.m_list.push_back(fill(make_rectangle(Rect(0, 4, 12, 4)), red));
    renderer.m_list.push_back(EndLayerCommand());
    renderer.m_list.push_back(EndLayerCommand());
    auto offscreen = OffscreenRenderer(renderer);
    auto image = offscreen.render(QSize(12, 8), 1);
    require_pixel(image, 1, 1, Pixel({0.5, 0, 0, 0.5}), 1);
    require_pixel(image, 6, 1, Pixel({0, 0, 0.5, 0.5}), 1);
    require_pixel(image, 10, 1, Pixel({0, 0, 0.5, 0.5}), 1);
    require_pixel(image, 6, 5, Pixel({0.25, 0, 0, 0.25}), 1);
    auto scaled = offscreen.render(QSize(24, 16), 2);
    require_pixel(scaled, 12, 2, Pixel({0, 0, 0.5, 0.5}), 1);
  }

  TEST_CASE("mask") {
    auto renderer = TestRenderer();
    auto color = make_color(20, 160, 60);
    auto circle = make_circle(Point(16.3, 15.6), 10.4);
    auto layer = begin_layer(0.8);
    layer.m_mask = LayerMask(share(circle), Matrix());
    renderer.m_list.push_back(layer);
    renderer.m_list.push_back(
      fill(make_rectangle(Rect(0, 0, 40, 32)), color));
    renderer.m_list.push_back(EndLayerCommand());
    auto translucent = color;
    translucent.m_alpha = 0.8;
    auto expected = make_image(QSize(32, 32), QColor(0, 0, 0, 0),
      {Layer(make_supersampled_coverage(circle, Matrix()), translucent)});
    require_match(
      render(renderer, QSize(32, 32)), expected, ImageTolerance(52, 0));
  }

  TEST_CASE("linear_gradient") {
    auto renderer = TestRenderer();
    auto stops = std::vector<GradientStop>({
      GradientStop(0, make_color(255, 0, 0)),
      GradientStop(0.5, make_color(0, 0, 255, 0.5)),
      GradientStop(1, make_color(0, 255, 0))});
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 0, 64, 8))),
      Matrix(), GradientPaint(
        LinearGradient(Point(8, 0), Point(56, 0)), stops, Matrix())));
    auto transform = make_translation(32, 8) *
      make_rotation(std::numbers::pi / 2) * make_scale(16, 16);
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 8, 64, 24))),
      Matrix(), GradientPaint(
        LinearGradient(Point(0, 0), Point(1, 0)), stops, transform)));
    auto image = render(renderer, QSize(64, 32));
    for(auto x = 0; x != 64; ++x) {
      auto offset = (x + 0.5 - 8) / 48;
      require_pixel(
        image, x, 4, premultiply(evaluate_gradient(stops, offset)), 3);
    }
    for(auto y = 8; y != 32; ++y) {
      auto offset = (y + 0.5 - 8) / 16;
      require_pixel(
        image, 20, y, premultiply(evaluate_gradient(stops, offset)), 3);
    }
  }

  TEST_CASE("mixed_paints") {
    auto renderer = TestRenderer();
    auto stops = std::vector<GradientStop>({
      GradientStop(0, make_color(0, 0, 255)),
      GradientStop(1, make_color(0, 0, 255))});
    for(auto pass = 0; pass != 3; ++pass) {
      for(auto i = 0; i != 300; ++i) {
        renderer.m_list.push_back(fill(make_rectangle(
          Rect(i % 30, 10 * pass + i / 30, 1, 1)), make_color(255, 0, 0)));
      }
      renderer.m_list.push_back(FillCommand(
        share(make_rectangle(Rect(32, 10 * pass, 4, 4))), Matrix(),
        GradientPaint(
          LinearGradient(Point(0, 0), Point(1, 0)), stops, Matrix())));
    }
    auto image = render(renderer, QSize(40, 30));
    for(auto pass = 0; pass != 3; ++pass) {
      require_pixel(image, 33, 10 * pass + 1, Pixel({0, 0, 1, 1}), 1);
      require_pixel(image, 5, 10 * pass + 5, Pixel({1, 0, 0, 1}), 0);
    }
  }

  TEST_CASE("radial_gradient") {
    auto renderer = TestRenderer();
    auto stops = std::vector<GradientStop>({
      GradientStop(0, make_color(255, 255, 255)),
      GradientStop(1, make_color(0, 0, 0))});
    auto gradient = RadialGradient(Point(32, 32), 24, Point(42, 30));
    auto transform = make_translation(0, 64) * make_scale(1, 0.5);
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 0, 64, 64))),
      Matrix(), GradientPaint(gradient, stops, Matrix())));
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 64, 64, 32))),
      Matrix(), GradientPaint(gradient, stops, transform)));
    auto image = render(renderer, QSize(64, 96));
    auto inverse = *invert(transform);
    for(auto y = 0; y < 96; y += 3) {
      for(auto x = 0; x < 64; x += 3) {
        auto point = Point(x + 0.5, y + 0.5);
        if(y >= 64) {
          point = inverse.map(point);
        }
        auto offset =
          std::clamp(get_radial_offset(gradient, point), 0.0, 1.0);
        require_pixel(
          image, x, y, premultiply(evaluate_gradient(stops, offset)), 3);
      }
    }
  }

  TEST_CASE("angular_gradient") {
    auto renderer = TestRenderer();
    auto stops = std::vector<GradientStop>({
      GradientStop(0, make_color(255, 255, 255)),
      GradientStop(1, make_color(0, 0, 0))});
    auto gradient = AngularGradient(Point(32, 32), 30);
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 0, 64, 64))),
      Matrix(), GradientPaint(gradient, stops, Matrix())));
    auto image = render(renderer, QSize(64, 64));
    for(auto y = 0; y < 64; y += 3) {
      for(auto x = 0; x < 64; x += 3) {
        auto point = Point(x + 0.5, y + 0.5);
        auto offset = get_angular_offset(gradient, point);
        if(std::abs(point.m_x - 32) < 3 && std::abs(point.m_y - 32) < 3 ||
            offset < 0.03 || offset > 0.97) {
          continue;
        }
        require_pixel(
          image, x, y, premultiply(evaluate_gradient(stops, offset)), 3);
      }
    }
  }

  TEST_CASE("image_fill") {
    auto colors = std::array<QColor, 8>({QColor(255, 0, 0), QColor(0, 255, 0),
      QColor(0, 0, 255), QColor(255, 255, 0), QColor(0, 255, 255),
      QColor(255, 0, 255), QColor(128, 64, 32), QColor(255, 255, 255, 128)});
    auto source = QImage(4, 2, QImage::Format_RGBA8888);
    for(auto i = 0; i != 8; ++i) {
      source.setPixelColor(i % 4, i / 4, colors[i]);
    }
    auto image = std::make_shared<const QImage>(source);
    auto renderer = TestRenderer();
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 0, 36, 18))),
      Matrix(), ImagePaint(image, make_scale(9, 9))));
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 18, 36, 18))),
      Matrix(), ImagePaint(image,
        make_translation(36, 18) * make_scale(-9, 9), 0.5)));
    auto result = render(renderer, QSize(36, 36));
    auto to_pixel = [] (const QColor& color, double opacity) {
      return premultiply(Color(color.redF(), color.greenF(), color.blueF(),
        color.alphaF() * opacity));
    };
    for(auto i = 0; i != 8; ++i) {
      auto x = 9 * (i % 4) + 4;
      auto y = 9 * (i / 4) + 4;
      require_pixel(result, x, y, to_pixel(colors[i], 1), 1);
      require_pixel(result, 35 - x, 18 + y, to_pixel(colors[i], 0.5), 1);
    }
    require_pixel(result, 9, 4, Pixel({0, 1, 0, 1}), 1);
    require_pixel(result, 8, 4, Pixel({1, 0, 0, 1}), 1);
  }

  TEST_CASE("image_magnification") {
    auto source = QImage(2, 1, QImage::Format_RGBA8888);
    source.setPixelColor(0, 0, QColor(255, 0, 0));
    source.setPixelColor(1, 0, QColor(0, 255, 0));
    auto renderer = TestRenderer();
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 0, 4, 2))), Matrix(),
      ImagePaint(std::make_shared<const QImage>(source), make_scale(2, 2))));
    auto image = render(renderer, QSize(4, 2));
    require_pixel(image, 1, 0, Pixel({0.75, 0.25, 0, 1}), 2);
    require_pixel(image, 2, 0, Pixel({0.25, 0.75, 0, 1}), 2);
  }

  TEST_CASE("image_minification") {
    auto source = QImage(64, 64, QImage::Format_RGBA8888);
    for(auto y = 0; y != 64; ++y) {
      for(auto x = 0; x != 64; ++x) {
        source.setPixelColor(
          x, y, (x + y) % 2 ? QColor(255, 255, 255) : QColor(0, 0, 0));
      }
    }
    auto renderer = TestRenderer();
    renderer.m_list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 0, 10, 10))),
      Matrix(), ImagePaint(std::make_shared<const QImage>(source),
        make_scale(10.0 / 64, 10.0 / 64))));
    auto image = render(renderer, QSize(10, 10));
    for(auto y = 0; y != 10; ++y) {
      for(auto x = 0; x != 10; ++x) {
        require_pixel(image, x, y, Pixel({0.5, 0.5, 0.5, 1}), 2);
      }
    }
  }

  TEST_CASE("drop_shadow") {
    auto renderer = TestRenderer();
    auto offscreen = OffscreenRenderer(renderer);
    for(auto [radius, scale] : {std::pair(3.0, 1.0), std::pair(3.0, 2.0),
        std::pair(12.0, 1.0)}) {
      CAPTURE(radius);
      CAPTURE(scale);
      renderer.m_list.clear();
      auto layer = begin_layer();
      layer.m_drop_shadows.push_back(
        Shadow(Point(-4, 2), radius, make_color(0, 0, 255, 0.5)));
      renderer.m_list.push_back(layer);
      renderer.m_list.push_back(fill(
        make_rectangle(Rect(64, -100, 200, 300)), make_color(255, 0, 0)));
      renderer.m_list.push_back(EndLayerCommand());
      auto size = static_cast<int>(64 * scale);
      auto image = offscreen.render(QSize(size + size / 2, 4), scale);
      for(auto x = 0; x != size; ++x) {
        auto alpha = 0.5 * get_box_coverage(
          x, static_cast<int>(60 * scale), radius * scale);
        require_pixel(image, x, 1, Pixel({0, 0, alpha, alpha}), 3);
      }
    }
  }

  TEST_CASE("inner_shadow") {
    auto renderer = TestRenderer();
    auto layer = begin_layer();
    layer.m_inner_shadows.push_back(
      Shadow(Point(0, 4), 0, make_color(0, 0, 0)));
    layer.m_inner_shadows.push_back(
      Shadow(Point(0, -40), 3, make_color(0, 0, 0)));
    renderer.m_list.push_back(layer);
    renderer.m_list.push_back(
      fill(make_rectangle(Rect(8, 8, 16, 60)), make_color(255, 255, 255)));
    renderer.m_list.push_back(EndLayerCommand());
    auto image = render(renderer, QSize(32, 76));
    for(auto y = 0; y != 76; ++y) {
      auto expected = Pixel({0, 0, 0, 0});
      if(y >= 8 && y < 12) {
        expected = Pixel({0, 0, 0, 1});
      } else if(y >= 12 && y < 68) {
        auto shadow = get_box_coverage(y, 28, 3);
        expected = Pixel({1 - shadow, 1 - shadow, 1 - shadow, 1});
      }
      require_pixel(image, 16, y, expected, 3);
      require_pixel(image, 4, y, Pixel({0, 0, 0, 0}), 0);
    }
  }

  TEST_CASE("inner_shadow_edge") {
    auto renderer = TestRenderer();
    auto layer = begin_layer();
    layer.m_inner_shadows.push_back(
      Shadow(Point(0, 0), 0, make_color(0, 0, 0)));
    renderer.m_list.push_back(layer);
    renderer.m_list.push_back(
      fill(make_rectangle(Rect(8.5, 0, 16, 16)), make_color(255, 255, 255)));
    renderer.m_list.push_back(EndLayerCommand());
    auto image = render(renderer, QSize(32, 16));
    require_pixel(image, 8, 8, Pixel({0.375, 0.375, 0.375, 0.625}), 3);
    require_pixel(image, 16, 8, Pixel({1, 1, 1, 1}), 1);
  }

  TEST_CASE("blur") {
    auto renderer = TestRenderer();
    auto layer = begin_layer();
    layer.m_blur = 5;
    renderer.m_list.push_back(layer);
    renderer.m_list.push_back(
      fill(make_rectangle(Rect(32, -50, 100, 100)), make_color(0, 255, 0)));
    renderer.m_list.push_back(EndLayerCommand());
    auto image = render(renderer, QSize(64, 4));
    for(auto x = 0; x != 64; ++x) {
      auto alpha = get_box_coverage(x, 32, 5);
      require_pixel(image, x, 1, Pixel({0, alpha, 0, alpha}), 3);
    }
  }

  TEST_CASE("blur_kernel") {
    auto samples = std::array<std::pair<double, std::vector<double>>, 2>({
      std::pair(1.0, std::vector<double>({0.084, 0.335, 0.665, 0.916})),
      std::pair(2.0, std::vector<double>({0.01, 0.052, 0.126, 0.251, 0.414,
        0.586, 0.749, 0.874, 0.948, 0.99}))});
    for(auto& sample : samples) {
      CAPTURE(sample.first);
      auto renderer = TestRenderer();
      auto layer = begin_layer();
      layer.m_blur = sample.first;
      renderer.m_list.push_back(layer);
      renderer.m_list.push_back(
        fill(make_rectangle(Rect(32, -50, 100, 100)), make_color(0, 255, 0)));
      renderer.m_list.push_back(EndLayerCommand());
      auto image = render(renderer, QSize(64, 4));
      auto start = 32 - static_cast<int>(sample.second.size()) / 2;
      for(auto i = std::size_t(0); i != sample.second.size(); ++i) {
        auto alpha = sample.second[i];
        require_pixel(image, start + static_cast<int>(i), 1,
          Pixel({0, alpha, 0, alpha}), 2);
      }
    }
  }

  TEST_CASE("backdrop_blur") {
    auto renderer = TestRenderer();
    for(auto x = 0; x < 64; x += 2) {
      renderer.m_list.push_back(fill(
        make_rectangle(Rect(x, 0, 1, 32)), make_color(255, 255, 255)));
      renderer.m_list.push_back(
        fill(make_rectangle(Rect(x + 1, 0, 1, 32)), make_color(0, 0, 0)));
    }
    renderer.m_list.push_back(BackdropBlurCommand(
      share(make_rectangle(Rect(16, 8, 32, 16))), Matrix(), 4, 0.2,
      std::nullopt));
    auto image = render(renderer, QSize(64, 32));
    for(auto x = 28; x != 36; ++x) {
      require_pixel(image, x, 16, Pixel({0.6, 0.6, 0.6, 1}), 3);
    }
    require_pixel(image, 10, 16, Pixel({1, 1, 1, 1}), 0);
    require_pixel(image, 11, 4, Pixel({0, 0, 0, 1}), 0);
  }

  TEST_CASE("backdrop_blur_resolution") {
    auto renderer = TestRenderer();
    renderer.m_list.push_back(
      fill(make_rectangle(Rect(0, 0, 20, 10)), make_color(0, 0, 0)));
    renderer.m_list.push_back(fill(make_rectangle(Rect(31.0 / 3, 0, 1.0 / 3,
      10)), make_color(255, 255, 255)));
    renderer.m_list.push_back(BackdropBlurCommand(
      share(make_rectangle(Rect(0, 0, 20, 10))), Matrix(), 0.2, 0,
      std::nullopt));
    auto image = render(renderer, QSize(60, 30), 3);
    require_pixel(image, 28, 15, Pixel({0, 0, 0, 1}), 1);
    require_pixel(image, 30, 15, Pixel({2.0 / 3, 2.0 / 3, 2.0 / 3, 1}), 2);
    require_pixel(image, 31, 15, Pixel({1, 1, 1, 1}), 1);
    require_pixel(image, 33, 15, Pixel({1.0 / 3, 1.0 / 3, 1.0 / 3, 1}), 2);
    require_pixel(image, 35, 15, Pixel({0, 0, 0, 1}), 1);
  }

  TEST_CASE("backdrop_blur_edges") {
    for(auto radius : {3.0, 40.0}) {
      auto renderer = TestRenderer();
      renderer.m_list.push_back(fill(
        make_rectangle(Rect(0, 0, 128, 32)), make_color(255, 255, 255)));
      renderer.m_list.push_back(fill(
        make_rectangle(Rect(32, 0, 64, 32)), make_color(0, 0, 0)));
      renderer.m_list.push_back(BackdropBlurCommand(
        share(make_rectangle(Rect(32, 8, 64, 16))), Matrix(), radius, 0,
        std::nullopt));
      auto image = render(renderer, QSize(128, 32));
      CAPTURE(radius);
      for(auto x = 32; x < 96; x += 7) {
        require_pixel(image, x, 16, Pixel({0, 0, 0, 1}), 1);
      }
      require_pixel(image, 95, 16, Pixel({0, 0, 0, 1}), 1);
      require_pixel(image, 31, 16, Pixel({1, 1, 1, 1}), 0);
    }
  }
}

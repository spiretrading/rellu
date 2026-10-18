#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/ImageComparison.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/PathRenderer.hpp"

using namespace Rellu;

namespace {
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
        buffer.beginPass(
          &target, m_background, QRhiDepthStencilClearValue(1, 0), batch);
        m_paths.draw(buffer);
        buffer.endPass();
      }

      const StripScene& get_scene() const {
        return m_paths.get_scene();
      }

    private:
      QRhi* m_rhi = nullptr;
      PathRenderer m_paths;
  };

  auto fill(Path path, Color color, Matrix transform = Matrix()) {
    return DrawCommand(FillCommand(std::move(path), transform,
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
      int samples = 32) {
    return [=] (int x, int y) {
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
    return make_sampled_coverage([=] (Point point) {
      return contains(path, inverse.map(point));
    });
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
    auto expected = make_image(QSize(32, 24), QColor(255, 255, 255),
      {Layer(make_rectangle_coverage(Rect(3, 5, 17, 9)), color),
        Layer(make_rectangle_coverage(Rect(22, 1, 1, 20)), color)});
    require_match(
      render(renderer, QSize(32, 24)), expected, ImageTolerance(0, 0));
    auto scaled = make_image(QSize(64, 48), QColor(255, 255, 255),
      {Layer(make_rectangle_coverage(Rect(6, 10, 34, 18)), color),
        Layer(make_rectangle_coverage(Rect(44, 2, 2, 40)), color)});
    require_match(
      render(renderer, QSize(64, 48), 2), scaled, ImageTolerance(0, 0));
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
      Layer(make_path_coverage(make_rectangle(diagonal), line), color));
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
      {Layer(make_path_coverage(circle, Matrix()), color),
        Layer(make_path_coverage(ring, Matrix()), color)});
    auto image = render(renderer, QSize(56, 30));
    auto comparison = compare_images(image, expected, ImageTolerance(6, 0));
    CAPTURE(comparison.m_maximum_difference);
    CAPTURE(comparison.m_mean_difference);
    REQUIRE(comparison.m_is_match);
    REQUIRE(comparison.m_mean_difference < 0.25);
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
    for(auto rule : {FillRule::NON_ZERO, FillRule::EVEN_ODD}) {
      CAPTURE(int(rule));
      auto renderer = TestRenderer();
      auto shapes = make_shapes(rule);
      renderer.m_list.push_back(fill(shapes, color));
      auto expected = make_image(QSize(60, 42), QColor(0, 0, 0, 0),
        {Layer(make_path_coverage(shapes, Matrix()), color)});
      auto image = render(renderer, QSize(60, 42));
      require_match(image, expected, ImageTolerance(6, 0.005));
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
    auto expected = [&] (double scale) {
      auto pixels = make_scale(scale, scale) * view;
      return make_image(
        QSize(int(48 * scale), int(26 * scale)), QColor(255, 255, 255), {
          Layer(make_path_coverage(rectangle, pixels * rotation),
            make_color(230, 120, 0)),
          Layer(make_path_coverage(ellipse, pixels * skew),
            make_color(0, 60, 230))});
    };
    require_match(render(renderer, QSize(48, 26)), expected(1),
      ImageTolerance(6, 0));
    require_match(render(renderer, QSize(96, 52), 2), expected(2),
      ImageTolerance(6, 0));
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
    auto expected = make_image(QSize(24, 20), QColor(240, 230, 220), layers);
    require_match(
      render(renderer, QSize(24, 20)), expected, ImageTolerance(2, 0));
    renderer.m_background = QColor(0, 0, 0, 0);
    auto transparent = make_image(QSize(24, 20), QColor(0, 0, 0, 0), layers);
    require_match(
      render(renderer, QSize(24, 20)), transparent, ImageTolerance(2, 0));
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
    layers.push_back(Layer(make_path_coverage(triangle, Matrix()), color));
    auto large = make_rectangle(Rect(-100, 14.5, 300, 300));
    renderer.m_list.push_back(fill(large, color, rotation));
    layers.push_back(Layer(make_path_coverage(large, rotation), color));
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
}

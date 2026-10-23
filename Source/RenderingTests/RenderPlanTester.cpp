#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <variant>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/RenderPlan.hpp"

using namespace Rellu;

namespace {
  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  auto plan(const DrawList& list, QSize size) {
    auto scene = StripScene();
    auto rasterizer = StripRasterizer();
    rasterizer.rasterize(list, Matrix(), size, scene);
    auto result = RenderPlan();
    make_render_plan(list, scene, Matrix(), size, PlanOptions(), result);
    return result;
  }

  auto count_passes(const RenderPlan& plan) {
    auto count = 0;
    for(auto& step : plan.m_steps) {
      count += std::holds_alternative<PlanPass>(step);
    }
    return count;
  }

  bool is_near(const Color& left, const Color& right) {
    return std::abs(left.m_red - right.m_red) < 1e-9 &&
      std::abs(left.m_green - right.m_green) < 1e-9 &&
      std::abs(left.m_blue - right.m_blue) < 1e-9 &&
      std::abs(left.m_alpha - right.m_alpha) < 1e-9;
  }
}

TEST_SUITE("RenderPlan") {
  TEST_CASE("single_pass") {
    auto list = DrawList();
    for(auto i = 0; i != 3; ++i) {
      list.push_back(FillCommand(
        share(make_rectangle(Rect(i, i, 4, 4))), Matrix(),
        SolidPaint(make_color(255, 0, 0))));
    }
    auto result = plan(list, QSize(16, 16));
    REQUIRE(result.m_steps.size() == 1);
    auto& pass = std::get<PlanPass>(result.m_steps[0]);
    REQUIRE(pass.m_target == -1);
    REQUIRE(pass.m_clear == PlanClear::BACKGROUND);
    REQUIRE(pass.m_draws.size() == 1);
    REQUIRE(pass.m_draws[0].m_pipeline == PlanPipeline::SOLID);
    REQUIRE(result.m_textures.empty());
    REQUIRE(result.m_strips.empty());
  }

  TEST_CASE("solid_runs") {
    auto list = DrawList();
    auto add_solids = [&] (int count) {
      for(auto i = 0; i != count; ++i) {
        list.push_back(FillCommand(
          share(make_rectangle(Rect(i % 64, i / 64, 1, 1))), Matrix(),
          SolidPaint(make_color(255, 0, 0))));
      }
    };
    add_solids(300);
    list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 10, 4, 1))), Matrix(),
      GradientPaint(LinearGradient(), {GradientStop()}, Matrix())));
    add_solids(300);
    auto result = plan(list, QSize(64, 64));
    auto& draws = std::get<PlanPass>(result.m_steps[0]).m_draws;
    REQUIRE(draws.size() == 3);
    REQUIRE(draws[0].m_pipeline == PlanPipeline::SOLID);
    REQUIRE(draws[0].m_count == 300);
    REQUIRE(draws[1].m_pipeline == PlanPipeline::OVER);
    REQUIRE(draws[1].m_first == 300);
    REQUIRE(draws[1].m_count == 1);
    REQUIRE(draws[2].m_pipeline == PlanPipeline::SOLID);
    REQUIRE(draws[2].m_count == 300);
  }

  TEST_CASE("layers") {
    auto list = DrawList();
    list.push_back(FillCommand(
      share(make_rectangle(Rect(0, 0, 16, 16))), Matrix(),
      SolidPaint(make_color(255, 0, 0))));
    auto layer = BeginLayerCommand();
    layer.m_blend_mode = BlendMode::SCREEN;
    list.push_back(layer);
    list.push_back(FillCommand(
      share(make_rectangle(Rect(2, 2, 4, 4))), Matrix(),
      SolidPaint(make_color(0, 255, 0))));
    list.push_back(EndLayerCommand());
    auto result = plan(list, QSize(16, 16));
    REQUIRE(count_passes(result) == 4);
    REQUIRE(std::holds_alternative<PlanCopy>(result.m_steps[2]));
    auto& blend = std::get<PlanPass>(result.m_steps[3]);
    REQUIRE(blend.m_clear == PlanClear::KEEP);
    REQUIRE(blend.m_draws.back().m_pipeline == PlanPipeline::BLEND);
    auto& last = std::get<PlanPass>(result.m_steps.back());
    REQUIRE(last.m_target == -1);
    REQUIRE(result.m_textures.size() == 3);
    auto empty = DrawList();
    empty.push_back(BeginLayerCommand());
    empty.push_back(EndLayerCommand());
    REQUIRE(plan(empty, QSize(16, 16)).m_steps.size() == 1);
  }

  TEST_CASE("projective_gradient") {
    auto projection = Projection(1.2, 0.1, -0.2, 0.9, 3, 4, 0.01, 0.02, 1);
    auto paint = GradientPaint(LinearGradient(Point(0, 0), Point(10, 0)),
      {GradientStop(0, make_color(255, 0, 0)),
        GradientStop(1, make_color(0, 0, 255))}, make_scale(2, 1));
    paint.m_projection = projection;
    auto list = DrawList();
    list.push_back(FillCommand(share(make_rectangle(Rect(0, 0, 32, 32))),
      Matrix(), paint));
    auto result = plan(list, QSize(32, 32));
    REQUIRE(result.m_paints.size() == PAINT_TEXELS);
    auto& first = result.m_paints[0];
    auto& divisor = result.m_paints[4];
    auto inverse = *invert(projection * make_scale(2, 1));
    for(auto point : {Point(0.5, 0.5), Point(20.5, 7.5), Point(9.5, 30.5)}) {
      auto offset = (first[0] * point.m_x + first[1] * point.m_y + first[2]) /
        (divisor[0] * point.m_x + divisor[1] * point.m_y + divisor[2] + 1);
      REQUIRE(std::abs(offset - inverse.map(point).m_x / 10) < 1e-5);
    }
    auto affine = plan(DrawList({FillCommand(share(make_rectangle(
      Rect(0, 0, 4, 4))), Matrix(), GradientPaint(LinearGradient(), {
        GradientStop()}, Matrix()))}), QSize(8, 8));
    REQUIRE(affine.m_paints[4] == std::array<float, 4>({0, 0, 0, 0}));
  }

  TEST_CASE("gradient_evaluation") {
    auto stops = std::vector<GradientStop>({
      GradientStop(0.25, make_color(255, 0, 0)),
      GradientStop(0.5, make_color(0, 0, 255, 0.5)),
      GradientStop(0.5, make_color(0, 255, 0)),
      GradientStop(0.25, make_color(255, 255, 255))});
    REQUIRE(is_near(evaluate_gradient(stops, 0), make_color(255, 0, 0)));
    REQUIRE(is_near(evaluate_gradient(stops, 0.375),
      Color(0.5, 0, 0.5, 0.75)));
    REQUIRE(is_near(evaluate_gradient(stops, 0.5), make_color(0, 0, 255, 0.5)));
    REQUIRE(is_near(evaluate_gradient(stops, 0.75),
      make_color(255, 255, 255)));
    REQUIRE(is_near(evaluate_gradient({}, 0.5), Color(0, 0, 0, 0)));
  }

  TEST_CASE("radial_offset") {
    auto gradient = RadialGradient(Point(10, 10), 5, Point(12, 10));
    REQUIRE(std::abs(get_radial_offset(gradient, Point(12, 10))) < 1e-9);
    REQUIRE(std::abs(get_radial_offset(gradient, Point(15, 10)) - 1) < 1e-9);
    REQUIRE(std::abs(get_radial_offset(gradient, Point(5, 10)) - 1) < 1e-9);
    REQUIRE(std::abs(get_radial_offset(gradient, Point(13.5, 10)) - 0.5) <
      1e-9);
    REQUIRE(std::abs(get_radial_offset(gradient, Point(8.5, 10)) - 0.5) <
      1e-9);
    auto outside = RadialGradient(Point(0, 0), 1, Point(3, 0));
    REQUIRE(std::abs(get_radial_offset(outside, Point(0.999, 0))) < 1e-6);
  }

  TEST_CASE("angular_offset") {
    auto gradient = AngularGradient(Point(0.5, 0.5), 0);
    REQUIRE(get_angular_offset(gradient, Point(1, 0.5)) == doctest::Approx(0));
    REQUIRE(get_angular_offset(gradient, Point(0.5, 1)) ==
      doctest::Approx(0.25));
    REQUIRE(get_angular_offset(gradient, Point(0, 0.5)) ==
      doctest::Approx(0.5));
    REQUIRE(get_angular_offset(gradient, Point(0.5, 0)) ==
      doctest::Approx(0.75));
    auto rotated = AngularGradient(Point(0, 0), 90);
    REQUIRE(get_angular_offset(rotated, Point(0, 1)) == doctest::Approx(0));
    REQUIRE(get_angular_offset(rotated, Point(1, 0)) ==
      doctest::Approx(0.75));
  }

  TEST_CASE("blur_downsampling") {
    REQUIRE(get_blur_downsampling(0) == 1);
    REQUIRE(get_blur_downsampling(2.9) == 1);
    REQUIRE(get_blur_downsampling(3) == 2);
    REQUIRE(get_blur_downsampling(12) == 4);
    REQUIRE(get_blur_downsampling(30) == 8);
    REQUIRE(get_blur_downsampling(1000) == 32);
  }
}

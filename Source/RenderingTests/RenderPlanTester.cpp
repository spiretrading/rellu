#include <cmath>
#include <variant>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/RenderPlan.hpp"

using namespace Rellu;

namespace {
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
      list.push_back(FillCommand(make_rectangle(Rect(i, i, 4, 4)), Matrix(),
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
          make_rectangle(Rect(i % 64, i / 64, 1, 1)), Matrix(),
          SolidPaint(make_color(255, 0, 0))));
      }
    };
    add_solids(300);
    list.push_back(FillCommand(make_rectangle(Rect(0, 10, 4, 1)), Matrix(),
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
    list.push_back(FillCommand(make_rectangle(Rect(0, 0, 16, 16)), Matrix(),
      SolidPaint(make_color(255, 0, 0))));
    auto layer = BeginLayerCommand();
    layer.m_blend_mode = BlendMode::SCREEN;
    list.push_back(layer);
    list.push_back(FillCommand(make_rectangle(Rect(2, 2, 4, 4)), Matrix(),
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

  TEST_CASE("blur_downsampling") {
    REQUIRE(get_blur_downsampling(0) == 1);
    REQUIRE(get_blur_downsampling(3.9) == 1);
    REQUIRE(get_blur_downsampling(4) == 2);
    REQUIRE(get_blur_downsampling(16) == 8);
    REQUIRE(get_blur_downsampling(1000) == 32);
  }
}

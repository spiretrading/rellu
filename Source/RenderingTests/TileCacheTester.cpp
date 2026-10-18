#include <chrono>
#include <memory>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"

using namespace Rellu;

namespace {
  const auto SIZE = QSize(200, 160);

  auto share(Path path) {
    return std::make_shared<const Path>(std::move(path));
  }

  DrawCommand make_fill(Path path, Color color) {
    return FillCommand(share(std::move(path)), Matrix(), SolidPaint(color));
  }

  std::shared_ptr<const DrawList> make_background() {
    auto list = std::make_shared<DrawList>();
    list->push_back(FillCommand(share(make_rectangle(Rect(0, 0, 200, 150))),
      Matrix(), GradientPaint(LinearGradient(Point(0, 0), Point(200, 150)),
        {GradientStop(0, make_color(255, 255, 255)),
          GradientStop(1, make_color(40, 120, 200))}, Matrix())));
    auto shadow = BeginLayerCommand();
    shadow.m_drop_shadows.push_back(
      Shadow(Point(5, 7), 4, make_color(0, 0, 0, 0.4)));
    list->push_back(shadow);
    list->push_back(make_fill(make_rectangle(Rect(30, 30, 40, 30),
      CornerRadii(6, 6, 6, 6)), make_color(250, 200, 0)));
    list->push_back(EndLayerCommand());
    auto blur = BeginLayerCommand();
    blur.m_blur = 3;
    list->push_back(blur);
    list->push_back(
      make_fill(make_ellipse(Point(115, 55), 15, 15), make_color(200, 0, 0)));
    list->push_back(EndLayerCommand());
    auto inner = BeginLayerCommand();
    inner.m_inner_shadows.push_back(
      Shadow(Point(2, 2), 3, make_color(0, 0, 0, 0.8)));
    list->push_back(inner);
    list->push_back(make_fill(
      make_rectangle(Rect(40, 90, 50, 30)), make_color(0, 160, 80)));
    list->push_back(EndLayerCommand());
    return list;
  }

  std::shared_ptr<const DrawList> make_foreground(Point offset) {
    auto list = std::make_shared<DrawList>();
    auto multiply = BeginLayerCommand();
    multiply.m_opacity = 0.8;
    multiply.m_blend_mode = BlendMode::MULTIPLY;
    list->push_back(multiply);
    list->push_back(make_fill(make_rectangle(Rect(60 + offset.m_x,
      50 + offset.m_y, 60, 40)), make_color(255, 100, 255)));
    list->push_back(EndLayerCommand());
    list->push_back(BackdropBlurCommand(
      share(make_rectangle(Rect(120, 80, 50, 40))), Matrix(), 5, 0.1,
      std::nullopt));
    list->push_back(make_fill(make_rectangle(Rect(120, 80, 50, 40)),
      make_color(255, 255, 255, 0.3)));
    auto masked = BeginLayerCommand();
    masked.m_mask = LayerMask(share(make_circle(Point(160, 30), 15)),
      Matrix());
    list->push_back(masked);
    list->push_back(make_fill(make_rectangle(Rect(140, 10, 50, 50)),
      make_color(0, 0, 200)));
    list->push_back(EndLayerCommand());
    auto image = QImage(8, 8, QImage::Format_RGBA8888_Premultiplied);
    for(auto y = 0; y != 8; ++y) {
      for(auto x = 0; x != 8; ++x) {
        image.setPixelColor(x, y, QColor(32 * x, 32 * y, 128, 255));
      }
    }
    list->push_back(FillCommand(share(make_rectangle(Rect(10, 110, 30, 30))),
      Matrix(), ImagePaint(std::make_shared<const QImage>(image),
        make_translation(10, 110) * make_scale(3.75, 3.75))));
    return list;
  }

  struct Harness {
    CanvasRenderer m_renderer;
    OffscreenRenderer m_offscreen;

    Harness(std::shared_ptr<const Drawing> drawing, int tile_size,
        const Matrix& view)
        : m_offscreen(m_renderer) {
      m_renderer.set_drawing(std::move(drawing));
      m_renderer.get_tiles().set_tile_size(tile_size);
      m_renderer.set_view(view);
    }

    QImage render() {
      auto image = m_offscreen.render(SIZE, 1);
      for(auto i = 0; i != 1000 && !m_renderer.is_complete(); ++i) {
        image = m_offscreen.render(SIZE, 1);
      }
      return image;
    }
  };

  std::shared_ptr<Drawing> make_drawing() {
    auto drawing = std::make_shared<Drawing>();
    drawing->set_lists({make_background(), make_foreground(Point(0, 0))});
    return drawing;
  }
}

TEST_SUITE("TileCache") {
  TEST_CASE("tiling") {
    auto drawing = make_drawing();
    for(auto& view : {make_translation(3.4, 7.6) * make_scale(1.5, 1.5),
        make_translation(-20, -10) * make_scale(2, 2),
        make_translation(-91.3, -45.8) * make_scale(3.3, 3.3),
        make_translation(17.5, 9.25) * make_scale(0.75, 0.75)}) {
      auto tiled = Harness(drawing, 16, view);
      auto single = Harness(drawing, 32, view);
      single.m_renderer.get_tiles().set_batch_size(1);
      auto whole = Harness(drawing, 4096, view);
      auto image = whole.render();
      REQUIRE(whole.m_renderer.is_complete());
      REQUIRE(tiled.render() == image);
      REQUIRE(single.render() == image);
    }
  }

  TEST_CASE("pan") {
    auto drawing = make_drawing();
    auto view = make_translation(4, 3);
    auto tiled = Harness(drawing, 16, view);
    auto whole = Harness(drawing, 4096, view);
    REQUIRE(tiled.render() == whole.render());
    for(auto i = 0; i != 4; ++i) {
      tiled.m_offscreen.render(SIZE, 1);
    }
    auto panned = make_translation(15, -3);
    tiled.m_renderer.set_view(panned);
    whole.m_renderer.set_view(panned);
    tiled.m_renderer.get_tiles().set_budget(std::chrono::nanoseconds(0));
    auto image = tiled.m_offscreen.render(SIZE, 1);
    REQUIRE(image == whole.render());
    REQUIRE(tiled.render() == whole.render());
  }

  TEST_CASE("invalidation") {
    auto drawing = make_drawing();
    auto view = make_translation(2, 1) * make_scale(1.25, 1.25);
    auto tiled = Harness(drawing, 16, view);
    auto whole = Harness(drawing, 4096, view);
    REQUIRE(tiled.render() == whole.render());
    auto damage = drawing->set_lists(
      {drawing->get_lists()[0], make_foreground(Point(-20, 30))});
    REQUIRE(!damage.empty());
    tiled.m_renderer.invalidate(damage);
    whole.m_renderer.invalidate(damage);
    auto image = tiled.render();
    REQUIRE(image == whole.render());
    REQUIRE(image == Harness(drawing, 4096, view).render());
  }

  TEST_CASE("zoom") {
    auto drawing = make_drawing();
    auto tiled = Harness(drawing, 16, make_scale(1, 1));
    tiled.render();
    auto view = make_translation(-30, -20) * make_scale(2, 2);
    tiled.m_renderer.set_view(view);
    tiled.m_renderer.get_tiles().set_budget(std::chrono::nanoseconds(0));
    tiled.m_renderer.get_tiles().set_batch_size(1);
    tiled.m_offscreen.render(SIZE, 1);
    REQUIRE(!tiled.m_renderer.is_complete());
    REQUIRE(tiled.render() == Harness(drawing, 4096, view).render());
  }
}

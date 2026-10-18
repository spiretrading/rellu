#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <QGuiApplication>
#include <QtPlugin>
#include <rhi/qrhi_platform.h>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/Scene.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"
#include "Rellu/Tools/ToolManager.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

namespace {
  constexpr auto HIT_COUNT = 5000;
  constexpr auto MARQUEE_COUNT = 200;
  constexpr auto DRAG_FRAMES = 60;
  constexpr auto MAXIMUM_FRAMES = 2000;
  constexpr auto SCENE_BUDGET = std::chrono::milliseconds(8);
  const auto SIZE = QSize(1920, 1080);

  using Clock = std::chrono::steady_clock;

  double to_milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  struct Statistics {
    double m_median;
    double m_maximum;
  };

  Statistics get_statistics(std::vector<double> values) {
    if(values.empty()) {
      return Statistics(0, 0);
    }
    std::sort(values.begin(), values.end());
    return Statistics(values[values.size() / 2], values.back());
  }

  class Harness {
    public:
      explicit Harness(Renderer& renderer)
          : m_renderer(&renderer) {
        auto parameters = QRhiD3D11InitParams();
        m_rhi.reset(QRhi::create(QRhi::D3D11, &parameters));
        if(!m_rhi) {
          throw std::runtime_error("Unable to create the QRhi.");
        }
        m_texture.reset(m_rhi->newTexture(
          QRhiTexture::RGBA8, SIZE, 1, QRhiTexture::RenderTarget));
        m_texture->create();
        m_depth_stencil.reset(m_rhi->newRenderBuffer(
          QRhiRenderBuffer::DepthStencil, SIZE, 1));
        m_depth_stencil->create();
        m_target.reset(m_rhi->newTextureRenderTarget(
          QRhiTextureRenderTargetDescription(
            QRhiColorAttachment(m_texture.get()), m_depth_stencil.get())));
        m_descriptor.reset(m_target->newCompatibleRenderPassDescriptor());
        m_target->setRenderPassDescriptor(m_descriptor.get());
        m_target->create();
        m_renderer->initialize(*m_rhi, *m_descriptor, 1);
      }

      ~Harness() {
        m_renderer->release();
      }

      double render() {
        auto start = Clock::now();
        auto buffer = static_cast<QRhiCommandBuffer*>(nullptr);
        m_rhi->beginOffscreenFrame(&buffer);
        m_renderer->render(*buffer, *m_target, 1);
        m_rhi->endOffscreenFrame();
        return to_milliseconds(Clock::now() - start);
      }

    private:
      Renderer* m_renderer;
      std::unique_ptr<QRhi> m_rhi;
      std::unique_ptr<QRhiTexture> m_texture;
      std::unique_ptr<QRhiRenderBuffer> m_depth_stencil;
      std::unique_ptr<QRhiRenderPassDescriptor> m_descriptor;
      std::unique_ptr<QRhiTextureRenderTarget> m_target;
  };

  std::size_t count_nodes(const Document& document, const NodeId& id) {
    auto count = std::size_t(1);
    for(auto& child : document.get(id).m_children) {
      count += count_nodes(document, child);
    }
    return count;
  }

  std::optional<NodeId> find_shape(
      const Document& document, const NodeId& id, int depth) {
    auto node = document.find(id);
    if(!node || depth > 64 || !node->m_header.m_is_visible ||
        node->m_header.m_is_locked) {
      return std::nullopt;
    }
    if(auto shape = node->get_shape()) {
      if(std::holds_alternative<SolidFill>(shape->m_style.m_fill)) {
        return id;
      }
    }
    auto& children = node->m_children;
    for(auto i = children.rbegin(); i != children.rend(); ++i) {
      if(auto shape = find_shape(document, *i, depth + 1)) {
        return shape;
      }
    }
    return std::nullopt;
  }

  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void drag(const char* name, Document& document, Scene& scene,
      CanvasRenderer& renderer, Harness& harness, ToolManager& tools,
      Point start, Qt::KeyboardModifiers modifiers) {
    tools.press(
      make_event(start, Qt::LeftButton, Qt::LeftButton, modifiers));
    auto& selection = tools.get_context().get_editor().get_selection();
    auto selected = selection.is_empty() ? NodeId() : selection.get_nodes()[0];
    auto size = std::size_t(0);
    if(!selected.is_nil()) {
      size = count_nodes(document, selected);
    }
    auto tool_times = std::vector<double>();
    auto scene_times = std::vector<double>();
    auto render_times = std::vector<double>();
    auto totals = std::vector<double>();
    auto view = renderer.get_view();
    auto region = Rect(-view.m_tx, -view.m_ty, SIZE.width(), SIZE.height());
    for(auto i = 1; i <= DRAG_FRAMES; ++i) {
      auto frame_start = Clock::now();
      tools.move(make_event(start + Point(3 * i, 2 * i), Qt::NoButton,
        Qt::LeftButton, Qt::NoModifier));
      auto moved = Clock::now();
      renderer.invalidate(scene.update(region, SCENE_BUDGET));
      auto updated = Clock::now();
      renderer.set_overlay(tools.draw());
      auto render = harness.render();
      auto rendered = Clock::now();
      tool_times.push_back(to_milliseconds(moved - frame_start));
      scene_times.push_back(to_milliseconds(updated - moved));
      render_times.push_back(render);
      totals.push_back(to_milliseconds(rendered - frame_start));
    }
    tools.release(make_event(start + Point(3 * DRAG_FRAMES, 2 * DRAG_FRAMES),
      Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    auto tool = get_statistics(tool_times);
    auto update = get_statistics(scene_times);
    auto render = get_statistics(render_times);
    auto total = get_statistics(totals);
    std::printf("  drag %s (%zu nodes moved, %zu undo steps): frame median "
      "%.2f ms, max %.2f ms; tools %.2f (max %.2f), scene %.2f (max %.2f), "
      "render %.2f (max %.2f)\n", name, size,
      tools.get_context().get_editor().get_history().get_undo_count(),
      total.m_median, total.m_maximum, tool.m_median, tool.m_maximum,
      update.m_median, update.m_maximum, render.m_median, render.m_maximum);
  }

  void run(const std::filesystem::path& path) {
    auto document = Document();
    auto start = Clock::now();
    import_xd(path, document);
    std::printf("%s: %zu nodes, %zu top-level nodes, import %.1f ms\n",
      path.filename().string().c_str(), document.get_nodes().get_size(),
      document.get_canvas().size(), to_milliseconds(Clock::now() - start));
    {
      auto prepared = HitTester(document);
      auto calls = 1;
      start = Clock::now();
      while(!prepared.prepare(std::chrono::milliseconds(8))) {
        ++calls;
      }
      std::printf("  prepare: %.1f ms in %d slices of 8 ms\n",
        to_milliseconds(Clock::now() - start), calls);
    }
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& tester = tools.get_hit_tester();
    auto bounds = Rect(0, 0, 0, 0);
    for(auto& id : document.get_canvas()) {
      if(auto extent = get_world_bounds(document, id)) {
        bounds = unite(bounds, *extent);
      }
    }
    auto random = std::mt19937(11);
    auto x = std::uniform_real_distribution<double>(
      bounds.get_left(), bounds.get_right());
    auto y = std::uniform_real_distribution<double>(
      bounds.get_top(), bounds.get_bottom());
    auto times = std::vector<double>();
    auto hits = 0;
    start = Clock::now();
    for(auto i = 0; i != HIT_COUNT; ++i) {
      auto hit_start = Clock::now();
      if(!tester.hit(Point(x(random), y(random)), 3).is_nil()) {
        ++hits;
      }
      times.push_back(to_milliseconds(Clock::now() - hit_start));
    }
    auto cold = get_statistics(times);
    std::printf("  cold hits: %d of %d hit, total %.1f ms, median %.4f ms, "
      "max %.2f ms\n", hits, HIT_COUNT, to_milliseconds(Clock::now() - start),
      cold.m_median, cold.m_maximum);
    times.clear();
    hits = 0;
    for(auto i = 0; i != HIT_COUNT; ++i) {
      auto hit_start = Clock::now();
      if(!tester.hit(Point(x(random), y(random)), 3).is_nil()) {
        ++hits;
      }
      times.push_back(to_milliseconds(Clock::now() - hit_start));
    }
    auto warm = get_statistics(times);
    std::printf("  warm hits: %d of %d hit, median %.4f ms, max %.3f ms\n",
      hits, HIT_COUNT, warm.m_median, warm.m_maximum);
    times.clear();
    auto selected = std::size_t(0);
    auto& canvas = document.get_canvas();
    for(auto i = 0; i != MARQUEE_COUNT; ++i) {
      auto& id = canvas[i % canvas.size()];
      auto artboard = document.get(id).get_artboard();
      auto query_start = Clock::now();
      if(artboard) {
        auto& frame = artboard->m_frame;
        selected += tester.query(id, Rect(frame.m_x, frame.m_y,
          frame.m_width / 2, frame.m_height / 2)).size();
      } else {
        selected += tester.query(NodeId(), Rect(x(random), y(random),
          bounds.m_width / 10, bounds.m_height / 10)).size();
      }
      times.push_back(to_milliseconds(Clock::now() - query_start));
    }
    auto marquee = get_statistics(times);
    std::printf("  marquee queries: %zu nodes selected, median %.4f ms, max "
      "%.3f ms\n", selected, marquee.m_median, marquee.m_maximum);
    auto largest = NodeId();
    auto largest_size = std::size_t(0);
    for(auto& id : canvas) {
      if(!document.get(id).get_artboard()) {
        continue;
      }
      auto size = count_nodes(document, id);
      if(size > largest_size) {
        largest = id;
        largest_size = size;
      }
    }
    auto shape = std::optional<NodeId>();
    if(!largest.is_nil()) {
      shape = find_shape(document, largest, 0);
    }
    if(!shape) {
      return;
    }
    auto shape_bounds = *get_world_bounds(document, *shape);
    auto center = shape_bounds.get_center();
    auto view = make_translation(SIZE.width() / 2.0 - center.m_x,
      SIZE.height() / 2.0 - center.m_y);
    auto scene = Scene(document, StripRasterizer::TOLERANCE);
    start = Clock::now();
    scene.update();
    std::printf("  largest artboard %zu nodes; scene %.1f ms\n", largest_size,
      to_milliseconds(Clock::now() - start));
    {
      auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE);
      builder.build(largest);
      start = Clock::now();
      auto list = std::make_shared<const DrawList>(builder.build(largest));
      auto built = Clock::now();
      auto drawing = Drawing();
      drawing.set_lists({list});
      std::printf("  rebuild of the largest artboard %.2f ms (%zu commands), "
        "indexing %.2f ms\n", to_milliseconds(built - start), list->size(),
        to_milliseconds(Clock::now() - built));
      auto sizes = std::vector<std::size_t>();
      for(auto& child : document.get(largest).m_children) {
        sizes.push_back(count_nodes(document, child));
      }
      std::sort(sizes.rbegin(), sizes.rend());
      std::printf("  %zu children, largest:", sizes.size());
      for(auto i = std::size_t(0); i != std::min<std::size_t>(5, sizes.size());
          ++i) {
        std::printf(" %zu", sizes[i]);
      }
      std::printf("\n");
    }
    auto renderer = CanvasRenderer();
    renderer.set_drawing(scene.get_drawing());
    renderer.set_view(view);
    auto harness = Harness(renderer);
    for(auto i = 0; i != MAXIMUM_FRAMES && !renderer.is_complete(); ++i) {
      harness.render();
    }
    tools.set_view(view, 1, Size(SIZE.width(), SIZE.height()));
    auto idle = std::vector<double>();
    for(auto i = 0; i != 10; ++i) {
      renderer.set_overlay(tools.draw());
      idle.push_back(harness.render());
    }
    std::printf("  idle frame with overlay %.2f ms\n",
      get_statistics(idle).m_median);
    auto hover_times = std::vector<double>();
    for(auto i = 0; i != 200; ++i) {
      auto hover_start = Clock::now();
      tools.move(make_event(Point(SIZE.width() * (i % 20) / 20.0,
        SIZE.height() * (i / 20) / 10.0), Qt::NoButton, Qt::NoButton,
        Qt::NoModifier));
      hover_times.push_back(to_milliseconds(Clock::now() - hover_start));
    }
    auto hover = get_statistics(hover_times);
    std::printf("  hover: median %.3f ms, max %.3f ms\n", hover.m_median,
      hover.m_maximum);
    auto press = view.map(center);
    drag("deep", document, scene, renderer, harness, tools, press,
      Qt::ControlModifier);
    editor.get_selection().clear();
    editor.get_context().set_hover(NodeId());
    drag("top-level", document, scene, renderer, harness, tools,
      view.map(center + Point(3 * DRAG_FRAMES, 2 * DRAG_FRAMES)),
      Qt::NoModifier);
  }
}

int main(int argc, char** argv) {
  auto platform = std::string("-platform");
  auto offscreen = std::string("offscreen");
  auto arguments = std::vector<char*>(argv, argv + argc);
  arguments.insert(arguments.begin() + 1, {platform.data(), offscreen.data()});
  argc = static_cast<int>(arguments.size());
  arguments.push_back(nullptr);
  argv = arguments.data();
  auto application = QGuiApplication(argc, argv);
  auto paths = std::vector<std::filesystem::path>();
  for(auto i = 1; i < argc; ++i) {
    paths.push_back(std::filesystem::path(argv[i]));
  }
  if(paths.empty()) {
    std::fprintf(stderr, "Usage: ToolsBenchmarks <xd file>...\n");
    return 2;
  }
  try {
    for(auto& path : paths) {
      run(path);
    }
  } catch(const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
  return 0;
}

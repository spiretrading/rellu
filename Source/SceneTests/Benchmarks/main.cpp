#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <QGuiApplication>
#include <QtPlugin>
#include <rhi/qrhi_platform.h>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/Scene.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

namespace {
  constexpr auto FIT_MARGIN = 24.0;
  constexpr auto MAXIMUM_FRAMES = 2000;
  constexpr auto DRAG_FRAMES = 60;
  constexpr auto SCENE_BUDGET = std::chrono::milliseconds(8);

  struct Options {
    std::vector<std::filesystem::path> m_paths;
    int m_frames = 30;
    QSize m_size = QSize(1920, 1080);
    bool m_is_software = false;
    bool m_is_text = false;
  };

  struct Frame {
    double m_cpu;
    double m_gpu;
  };

  using Clock = std::chrono::steady_clock;

  double to_milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  double get_level(double zoom) {
    return std::exp2(std::ceil(std::log2(zoom)));
  }

  double get_median(std::vector<double> values) {
    if(values.empty()) {
      return 0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
  }

  double get_maximum(const std::vector<double>& values) {
    if(values.empty()) {
      return 0;
    }
    return *std::max_element(values.begin(), values.end());
  }

  class Harness {
    public:
      Harness(Renderer& renderer, const Options& options)
          : m_renderer(&renderer) {
        auto flags = QRhi::Flags(QRhi::EnableTimestamps);
        if(options.m_is_software) {
          flags |= QRhi::PreferSoftwareRenderer;
        }
        auto parameters = QRhiD3D11InitParams();
        m_rhi.reset(QRhi::create(QRhi::D3D11, &parameters, flags));
        if(!m_rhi) {
          throw std::runtime_error("Unable to create the QRhi.");
        }
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8,
          options.m_size, 1, QRhiTexture::RenderTarget));
        m_texture->create();
        m_depth_stencil.reset(m_rhi->newRenderBuffer(
          QRhiRenderBuffer::DepthStencil, options.m_size, 1));
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

      std::string get_name() const {
        return std::string(m_rhi->backendName()) + " (" +
          m_rhi->driverInfo().deviceName.toStdString() + ")";
      }

      Frame render() {
        auto start = Clock::now();
        auto buffer = static_cast<QRhiCommandBuffer*>(nullptr);
        m_rhi->beginOffscreenFrame(&buffer);
        m_renderer->render(*buffer, *m_target, 1);
        auto recorded = Clock::now();
        m_rhi->endOffscreenFrame();
        return Frame(to_milliseconds(recorded - start),
          1000 * buffer->lastCompletedGpuTime());
      }

    private:
      Renderer* m_renderer;
      std::unique_ptr<QRhi> m_rhi;
      std::unique_ptr<QRhiTexture> m_texture;
      std::unique_ptr<QRhiRenderBuffer> m_depth_stencil;
      std::unique_ptr<QRhiRenderPassDescriptor> m_descriptor;
      std::unique_ptr<QRhiTextureRenderTarget> m_target;
  };

  std::optional<NodeId> find_shape(const Document& document,
      const NodeId& id, int depth) {
    auto node = document.find(id);
    if(!node || depth > 64 || !node->m_header.m_is_visible) {
      return std::nullopt;
    }
    if(auto shape = node->get_shape()) {
      if(std::holds_alternative<SolidFill>(shape->m_style.m_fill)) {
        return id;
      }
    }
    for(auto& child : node->m_children) {
      if(auto shape = find_shape(document, child, depth + 1)) {
        return shape;
      }
    }
    return std::nullopt;
  }

  void edit(Document& document, const NodeId& id) {
    auto shape = *document.get(id).get_shape();
    auto& fill = std::get<SolidFill>(shape.m_style.m_fill);
    fill.m_color.m_red = 1 - fill.m_color.m_red;
    auto transaction = Transaction(document);
    transaction.replace_payload(id, shape);
    transaction.commit();
  }

  std::size_t count_nodes(const Document& document, const NodeId& id) {
    auto count = std::size_t(1);
    for(auto& child : document.get(id).m_children) {
      count += count_nodes(document, child);
    }
    return count;
  }

  std::optional<NodeId> find_deep_shape(
      const Document& document, const NodeId& id, int depth) {
    auto node = document.find(id);
    if(!node || depth > 64 || !node->m_header.m_is_visible ||
        node->get_reference()) {
      return std::nullopt;
    }
    if(node->get_shape()) {
      return id;
    }
    auto children = std::vector<std::pair<std::size_t, NodeId>>();
    for(auto& child : node->m_children) {
      children.emplace_back(count_nodes(document, child), child);
    }
    std::stable_sort(children.begin(), children.end(),
      [] (const auto& left, const auto& right) {
        return left.first > right.first;
      });
    for(auto& child : children) {
      if(auto shape = find_deep_shape(document, child.second, depth + 1)) {
        return shape;
      }
    }
    return std::nullopt;
  }

  void drag(Document& document, Scene& scene, const NodeId& id,
      const char* name) {
    auto header = document.get(id).m_header;
    auto times = std::vector<double>();
    auto areas = std::vector<double>();
    for(auto i = 0; i != DRAG_FRAMES; ++i) {
      header.m_transform = make_translation(3, 2) * header.m_transform;
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      if(i == 0) {
        transaction.commit();
      } else {
        transaction.amend();
      }
      auto start = Clock::now();
      auto damage = scene.update();
      times.push_back(to_milliseconds(Clock::now() - start));
      auto area = 0.0;
      for(auto& region : damage) {
        area += region.m_bounds.m_width * region.m_bounds.m_height;
      }
      areas.push_back(area);
    }
    document.undo();
    scene.update();
    std::printf("    drag %s (%zu nodes): frame median %.2f ms, max %.2f ms, "
      "damage median %.0f square units\n", name, count_nodes(document, id),
      get_median(times), get_maximum(times), get_median(areas));
  }

  void drag(Document& document, Scene& scene) {
    auto& canvas = document.get_canvas();
    auto largest = NodeId();
    auto largest_size = std::size_t(0);
    for(auto& id : canvas) {
      if(auto size = count_nodes(document, id); size > largest_size) {
        largest = id;
        largest_size = size;
      }
    }
    auto shape = find_deep_shape(document, largest, 0);
    if(!shape) {
      return;
    }
    auto builder = SceneBuilder(document, scene.get_tolerance());
    builder.build(largest);
    auto start = Clock::now();
    auto list = builder.build(largest);
    std::printf("  largest top-level node (%zu nodes, %zu commands): full "
      "rebuild %.2f ms; %zu draw lists in the scene\n", largest_size,
      list.size(), to_milliseconds(Clock::now() - start),
      scene.get_drawing()->get_lists().size());
    drag(document, scene, *shape, "a shape in it");
    auto child = NodeId();
    auto child_size = std::size_t(0);
    for(auto& id : document.get(largest).m_children) {
      if(auto size = count_nodes(document, id); size > child_size) {
        child = id;
        child_size = size;
      }
    }
    if(!child.is_nil() && child != *shape) {
      drag(document, scene, child, "its largest child");
    }
  }

  Matrix make_view(Point center, double zoom, QSize size) {
    return make_translation(size.width() / 2.0 - zoom * center.m_x,
      size.height() / 2.0 - zoom * center.m_y) * make_scale(zoom, zoom);
  }

  double get_fit(const Rect& bounds, QSize size) {
    return std::min(
      std::max(size.width() - 2 * FIT_MARGIN, 1.0) / bounds.m_width,
      std::max(size.height() - 2 * FIT_MARGIN, 1.0) / bounds.m_height);
  }

  bool is_shown(CanvasRenderer& renderer) {
    return !renderer.get_drawing() || renderer.get_tiles().is_view_complete();
  }

  void settle(Harness& harness, CanvasRenderer& renderer) {
    for(auto i = 0; i != MAXIMUM_FRAMES && !renderer.is_complete(); ++i) {
      harness.render();
    }
  }

  void pan(const Options& options, Harness& harness, CanvasRenderer& renderer,
      Matrix view, const std::string& name) {
    renderer.set_view(view);
    auto start = Clock::now();
    auto first = harness.render();
    auto count = 1;
    while(!is_shown(renderer) && count != MAXIMUM_FRAMES) {
      harness.render();
      ++count;
    }
    auto complete = to_milliseconds(Clock::now() - start);
    settle(harness, renderer);
    auto cpu = std::vector<double>();
    auto gpu = std::vector<double>();
    for(auto i = 0; i != options.m_frames; ++i) {
      view = make_translation(23, 13) * view;
      renderer.set_view(view);
      auto frame = harness.render();
      cpu.push_back(frame.m_cpu);
      gpu.push_back(frame.m_gpu);
    }
    std::printf("    %-8s first frame %8.2f ms CPU %7.2f ms GPU; shown "
      "after %d frames, %8.2f ms; pan frame %7.2f ms CPU (max %7.2f), "
      "%6.2f ms GPU\n", name.c_str(), first.m_cpu, first.m_gpu, count,
      complete, get_median(cpu), get_maximum(cpu), get_median(gpu));
  }

  void zoom(Harness& harness, CanvasRenderer& renderer, const Matrix& view,
      double factor, QSize size, const std::string& name) {
    renderer.set_view(view);
    harness.render();
    settle(harness, renderer);
    auto center = Point(size.width() / 2.0, size.height() / 2.0);
    renderer.set_view(make_translation(center.m_x, center.m_y) *
      make_scale(factor, factor) *
      make_translation(-center.m_x, -center.m_y) * view);
    auto start = Clock::now();
    auto first = harness.render();
    auto count = 1;
    while(!is_shown(renderer) && count != MAXIMUM_FRAMES) {
      harness.render();
      ++count;
    }
    std::printf("    %-8s zoom by %.2f: first frame %8.2f ms CPU %7.2f ms "
      "GPU; shown after %d frames, %8.2f ms\n", name.c_str(), factor,
      first.m_cpu, first.m_gpu, count, to_milliseconds(Clock::now() - start));
  }

  void run(const Options& options, const std::filesystem::path& path) {
    auto document = Document();
    auto start = Clock::now();
    import_xd(path, document);
    auto imported = Clock::now();
    std::printf("%s: %zu nodes, %zu top-level nodes, import %.1f ms\n",
      path.filename().string().c_str(), document.get_nodes().get_size(),
      document.get_canvas().size(), to_milliseconds(imported - start));
    auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE);
    start = Clock::now();
    auto list = std::make_shared<const DrawList>(builder.build());
    auto built = Clock::now();
    builder.build();
    auto rebuilt = Clock::now();
    auto scene = Scene(document, StripRasterizer::TOLERANCE);
    scene.update();
    auto updated = Clock::now();
    std::printf("  build: before %.1f ms (%zu commands), rebuild %.1f ms; "
      "after %.1f ms\n", to_milliseconds(built - start), list->size(),
      to_milliseconds(rebuilt - built), to_milliseconds(updated - rebuilt));
    auto& canvas = document.get_canvas();
    auto shape = std::optional<NodeId>();
    for(auto i = canvas.size() / 2; i != canvas.size() && !shape; ++i) {
      shape = find_shape(document, canvas[i], 0);
    }
    if(shape) {
      edit(document, *shape);
      start = Clock::now();
      builder.build();
      auto legacy = Clock::now();
      auto damage = scene.update();
      auto incremental = Clock::now();
      auto area = Rect(0, 0, 0, 0);
      for(auto& region : damage) {
        area = unite(area, region.m_bounds);
      }
      std::printf("  single node edit: before %.2f ms (full rebuild); after "
        "%.2f ms, %zu damaged regions within %.0fx%.0f\n",
        to_milliseconds(legacy - start),
        to_milliseconds(incremental - legacy), damage.size(), area.m_width,
        area.m_height);
      edit(document, *shape);
      scene.update();
    }
    drag(document, scene);
    auto& drawing = *scene.get_drawing();
    auto bounds = drawing.get_bounds();
    if(bounds.is_empty()) {
      return;
    }
    auto fit = get_fit(bounds, options.m_size);
    auto lists = drawing.get_lists();
    auto focus = drawing.get_region(lists.size() / 2).m_bounds.get_center();
    struct View {
      std::string m_name;
      Point m_center;
      double m_zoom;
    };
    auto views = std::vector<View>({View("fit", bounds.get_center(), fit),
      View("100%", focus, 1), View("400%", focus, 4)});
    for(auto& view : views) {
      auto level = get_level(view.m_zoom);
      builder.set_tolerance(StripRasterizer::TOLERANCE / level);
      start = Clock::now();
      auto level_list = std::make_shared<const DrawList>(builder.build());
      auto legacy = Clock::now();
      scene.set_tolerance(StripRasterizer::TOLERANCE / level);
      scene.update();
      auto incremental = Clock::now();
      std::printf("  view %s (zoom %.4f): tolerance change before %.1f ms, "
        "after %.1f ms\n", view.m_name.c_str(), view.m_zoom,
        to_milliseconds(legacy - start),
        to_milliseconds(incremental - legacy));
      auto matrix = make_view(view.m_center, view.m_zoom, options.m_size);
      {
        auto renderer = CanvasRenderer();
        renderer.set_draw_list(level_list);
        auto harness = Harness(renderer, options);
        if(&view == &views.front()) {
          std::printf("  %s, %dx%d\n", harness.get_name().c_str(),
            options.m_size.width(), options.m_size.height());
        }
        pan(options, harness, renderer, matrix, "before");
        zoom(harness, renderer, matrix, 1.25, options.m_size, "before");
      }
      {
        auto renderer = CanvasRenderer();
        renderer.set_drawing(scene.get_drawing());
        auto harness = Harness(renderer, options);
        pan(options, harness, renderer, matrix, "after");
        zoom(harness, renderer, matrix, 1.25, options.m_size, "after");
      }
    }
  }
  void collect_texts(const Document& document, const NodeId& id, int depth,
      std::vector<NodeId>& texts) {
    auto node = document.find(id);
    if(!node || depth > 64 || !node->m_header.m_is_visible) {
      return;
    }
    if(node->get_text()) {
      texts.push_back(id);
      return;
    }
    for(auto& child : node->m_children) {
      collect_texts(document, child, depth + 1, texts);
    }
  }

  std::shared_ptr<const Drawing> draw_texts(
      SceneBuilder& builder, const std::vector<NodeId>& texts) {
    auto lists = std::vector<std::shared_ptr<const DrawList>>();
    for(auto& id : texts) {
      auto list = builder.build(id);
      if(!list.empty()) {
        lists.push_back(std::make_shared<const DrawList>(std::move(list)));
      }
    }
    auto drawing = std::make_shared<Drawing>();
    drawing->set_lists(std::move(lists));
    return drawing;
  }

  std::size_t count_commands(const Drawing& drawing) {
    auto count = std::size_t(0);
    for(auto& list : drawing.get_lists()) {
      count += list->size();
    }
    return count;
  }

  double time_update(Scene& scene) {
    auto start = Clock::now();
    scene.update();
    return to_milliseconds(Clock::now() - start);
  }

  struct Settling {
    double m_total;
    double m_maximum;
    int m_updates;
  };

  Rect get_view_region(const Matrix& view, QSize size) {
    return Rect(-view.m_tx / view.m_a, -view.m_ty / view.m_a,
      size.width() / view.m_a, size.height() / view.m_a);
  }

  Settling settle_scene(Scene& scene, const Rect& region) {
    auto settling = Settling(0, 0, 0);
    while((!scene.is_current() || !scene.is_rasterized()) &&
        settling.m_updates != MAXIMUM_FRAMES) {
      auto start = Clock::now();
      if(!scene.is_current()) {
        scene.update(region, SCENE_BUDGET);
      } else {
        scene.rasterize(region, SCENE_BUDGET);
      }
      auto time = to_milliseconds(Clock::now() - start);
      settling.m_total += time;
      settling.m_maximum = std::max(settling.m_maximum, time);
      ++settling.m_updates;
    }
    return settling;
  }

  void pan_scene(const Options& options, Harness& harness,
      CanvasRenderer& renderer, Scene& scene, Matrix view,
      const std::string& name) {
    renderer.set_view(view);
    harness.render();
    settle(harness, renderer);
    auto updates = std::vector<double>();
    auto cpu = std::vector<double>();
    auto gpu = std::vector<double>();
    for(auto i = 0; i != options.m_frames; ++i) {
      view = make_translation(23, 13) * view;
      renderer.set_view(view);
      auto visible = get_view_region(view, options.m_size);
      scene.set_text_region(
        inflate(visible, visible.m_width, visible.m_height));
      auto start = Clock::now();
      if(!scene.is_current()) {
        renderer.invalidate(scene.update(visible, SCENE_BUDGET));
      } else if(!scene.is_rasterized()) {
        renderer.invalidate(scene.rasterize(visible, SCENE_BUDGET));
      }
      updates.push_back(to_milliseconds(Clock::now() - start));
      auto frame = harness.render();
      cpu.push_back(frame.m_cpu);
      gpu.push_back(frame.m_gpu);
    }
    std::printf("    %-8s pan frame: scene update %.2f ms (max %.2f), "
      "render %.2f ms CPU (max %.2f), %.2f ms GPU\n", name.c_str(),
      get_median(updates), get_maximum(updates), get_median(cpu),
      get_maximum(cpu), get_median(gpu));
  }

  void drag_text(Document& document, Scene& scene, const NodeId& id,
      double step) {
    auto bounds = scene.get_drawing()->get_bounds();
    auto header = document.get(id).m_header;
    auto times = std::vector<double>();
    for(auto i = 0; i != DRAG_FRAMES; ++i) {
      header.m_transform = make_translation(step, 2) * header.m_transform;
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      if(i == 0) {
        transaction.commit();
      } else {
        transaction.amend();
      }
      auto start = Clock::now();
      while(!scene.is_current()) {
        scene.update(bounds, SCENE_BUDGET);
      }
      times.push_back(to_milliseconds(Clock::now() - start));
    }
    auto settling = settle_scene(scene, bounds);
    std::printf("    drag by %.2f (%zu nodes): frame median %.2f ms, max "
      "%.2f ms; then %.1f ms in %d steps (max %.1f ms)\n", step,
      count_nodes(document, id), get_median(times), get_maximum(times),
      settling.m_total, settling.m_updates, settling.m_maximum);
    document.undo();
    scene.update();
  }

  void drag_text(Document& document, Scene& scene) {
    auto largest = NodeId();
    auto largest_size = std::size_t(0);
    for(auto& id : document.get_canvas()) {
      if(auto size = count_nodes(document, id); size > largest_size) {
        largest = id;
        largest_size = size;
      }
    }
    auto child = NodeId();
    auto child_size = std::size_t(0);
    for(auto& id : document.get(largest).m_children) {
      if(auto size = count_nodes(document, id); size > child_size) {
        child = id;
        child_size = size;
      }
    }
    if(child.is_nil()) {
      return;
    }
    drag_text(document, scene, child, 3);
    drag_text(document, scene, child, 0.3);
  }

  void run_text(const Options& options, const std::filesystem::path& path) {
    auto document = Document();
    import_xd(path, document);
    auto texts = std::vector<NodeId>();
    for(auto& id : document.get_canvas()) {
      collect_texts(document, id, 0, texts);
    }
    std::printf("%s: %zu nodes, %zu text nodes\n",
      path.filename().string().c_str(), document.get_nodes().get_size(),
      texts.size());
    auto scene = Scene(document, StripRasterizer::TOLERANCE);
    scene.update();
    auto bounds = scene.get_drawing()->get_bounds();
    if(bounds.is_empty() || texts.empty()) {
      return;
    }
    auto fit = get_fit(bounds, options.m_size);
    auto focus = Point(0, 0);
    {
      auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE);
      auto drawing = draw_texts(builder, texts);
      focus = drawing->get_region(
        drawing->get_lists().size() / 2).m_bounds.get_center();
    }
    struct View {
      std::string m_name;
      Point m_center;
      double m_zoom;
    };
    auto views = std::vector<View>({View("fit", bounds.get_center(), fit),
      View("100%", focus, 1), View("400%", focus, 4)});
    for(auto& view : views) {
      auto tolerance = StripRasterizer::TOLERANCE / get_level(view.m_zoom);
      auto device = make_scale(view.m_zoom, view.m_zoom);
      auto outlines = SceneBuilder(document, tolerance);
      auto start = Clock::now();
      auto outlined = draw_texts(outlines, texts);
      auto outlined_time = to_milliseconds(Clock::now() - start);
      auto cold = SceneBuilder(document, tolerance);
      cold.set_text_transform(device);
      start = Clock::now();
      draw_texts(cold, texts);
      auto cold_time = to_milliseconds(Clock::now() - start);
      auto warm = SceneBuilder(document, tolerance);
      warm.set_text_transform(device);
      start = Clock::now();
      auto rasterized = draw_texts(warm, texts);
      auto warm_time = to_milliseconds(Clock::now() - start);
      std::printf("  view %s (zoom %.4f): text build outlines %.1f ms "
        "(%zu commands), rasters %.1f ms cold, %.1f ms warm (%zu "
        "commands)\n", view.m_name.c_str(), view.m_zoom, outlined_time,
        count_commands(*outlined), cold_time, warm_time,
        count_commands(*rasterized));
      {
        auto builder = SceneBuilder(document, tolerance);
        builder.set_text_transform(device);
        auto times = std::vector<double>();
        for(auto& id : document.get_canvas()) {
          auto dependencies = SceneDependencies();
          auto start = Clock::now();
          builder.build_lists(id, dependencies);
          times.push_back(to_milliseconds(Clock::now() - start));
        }
        std::printf("  top-level rasterize: %zu nodes, median %.2f ms, "
          "max %.2f ms\n", times.size(), get_median(times),
          get_maximum(times));
      }
      auto matrix = make_view(view.m_center, view.m_zoom, options.m_size);
      auto drawings = std::vector<
        std::pair<std::string, std::shared_ptr<const Drawing>>>({
          std::pair("outlines", outlined), std::pair("rasters", rasterized)});
      for(auto& drawing : drawings) {
        auto renderer = CanvasRenderer();
        renderer.set_drawing(drawing.second);
        auto harness = Harness(renderer, options);
        pan(options, harness, renderer, matrix, drawing.first);
        zoom(harness, renderer, matrix, 1.25, options.m_size, drawing.first);
      }
      auto visible = get_view_region(matrix, options.m_size);
      scene.set_tolerance(tolerance);
      scene.set_text_region(
        inflate(visible, visible.m_width, visible.m_height));
      scene.set_text_transform(std::nullopt);
      settle_scene(scene, visible);
      scene.set_text_transform(device);
      auto rasterize = settle_scene(scene, visible);
      scene.set_text_transform(std::nullopt);
      auto fallback = settle_scene(scene, visible);
      std::printf("  scene near the view: rasterize %.1f ms in %d updates "
        "(max %.1f ms), fall back to outlines %.1f ms in %d updates (max "
        "%.1f ms)\n", rasterize.m_total, rasterize.m_updates,
        rasterize.m_maximum, fallback.m_total, fallback.m_updates,
        fallback.m_maximum);
      for(auto is_rasterized : {false, true}) {
        if(is_rasterized) {
          scene.set_text_transform(device);
        } else {
          scene.set_text_transform(std::nullopt);
        }
        settle_scene(scene, visible);
        auto renderer = CanvasRenderer();
        renderer.set_drawing(scene.get_drawing());
        auto harness = Harness(renderer, options);
        pan_scene(options, harness, renderer, scene, matrix,
          is_rasterized ? "scene r" : "scene o");
      }
    }
    scene.set_tolerance(StripRasterizer::TOLERANCE);
    scene.set_text_region(std::nullopt);
    for(auto is_rasterized : {false, true}) {
      if(is_rasterized) {
        scene.set_text_transform(Matrix());
      } else {
        scene.set_text_transform(std::nullopt);
      }
      scene.update();
      std::printf("  drag with text %s at 100%%:\n",
        is_rasterized ? "rasterized" : "outlined");
      drag_text(document, scene);
    }
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
  auto options = Options();
  for(auto i = 1; i < argc; ++i) {
    auto argument = std::string_view(argv[i]);
    if(argument == "--frames" && i + 1 < argc) {
      options.m_frames = std::stoi(argv[++i]);
    } else if(argument == "--size" && i + 2 < argc) {
      options.m_size = QSize(std::stoi(argv[i + 1]), std::stoi(argv[i + 2]));
      i += 2;
    } else if(argument == "--software") {
      options.m_is_software = true;
    } else if(argument == "--text") {
      options.m_is_text = true;
    } else {
      options.m_paths.push_back(std::filesystem::path(argv[i]));
    }
  }
  if(options.m_paths.empty()) {
    std::fprintf(stderr, "Usage: SceneBenchmarks [--frames <count>] "
      "[--size <width> <height>] [--software] [--text] <xd file>...\n");
    return 2;
  }
  try {
    for(auto& path : options.m_paths) {
      if(options.m_is_text) {
        run_text(options, path);
      } else {
        run(options, path);
      }
    }
  } catch(const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
  return 0;
}

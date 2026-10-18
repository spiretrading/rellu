#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <QDir>
#include <QGuiApplication>
#include <QtPlugin>
#include <rhi/qrhi_platform.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "PrototypeRenderer.hpp"

Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;


namespace {
  const auto DOCUMENT_SIZE = QSize(1920, 1080);

  struct Options {
    int m_paths = 10000;
    int m_iterations = 10;
    QString m_output;
  };

  auto parse(int argc, char** argv) {
    auto options = Options();
    for(auto i = 1; i + 1 < argc; i += 2) {
      auto name = std::string(argv[i]);
      if(name == "--paths") {
        options.m_paths = std::stoi(argv[i + 1]);
      } else if(name == "--iterations") {
        options.m_iterations = std::stoi(argv[i + 1]);
      } else if(name == "--output") {
        options.m_output = QString::fromLocal8Bit(argv[i + 1]);
      }
    }
    return options;
  }

  auto make_blob(std::mt19937& random, Point center, double radius) {
    auto uniform = std::uniform_real_distribution<double>(0, 1);
    auto count = std::uniform_int_distribution<int>(5, 9)(random);
    auto points = std::vector<Point>();
    for(auto i = 0; i != count; ++i) {
      auto angle = 2 * std::numbers::pi * (i + 0.4 * uniform(random) - 0.2) /
        count;
      auto distance = radius * (0.45 + 0.55 * uniform(random));
      points.push_back(center +
        Point(std::cos(angle), std::sin(angle)) * distance);
    }
    auto path = Path();
    path.move_to(points[0]);
    for(auto i = 0; i != count; ++i) {
      auto& previous = points[(i + count - 1) % count];
      auto& start = points[i];
      auto& end = points[(i + 1) % count];
      auto& next = points[(i + 2) % count];
      path.cubic_to(start + (end - previous) / 6, end - (next - start) / 6,
        end);
    }
    path.close();
    return path;
  }

  auto make_star(std::mt19937& random, Point center, double radius) {
    auto path = Path(FillRule::EVEN_ODD);
    auto rotation = std::uniform_real_distribution<double>(0, 1)(random);
    for(auto i = 0; i != 5; ++i) {
      auto angle = 2 * std::numbers::pi * (2 * i / 5.0 + rotation);
      auto point = center + Point(std::cos(angle), std::sin(angle)) * radius;
      if(i == 0) {
        path.move_to(point);
      } else {
        path.quadratic_to(center + (point - center) * 0.2, point);
      }
    }
    path.close();
    return path;
  }

  auto make_scene(int count) {
    auto random = std::mt19937(1234);
    auto uniform = std::uniform_real_distribution<double>(0, 1);
    auto list = std::make_shared<DrawList>();
    for(auto i = 0; i != count; ++i) {
      auto width = std::exp(std::log(6.0) + uniform(random) * std::log(30.0));
      auto height =
        std::exp(std::log(6.0) + uniform(random) * std::log(30.0));
      auto bounds = Rect(-40 + uniform(random) * DOCUMENT_SIZE.width(),
        -40 + uniform(random) * DOCUMENT_SIZE.height(), width, height);
      auto center = bounds.get_center();
      auto path = Path();
      auto kind = i % 4;
      if(kind == 0) {
        path = make_rectangle(bounds);
      } else if(kind == 1) {
        auto radius = uniform(random) * std::min(width, height) / 2;
        path = make_rectangle(
          bounds, CornerRadii(radius, radius, radius, radius));
      } else if(kind == 2) {
        path = make_ellipse(center, width / 2, height / 2);
      } else if(i % 8 == 3) {
        path = make_blob(random, center, std::max(width, height) / 2);
      } else {
        path = make_star(random, center, std::max(width, height) / 2);
      }
      auto transform = Matrix();
      if(uniform(random) < 0.25) {
        transform = make_rotation(2 * std::numbers::pi * uniform(random),
          center);
      }
      auto color = make_color(static_cast<int>(uniform(random) * 255),
        static_cast<int>(uniform(random) * 255),
        static_cast<int>(uniform(random) * 255), 0.35 + 0.65 * uniform(random));
      list->push_back(
        FillCommand(std::move(path), transform, SolidPaint(color)));
    }
    return list;
  }

  auto measure(int iterations, auto operation) {
    auto times = std::vector<double>();
    for(auto i = 0; i != iterations; ++i) {
      auto start = std::chrono::steady_clock::now();
      operation();
      auto end = std::chrono::steady_clock::now();
      times.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
  }

  class Harness {
    public:
      Harness(Renderer& renderer, RenderBackend backend, QSize size)
          : m_renderer(&renderer) {
        auto flags = QRhi::Flags(QRhi::EnableTimestamps);
        if(backend.m_is_software) {
          flags |= QRhi::PreferSoftwareRenderer;
        }
        if(backend.m_implementation == QRhi::D3D11) {
          auto parameters = QRhiD3D11InitParams();
          m_rhi.reset(QRhi::create(QRhi::D3D11, &parameters, flags));
        } else {
          auto parameters = QRhiD3D12InitParams();
          m_rhi.reset(QRhi::create(QRhi::D3D12, &parameters, flags));
        }
        if(!m_rhi) {
          throw std::runtime_error("Unable to create the QRhi.");
        }
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1,
          QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        m_texture->create();
        m_depth_stencil.reset(
          m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
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

      double render(double scale) {
        auto buffer = static_cast<QRhiCommandBuffer*>(nullptr);
        m_rhi->beginOffscreenFrame(&buffer);
        m_renderer->render(*buffer, *m_target, scale);
        m_rhi->endOffscreenFrame();
        return 1000 * buffer->lastCompletedGpuTime();
      }

    private:
      Renderer* m_renderer;
      std::unique_ptr<QRhi> m_rhi;
      std::unique_ptr<QRhiTexture> m_texture;
      std::unique_ptr<QRhiRenderBuffer> m_depth_stencil;
      std::unique_ptr<QRhiRenderPassDescriptor> m_descriptor;
      std::unique_ptr<QRhiTextureRenderTarget> m_target;
  };

  auto get_name(RenderBackend backend) {
    auto renderer = CanvasRenderer();
    auto offscreen = OffscreenRenderer(renderer, backend);
    auto info = offscreen.get_rhi().driverInfo();
    return std::string(offscreen.get_rhi().backendName()) + " (" +
      info.deviceName.toStdString() + ")";
  }

  void save(const Options& options, const QImage& image,
      const std::string& name) {
    if(options.m_output.isEmpty()) {
      return;
    }
    QDir().mkpath(options.m_output);
    image.save(QDir(options.m_output).filePath(
      QString::fromStdString(name + ".png")));
    auto scale = image.devicePixelRatio();
    auto crop = image.copy(QRect(QPoint(int(600 * scale), int(300 * scale)),
      QSize(160, 120) * scale));
    crop.setDevicePixelRatio(1);
    crop.scaled(QSize(800, 600), Qt::IgnoreAspectRatio,
      Qt::FastTransformation).save(QDir(options.m_output).filePath(
        QString::fromStdString(name + "_crop.png")));
  }

  void benchmark_frames(const Options& options, RenderBackend backend,
      const std::string& backend_name,
      const std::shared_ptr<const DrawList>& list, double scale) {
    auto size = DOCUMENT_SIZE * scale;
    auto empty = CanvasRenderer();
    auto empty_offscreen = OffscreenRenderer(empty, backend);
    empty_offscreen.render(size, scale);
    auto baseline = measure(options.m_iterations, [&] {
      empty_offscreen.render(size, scale);
    });
    std::printf("  %gx %dx%d, clear and readback %.2f ms\n", scale,
      size.width(), size.height(), baseline);
    auto run = [&] (const std::string& name, auto& renderer, auto invalidate) {
      auto full = 0.0;
      {
        auto offscreen = OffscreenRenderer(renderer, backend);
        auto image = offscreen.render(size, scale);
        save(options, image, backend_name + "_" + name + "_" +
          std::to_string(int(scale)) + "x");
        full = measure(options.m_iterations, [&] {
          invalidate();
          offscreen.render(size, scale);
        });
      }
      auto harness = Harness(renderer, backend, size);
      harness.render(scale);
      auto gpu_times = std::vector<double>();
      auto cached = measure(options.m_iterations, [&] {
        gpu_times.push_back(harness.render(scale));
      });
      std::sort(gpu_times.begin(), gpu_times.end());
      std::printf("    %-20s offscreen frame with preparation and readback "
        "%7.2f ms; frame without preparation %7.2f ms, GPU %7.2f ms\n",
        name.c_str(), full, cached, gpu_times[gpu_times.size() / 2]);
    };
    auto strips = CanvasRenderer();
    strips.set_draw_list(list);
    run("strips", strips, [&] {
      strips.set_view(Matrix());
    });
    for(auto samples : {4, 8}) {
      auto stencil = PrototypeRenderer(Approach::STENCIL, samples);
      stencil.set_draw_list(list);
      run("stencil_msaa" + std::to_string(samples), stencil, [&] {
        stencil.set_view(Matrix());
      });
    }
    auto triangulation = PrototypeRenderer(Approach::TRIANGULATION, 4);
    triangulation.set_draw_list(list);
    run("triangulation_msaa4", triangulation, [&] {
      triangulation.set_view(Matrix());
    });
  }

  void benchmark_preparation(const Options& options,
      const DrawList& list, double scale) {
    auto size = DOCUMENT_SIZE * scale;
    auto view = make_scale(scale, scale);
    auto scene = StripScene();
    auto rasterizer = StripRasterizer();
    rasterizer.rasterize(list, view, size, scene);
    auto threaded = measure(options.m_iterations, [&] {
      rasterizer.rasterize(list, view, size, scene);
    });
    auto single = StripRasterizer(1);
    auto sequential = measure(options.m_iterations, [&] {
      single.rasterize(list, view, size, scene);
    });
    auto texels = std::size_t(0);
    auto spans = std::size_t(0);
    for(auto& strip : scene.m_strips) {
      if(strip.m_column < 0) {
        ++spans;
      } else {
        texels += static_cast<std::size_t>(strip.m_width);
      }
    }
    std::printf("  %gx strips: %.2f ms threaded, %.2f ms single thread; "
      "%zu strips (%zu spans), %zu coverage texels, %d atlas rows\n", scale,
      threaded, sequential, scene.m_strips.size(), spans, texels,
      scene.get_atlas_height());
    auto prototype = PrototypeScene();
    auto stencil = measure(options.m_iterations, [&] {
      prepare_stencil(list, view, size, prototype);
    });
    std::printf("  %gx stencil: %.2f ms single thread; %zu fan vertices, "
      "%zu paths\n", scale, stencil, prototype.m_vertices.size(),
      prototype.m_draws.size());
    auto triangulation = measure(std::max(1, options.m_iterations / 4), [&] {
      prepare_triangulation(list, view, size, prototype);
    });
    std::printf("  %gx triangulation: %.2f ms single thread; "
      "%zu triangle vertices\n", scale, triangulation,
      prototype.m_vertices.size());
  }
}

int main(int argc, char** argv) {
  if(!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  auto application = QGuiApplication(argc, argv);
  auto options = parse(argc, argv);
  auto list = make_scene(options.m_paths);
  std::printf("%d paths, %d iterations, median times\n", options.m_paths,
    options.m_iterations);
  std::printf("CPU preparation\n");
  for(auto scale : {1.0, 2.0}) {
    benchmark_preparation(options, *list, scale);
  }
  for(auto backend : {RenderBackend(QRhi::D3D11, true),
      RenderBackend(QRhi::D3D11, false), RenderBackend(QRhi::D3D12, false)}) {
    auto name = std::string();
    try {
      name = get_name(backend);
    } catch(const std::exception&) {
      continue;
    }
    std::printf("%s\n", name.c_str());
    auto tag = std::string(QRhi::backendName(backend.m_implementation)) +
      (backend.m_is_software ? "_software" : "_hardware");
    for(auto scale : {1.0, 2.0}) {
      benchmark_frames(options, backend, tag, list, scale);
    }
  }
  return 0;
}

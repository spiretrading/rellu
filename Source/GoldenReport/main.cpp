#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QtPlugin>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/ImageComparison.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/References.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

namespace {
  constexpr auto THRESHOLDS = std::array<int, 6>({2, 4, 8, 16, 32, 64});

  struct Options {
    std::filesystem::path m_corpus;
    std::filesystem::path m_output;
    std::string m_filter;
    bool m_is_writing_all = false;
    std::string m_dump;
    ImageTolerance m_tolerance;
  };

  struct Difference {
    std::array<std::int64_t, THRESHOLDS.size()> m_counts = {};
  };

  enum class Scope {
    EXCLUDED,
    FULL,
    MASKED
  };

  struct Tally {
    int m_items = 0;
    int m_matches = 0;
    int m_size_matches = 0;
  };

  const TextStyle& get_style(const Text& text, std::size_t index) {
    auto end = std::size_t(0);
    for(auto& run : text.m_runs) {
      end += run.m_length;
      if(index < end) {
        return run.m_style;
      }
    }
    return text.m_base_style;
  }

  Rect get_text_bounds(const Text& text) {
    auto bounds = Rect(0, 0, 0, 0);
    for(auto& paragraph : (*text.m_layout)->m_paragraphs) {
      for(auto& line : paragraph.m_lines) {
        for(auto& run : line.m_runs) {
          auto& style = get_style(text, run.m_from);
          auto& font = run.m_font ? *run.m_font : style.m_font;
          auto size = font.m_size;
          auto& content = *text.m_content;
          auto from = std::min(run.m_from, content.size());
          auto to = std::clamp(run.m_to, from, content.size());
          auto advance = 0.62;
          if(style.m_transform != TextTransform::NONE) {
            advance = 0.75;
          }
          auto width = static_cast<double>(to - from) * size *
            (advance + std::abs(style.m_letter_spacing) / 1000);
          bounds = unite(bounds, Rect(run.m_position.m_x - size / 4,
            run.m_position.m_y - 1.2 * size, width + size / 2, 1.6 * size));
        }
      }
    }
    return bounds;
  }

  struct FeatureCollector {
    const Document* m_document;
    std::set<std::string> m_features;
    std::vector<Rect> m_text;
    int m_depth = 0;

    void add_text(const Text& text, const Matrix& transform) {
      add_effects(text.m_style.m_effects);
      if(text.m_layout) {
        m_text.push_back(inflate(transform.map(get_text_bounds(text)), 2));
      } else if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
        m_text.push_back(inflate(transform.map(
          Rect(0, 0, area->m_size.m_width, area->m_size.m_height)), 2));
      } else {
        m_features.insert("unbounded_text");
      }
    }

    void add_fill(const Fill& fill) {
      if(std::holds_alternative<GradientFill>(fill)) {
        m_features.insert("gradient");
      } else if(std::holds_alternative<ImageFill>(fill)) {
        m_features.insert("image");
      }
    }

    void add_effects(const std::vector<Effect>& effects) {
      for(auto& effect : effects) {
        if(auto shadow = std::get_if<DropShadow>(&effect)) {
          if(shadow->m_is_visible) {
            m_features.insert("drop_shadow");
          }
        } else if(auto shadow = std::get_if<InnerShadow>(&effect)) {
          if(shadow->m_is_visible) {
            m_features.insert("inner_shadow");
          }
        } else if(auto blur = std::get_if<Blur>(&effect)) {
          if(blur->m_is_visible) {
            m_features.insert(
              blur->m_is_background ? "background_blur" : "blur");
          }
        }
      }
    }

    int add(const NodeId& id, const Matrix& parent) {
      auto& nodes = m_document->get_nodes();
      auto node = nodes.find(id);
      if(!node || m_depth > 256) {
        return 0;
      }
      auto source = node;
      auto children = &node->m_children;
      auto transform = node->m_header.m_transform;
      if(node->get_reference()) {
        auto resolution = resolve_reference(nodes, id);
        if(!resolution) {
          m_features.insert("broken_reference");
          return 0;
        }
        source = &nodes.get(resolution->m_source);
        children = &nodes.get(resolution->m_child_source).m_children;
        transform = resolution->m_transform;
      }
      transform = parent * transform;
      auto& header = source->m_header;
      if(!header.m_is_visible || !(header.m_opacity > 0)) {
        return 0;
      }
      if(header.m_blend_mode != BlendMode::NORMAL) {
        m_features.insert("blend_mode");
      }
      if(auto shape = source->get_shape()) {
        auto& style = shape->m_style;
        add_fill(style.m_fill);
        add_effects(style.m_effects);
        auto count = 0;
        if(!std::holds_alternative<NoFill>(style.m_fill) &&
            !std::holds_alternative<LineShape>(shape->m_geometry)) {
          ++count;
        }
        if(style.m_stroke && style.m_stroke->m_is_enabled &&
            style.m_stroke->m_width > 0) {
          ++count;
        }
        if(count > 1 && header.m_opacity < 1) {
          m_features.insert("shape_opacity");
        }
        return count;
      } else if(auto text = source->get_text()) {
        m_features.insert("text");
        add_text(*text, transform);
        return 1;
      }
      auto count = 0;
      if(auto artboard = source->get_artboard()) {
        transform = parent * make_translation(
          artboard->m_frame.m_x, artboard->m_frame.m_y);
        add_fill(artboard->m_background);
        if(!std::holds_alternative<NoFill>(artboard->m_background)) {
          ++count;
        }
      } else if(auto group = source->get_group()) {
        if(group->m_mask) {
          m_features.insert("mask");
        }
        if(group->m_scroll_group) {
          m_features.insert("scroll_group");
        }
      }
      ++m_depth;
      for(auto& child : *children) {
        count += add(child, transform);
      }
      --m_depth;
      if(count > 1 && header.m_opacity < 1) {
        m_features.insert("group_opacity");
      }
      return count;
    }
  };

  Scope get_scope(const std::set<std::string>& features) {
    if(features.empty()) {
      return Scope::FULL;
    } else if(features.size() == 1 && features.contains("text")) {
      return Scope::MASKED;
    }
    return Scope::EXCLUDED;
  }

  std::string to_name(Scope scope) {
    if(scope == Scope::FULL) {
      return "in";
    } else if(scope == Scope::MASKED) {
      return "masked";
    }
    return "out";
  }

  double to_milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }

  QImage make_mask(const std::vector<Rect>& regions, const QRectF& bounds,
      int scale, QSize size) {
    auto mask = QImage(size, QImage::Format_Grayscale8);
    mask.fill(0);
    auto painter = QPainter(&mask);
    for(auto& region : regions) {
      auto left = std::floor((region.m_x - bounds.x()) * scale);
      auto top = std::floor((region.m_y - bounds.y()) * scale);
      auto right = std::ceil((region.get_right() - bounds.x()) * scale);
      auto bottom = std::ceil((region.get_bottom() - bounds.y()) * scale);
      painter.fillRect(QRectF(left, top, right - left, bottom - top),
        QColor(255, 255, 255));
    }
    return mask;
  }

  double get_coverage(const QImage& mask) {
    if(mask.isNull() || mask.width() * mask.height() == 0) {
      return 0;
    }
    auto count = std::int64_t(0);
    for(auto y = 0; y != mask.height(); ++y) {
      auto line = mask.constScanLine(y);
      for(auto x = 0; x != mask.width(); ++x) {
        count += line[x] != 0;
      }
    }
    return double(count) / (double(mask.width()) * mask.height());
  }

  QImage apply_mask(const QImage& image, const QImage& mask) {
    if(image.isNull()) {
      return image;
    }
    auto result = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    auto height = std::min(result.height(), mask.height());
    auto width = std::min(result.width(), mask.width());
    for(auto y = 0; y != height; ++y) {
      auto line = result.scanLine(y);
      auto flags = mask.constScanLine(y);
      for(auto x = 0; x != width; ++x) {
        if(flags[x]) {
          std::fill(line + 4 * x, line + 4 * x + 4, uchar(0));
        }
      }
    }
    return result;
  }

  void dump(const Document& document, const NodeId& id, const Matrix& parent,
      int depth) {
    auto& nodes = document.get_nodes();
    auto node = nodes.find(id);
    if(!node || depth > 64) {
      return;
    }
    auto source = node;
    auto transform = node->m_header.m_transform;
    auto children = &node->m_children;
    std::cout << std::string(2 * depth, ' ');
    if(node->get_reference()) {
      if(auto resolution = resolve_reference(nodes, id)) {
        source = &nodes.get(resolution->m_source);
        transform = resolution->m_transform;
        children = &nodes.get(resolution->m_child_source).m_children;
        std::cout << "reference to ";
      }
    }
    transform = parent * transform;
    auto& header = source->m_header;
    std::cout << std::format("{} kind {} '{}' visible {} opacity {} "
      "transform ({} {} {} {} {} {})", to_string(source->get_id()),
      static_cast<int>(source->get_kind()), header.m_name,
      header.m_is_visible, header.m_opacity, transform.m_a, transform.m_b,
      transform.m_c, transform.m_d, transform.m_tx, transform.m_ty);
    if(auto shape = source->get_shape()) {
      auto bounds = get_bounds(to_path(shape->m_geometry), transform);
      std::cout << std::format(" geometry {} bounds ({} {} {} {}) fill {}",
        shape->m_geometry.index(), bounds.m_x, bounds.m_y, bounds.m_width,
        bounds.m_height, shape->m_style.m_fill.index());
      if(auto solid = std::get_if<SolidFill>(&shape->m_style.m_fill)) {
        std::cout << std::format(" color ({} {} {} {})", solid->m_color.m_red,
          solid->m_color.m_green, solid->m_color.m_blue,
          solid->m_color.m_alpha);
      }
      if(auto& stroke = shape->m_style.m_stroke) {
        std::cout << std::format(" stroke {} align {} cap {} join {} enabled "
          "{} dashes {} color ({} {} {} {})", stroke->m_width,
          static_cast<int>(stroke->m_alignment),
          static_cast<int>(stroke->m_cap), static_cast<int>(stroke->m_join),
          stroke->m_is_enabled, stroke->m_dashes.size(),
          stroke->m_color.m_red, stroke->m_color.m_green,
          stroke->m_color.m_blue, stroke->m_color.m_alpha);
      }
    } else if(auto artboard = source->get_artboard()) {
      transform = parent * make_translation(
        artboard->m_frame.m_x, artboard->m_frame.m_y);
      std::cout << std::format(" frame ({} {} {} {})", artboard->m_frame.m_x,
        artboard->m_frame.m_y, artboard->m_frame.m_width,
        artboard->m_frame.m_height);
    }
    std::cout << '\n';
    for(auto& child : *children) {
      dump(document, child, transform, depth + 1);
    }
  }

  std::string to_string(const QString& text) {
    return text.toStdString();
  }

  std::string join(const std::set<std::string>& features) {
    auto result = std::string();
    for(auto& feature : features) {
      if(!result.empty()) {
        result += ' ';
      }
      result += feature;
    }
    return result;
  }

  QSize get_pixel_size(const QRectF& bounds, int scale) {
    return QSize(static_cast<int>(std::ceil(bounds.width() * scale - 0.01)),
      static_cast<int>(std::ceil(bounds.height() * scale - 0.01)));
  }

  Difference measure(const QImage& actual, const QImage& expected) {
    auto left =
      actual.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    auto right =
      expected.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    auto difference = Difference();
    auto width = std::min(left.width(), right.width());
    auto height = std::min(left.height(), right.height());
    for(auto y = 0; y != height; ++y) {
      auto first = left.constScanLine(y);
      auto second = right.constScanLine(y);
      for(auto x = 0; x != 4 * width; x += 4) {
        auto maximum = 0;
        for(auto channel = 0; channel != 4; ++channel) {
          maximum = std::max(maximum,
            std::abs(int(first[x + channel]) - int(second[x + channel])));
        }
        for(auto i = std::size_t(0); i != THRESHOLDS.size(); ++i) {
          if(maximum > THRESHOLDS[i]) {
            ++difference.m_counts[i];
          }
        }
      }
    }
    return difference;
  }

  QString to_path(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.u16string());
  }

  std::filesystem::path to_filesystem(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
  }

  std::string escape(std::string text) {
    std::replace(text.begin(), text.end(), ',', ';');
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
  }

  void time(const std::filesystem::path& path) {
    auto document = Document();
    auto start = std::chrono::steady_clock::now();
    import_xd(path, document);
    auto imported = std::chrono::steady_clock::now();
    auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE);
    auto list = builder.build();
    auto built = std::chrono::steady_clock::now();
    auto rebuilt_list = builder.build();
    auto rebuilt = std::chrono::steady_clock::now();
    builder.set_tolerance(StripRasterizer::TOLERANCE / 8);
    auto refined_list = builder.build();
    auto refined = std::chrono::steady_clock::now();
    auto segments = std::size_t(0);
    for(auto& command : list) {
      segments += std::get<FillCommand>(command).m_path.get_segments().size();
    }
    std::cout << std::format("{}: {} nodes, {} commands, {} segments; import "
      "{:.1f} ms, build {:.1f} ms, rebuild {:.1f} ms, rebuild at 8x {:.1f} "
      "ms\n", path.filename().string(), document.get_nodes().get_size(),
      list.size(), segments, to_milliseconds(imported - start),
      to_milliseconds(built - imported), to_milliseconds(rebuilt - built),
      to_milliseconds(refined - rebuilt));
  }

  int run(const Options& options) {
    auto root = get_reference_root();
    auto documents = list_reference_documents(root);
    std::filesystem::create_directories(options.m_output);
    auto summary = std::ofstream(options.m_output / "summary.csv");
    summary << "document,index,guid,name,type,in_scope,features,scale,"
      "expected_width,expected_height,actual_width,actual_height,"
      "size_match,mismatch_fraction,max_difference,mean_difference,match";
    for(auto threshold : THRESHOLDS) {
      summary << ",over_" << threshold;
    }
    summary << ",build_ms,render_ms,masked_fraction\n";
    auto renderer = CanvasRenderer();
    renderer.set_background(QColor(0, 0, 0, 0));
    auto offscreen = OffscreenRenderer(renderer);
    auto tallies = std::array<std::array<Tally, 3>, 3>();
    auto all = std::array<Tally, 3>();
    auto item_counts = std::array<int, 3>();
    for(auto& path : documents) {
      if(!options.m_filter.empty() &&
          to_string(path).find(options.m_filter) == std::string::npos) {
        continue;
      }
      auto reference = load_reference_document(root, path);
      auto document = Document();
      auto import_start = std::chrono::steady_clock::now();
      try {
        import_xd(options.m_corpus / to_filesystem(path + ".xd"), document);
      } catch(const std::exception& error) {
        std::cout << std::format("FAIL {}: {}\n", to_string(path),
          error.what());
        continue;
      }
      auto import_time = to_milliseconds(
        std::chrono::steady_clock::now() - import_start);
      auto builder = SceneBuilder(document, StripRasterizer::TOLERANCE);
      auto full_start = std::chrono::steady_clock::now();
      auto full = builder.build();
      auto full_time =
        to_milliseconds(std::chrono::steady_clock::now() - full_start);
      auto rebuild_start = std::chrono::steady_clock::now();
      builder.build();
      auto rebuild_time =
        to_milliseconds(std::chrono::steady_clock::now() - rebuild_start);
      std::cout << std::format("{}: {} nodes, import {:.1f} ms, build {:.1f} "
        "ms ({} commands), rebuild {:.1f} ms\n", to_string(path),
        document.get_nodes().get_size(), import_time, full_time, full.size(),
        rebuild_time);
      auto directory = options.m_output / to_filesystem(path);
      for(auto& item : reference.m_items) {
        auto id = parse_node_id(to_string(item.m_guid));
        auto collector = FeatureCollector(&document);
        if(!id || !document.find(*id)) {
          collector.m_features.insert("missing");
        } else {
          collector.add(*id, Matrix());
        }
        auto scope = get_scope(collector.m_features);
        if(id && to_string(item.m_guid) == options.m_dump) {
          dump(document, *id, Matrix(), 0);
          for(auto& region : collector.m_text) {
            std::cout << std::format("text ({} {} {} {})\n", region.m_x,
              region.m_y, region.m_width, region.m_height);
          }
        }
        ++item_counts[static_cast<int>(scope)];
        for(auto& image : item.m_images) {
          auto scale = image.first;
          auto expected = QImage(image.second);
          expected.setDevicePixelRatio(1);
          auto size = get_pixel_size(item.m_draw_bounds, scale);
          auto actual = QImage();
          auto build_time = 0.0;
          auto render_time = 0.0;
          if(id && !size.isEmpty()) {
            auto start = std::chrono::steady_clock::now();
            builder.set_tolerance(StripRasterizer::TOLERANCE / scale);
            auto list = std::make_shared<DrawList>(builder.build(*id));
            auto built = std::chrono::steady_clock::now();
            renderer.set_draw_list(list);
            renderer.set_view(make_translation(
              -item.m_draw_bounds.x(), -item.m_draw_bounds.y()));
            try {
              actual = offscreen.render(size, scale);
              actual.setDevicePixelRatio(1);
            } catch(const std::exception& error) {
              std::cout << std::format("  render failed {}: {}\n",
                item.m_index, error.what());
            }
            build_time = to_milliseconds(built - start);
            render_time =
              to_milliseconds(std::chrono::steady_clock::now() - built);
          }
          auto masked_actual = actual;
          auto masked_expected = expected;
          auto masked_fraction = 0.0;
          if(scope == Scope::MASKED) {
            auto mask = make_mask(collector.m_text, item.m_draw_bounds, scale,
              expected.size());
            masked_fraction = get_coverage(mask);
            masked_actual = apply_mask(actual, mask);
            masked_expected = apply_mask(expected, mask);
          }
          auto comparison = compare_images(
            masked_actual, masked_expected, options.m_tolerance);
          auto difference = measure(masked_actual, masked_expected);
          auto is_size_match = actual.size() == expected.size();
          for(auto& tally : {&all, &tallies[static_cast<int>(scope)]}) {
            auto& entry = (*tally)[scale];
            ++entry.m_items;
            entry.m_matches += comparison.m_is_match;
            entry.m_size_matches += is_size_match;
          }
          summary << std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},"
            "{:.6f},{},{:.4f},{}", escape(to_string(path)), item.m_index,
            to_string(item.m_guid), escape(to_string(item.m_name)),
            to_string(item.m_type), to_name(scope),
            join(collector.m_features), scale, expected.width(),
            expected.height(), actual.width(), actual.height(),
            is_size_match ? 1 : 0, comparison.m_fraction,
            comparison.m_maximum_difference, comparison.m_mean_difference,
            comparison.m_is_match ? 1 : 0);
          for(auto count : difference.m_counts) {
            summary << ',' << count;
          }
          summary << std::format(",{:.2f},{:.2f},{:.4f}\n", build_time,
            render_time, masked_fraction);
          if(options.m_is_writing_all ||
              (scope != Scope::EXCLUDED && !comparison.m_is_match)) {
            std::filesystem::create_directories(directory);
            auto name = std::format("{}@{}x", item.m_index, scale);
            actual.save(to_path(directory / (name + "-rellu.png")));
            comparison.m_highlight.save(
              to_path(directory / (name + "-diff.png")));
          }
        }
      }
      summary.flush();
    }
    std::cout << std::format("\n{} items: {} in scope, {} in scope with "
      "text masked, {} out of scope\n", item_counts[0] + item_counts[1] +
      item_counts[2], item_counts[1], item_counts[2], item_counts[0]);
    for(auto scope : {Scope::FULL, Scope::MASKED, Scope::EXCLUDED}) {
      for(auto scale = 1; scale <= 2; ++scale) {
        auto& tally = scope == Scope::EXCLUDED ? all[scale] :
          tallies[static_cast<int>(scope)][scale];
        std::cout << std::format("{} {}x: {} of {} match, {} sizes match\n",
          scope == Scope::EXCLUDED ? "all" : to_name(scope), scale,
          tally.m_matches, tally.m_items, tally.m_size_matches);
      }
    }
    return 0;
  }
}

int main(int argc, char** argv) {
  if(argc >= 3 && std::string_view(argv[1]) == "--time") {
    try {
      for(auto i = 2; i < argc; ++i) {
        time(std::filesystem::path(argv[i]));
      }
    } catch(const std::exception& error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
    return 0;
  }
  if(argc < 3) {
    std::cerr << "Usage: GoldenReport <corpus directory> <output directory> "
      "[--filter <text>] [--all] [--dump <guid>] "
      "[--tolerance <difference> <fraction>]\n"
      "       GoldenReport --time <xd file>...\n";
    return 2;
  }
  if(!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  auto application = QGuiApplication(argc, argv);
  auto options = Options();
  options.m_corpus = std::filesystem::path(argv[1]);
  options.m_output = std::filesystem::path(argv[2]);
  for(auto i = 3; i < argc; ++i) {
    auto argument = std::string_view(argv[i]);
    if(argument == "--filter" && i + 1 < argc) {
      options.m_filter = argv[++i];
    } else if(argument == "--dump" && i + 1 < argc) {
      options.m_dump = argv[++i];
    } else if(argument == "--all") {
      options.m_is_writing_all = true;
    } else if(argument == "--tolerance" && i + 2 < argc) {
      options.m_tolerance = ImageTolerance(
        std::atoi(argv[i + 1]), std::atof(argv[i + 2]));
      i += 2;
    } else {
      std::cerr << "Unknown argument " << argument << '\n';
      return 2;
    }
  }
  try {
    return run(options);
  } catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

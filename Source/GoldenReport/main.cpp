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
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QtPlugin>
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/ImageComparison.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Rendering/References.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"
#include "Rellu/Text/FontLibrary.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

namespace {
  constexpr auto THRESHOLDS = std::array<int, 6>({2, 4, 8, 16, 32, 64});
  const auto UNSUPPORTED = std::set<std::string>({"broken_reference",
    "font_style_substitution", "missing", "not_exported", "text_paint",
    "text_stroke"});

  struct Options {
    std::filesystem::path m_corpus;
    std::filesystem::path m_output;
    std::filesystem::path m_references;
    std::vector<std::filesystem::path> m_fonts;
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
    FULL
  };

  struct Tally {
    int m_items = 0;
    int m_matches = 0;
    int m_size_matches = 0;
    int m_within_16 = 0;
    int m_within_2 = 0;
    int m_identical = 0;
  };

  bool is_within(
      const ImageComparison& comparison, bool is_size_match, int difference) {
    return is_size_match && comparison.m_maximum_difference <= difference;
  }

  std::vector<Font> get_fonts(const Text& text) {
    auto fonts = std::vector<Font>();
    auto add = [&] (const Font& font) {
      if(std::find(fonts.begin(), fonts.end(), font) == fonts.end()) {
        fonts.push_back(font);
      }
    };
    if(text.m_runs.empty()) {
      add(text.m_base_style.m_font);
    }
    for(auto& run : text.m_runs) {
      add(run.m_style.m_font);
    }
    if(text.m_layout) {
      for(auto& paragraph : (*text.m_layout)->m_paragraphs) {
        for(auto& line : paragraph.m_lines) {
          for(auto& run : line.m_runs) {
            if(run.m_font) {
              add(*run.m_font);
            }
          }
        }
      }
    }
    return fonts;
  }

  bool is_exact(FontMatch match) {
    return match == FontMatch::POSTSCRIPT_NAME ||
      match == FontMatch::FAMILY_AND_STYLE;
  }

  struct FeatureCollector {
    const Document* m_document;
    std::set<std::string> m_features;
    int m_depth = 0;

    void add_text(const Text& text) {
      auto& fill = text.m_style.m_fill;
      if(!std::holds_alternative<NoFill>(fill) &&
          !std::holds_alternative<SolidFill>(fill)) {
        m_features.insert("text_paint");
      }
      for(auto& effect : text.m_style.m_effects) {
        if(std::visit([] (const auto& value) {
            return value.m_is_visible;
          }, effect)) {
          m_features.insert("text_effect");
        }
      }
      auto& stroke = text.m_style.m_stroke;
      if(stroke && stroke->m_is_enabled && stroke->m_width > 0) {
        m_features.insert("text_stroke");
      }
      for(auto& font : get_fonts(text)) {
        auto match = get_default_font_library().resolve(font).m_match;
        if(match == FontMatch::FAMILY || match == FontMatch::NONE) {
          m_features.insert("font_style_substitution");
        }
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
        auto poster = shape->m_video ?
          std::optional<Fill>(shape->m_video->m_poster) : std::nullopt;
        auto& fill = poster ? *poster : style.m_fill;
        add_fill(fill);
        add_effects(style.m_effects);
        auto count = 0;
        if(!std::holds_alternative<NoFill>(fill) &&
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
        add_text(*text);
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
        if(group->m_repeat_grid) {
          m_features.insert("repeat_grid");
        }
        if(group->m_scroll_group) {
          m_features.insert("scroll_group");
        }
        if(group->m_stack) {
          m_features.insert("stack");
        }
        if(group->m_padding) {
          m_features.insert("padding");
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
    if(std::any_of(features.begin(), features.end(), [] (const auto& name) {
        return UNSUPPORTED.contains(name);
      })) {
      return Scope::EXCLUDED;
    }
    return Scope::FULL;
  }

  std::string to_name(Scope scope) {
    if(scope == Scope::FULL) {
      return "in";
    }
    return "out";
  }

  double to_milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
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

  void report_fonts(const Document& document) {
    auto fonts = std::vector<Font>();
    document.get_nodes().for_each([&] (const Node& node) {
      if(auto text = node.get_text()) {
        for(auto font : get_fonts(*text)) {
          font.m_size = 0;
          if(std::find(fonts.begin(), fonts.end(), font) == fonts.end()) {
            fonts.push_back(font);
          }
        }
      }
    });
    auto faces = std::set<std::string>();
    auto substitutions = std::string();
    for(auto& font : fonts) {
      auto resolution = get_default_font_library().resolve(font);
      auto name = std::string("none");
      if(resolution.m_typeface) {
        name = resolution.m_typeface->get_names().m_postscript_name;
        faces.insert(name);
      }
      if(!is_exact(resolution.m_match)) {
        substitutions += std::format(" {}/{}/{} -> {} ({});", font.m_family,
          font.m_style, font.m_postscript_name, name,
          to_string(resolution.m_match));
      }
    }
    std::cout << "  fonts: " << join(faces);
    if(!substitutions.empty()) {
      std::cout << "; substituted:" << substitutions;
    }
    std::cout << '\n';
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
      if(auto fill = std::get_if<FillCommand>(&command)) {
        segments += fill->m_path->get_segments().size();
      }
    }
    std::cout << std::format("{}: {} nodes, {} commands, {} segments; import "
      "{:.1f} ms, build {:.1f} ms, rebuild {:.1f} ms, rebuild at 8x {:.1f} "
      "ms\n", path.filename().string(), document.get_nodes().get_size(),
      list.size(), segments, to_milliseconds(imported - start),
      to_milliseconds(built - imported), to_milliseconds(rebuilt - built),
      to_milliseconds(refined - rebuilt));
  }

  int run(const Options& options) {
    auto root = to_path(options.m_references);
    auto documents = list_reference_documents(root);
    std::filesystem::create_directories(options.m_output);
    auto summary = std::ofstream(options.m_output / "summary.csv");
    summary << "document,index,guid,name,type,in_scope,features,scale,"
      "expected_width,expected_height,actual_width,actual_height,"
      "size_match,mismatch_fraction,max_difference,mean_difference,match,"
      "within_16,within_2,identical";
    for(auto threshold : THRESHOLDS) {
      summary << ",over_" << threshold;
    }
    summary << ",build_ms,render_ms\n";
    auto renderer = CanvasRenderer();
    renderer.set_background(QColor(0, 0, 0, 0));
    auto offscreen = OffscreenRenderer(renderer);
    auto tallies = std::array<std::array<Tally, 3>, 2>();
    auto all = std::array<Tally, 3>();
    auto item_counts = std::array<int, 2>();
    auto reasons = std::map<std::string, std::array<int, 2>>();
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
      report_fonts(document);
      auto directory = options.m_output / to_filesystem(path);
      for(auto& item : reference.m_items) {
        auto id = parse_node_id(to_string(item.m_guid));
        auto collector = FeatureCollector(&document);
        if(!id || !document.find(*id)) {
          collector.m_features.insert("missing");
        } else {
          collector.add(*id, Matrix());
        }
        if(std::any_of(reference.m_errors.begin(), reference.m_errors.end(),
            [&] (const auto& error) {
              return error.m_index == item.m_index;
            })) {
          collector.m_features.insert("not_exported");
        }
        auto scope = get_scope(collector.m_features);
        if(id && to_string(item.m_guid) == options.m_dump) {
          dump(document, *id, Matrix(), 0);
        }
        ++item_counts[static_cast<int>(scope)];
        auto unsupported = std::vector<std::string>();
        std::ranges::set_intersection(collector.m_features, UNSUPPORTED,
          std::back_inserter(unsupported));
        for(auto& name : unsupported) {
          auto& count = reasons[name];
          ++count[0];
          count[1] += unsupported.size() == 1;
        }
        for(auto& image : item.m_images) {
          auto scale = image.first;
          auto expected = QImage(image.second);
          if(expected.isNull()) {
            std::cout << std::format("  unreadable reference {}@{}x\n",
              item.m_index, scale);
          }
          expected.setDevicePixelRatio(1);
          auto size = get_pixel_size(item.m_draw_bounds, scale);
          auto actual = QImage();
          auto build_time = 0.0;
          auto render_time = 0.0;
          if(id && !size.isEmpty()) {
            auto start = std::chrono::steady_clock::now();
            builder.set_tolerance(StripRasterizer::TOLERANCE / scale);
            builder.set_device_transform(make_scale(scale, scale) *
              make_translation(
                -item.m_draw_bounds.x(), -item.m_draw_bounds.y()));
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
          auto comparison =
            compare_images(actual, expected, options.m_tolerance);
          auto difference = measure(actual, expected);
          auto is_size_match = actual.size() == expected.size();
          auto is_within_16 = is_within(comparison, is_size_match, 16);
          auto is_within_2 = is_within(comparison, is_size_match, 2);
          auto is_identical = is_within(comparison, is_size_match, 0);
          for(auto& tally : {&all, &tallies[static_cast<int>(scope)]}) {
            auto& entry = (*tally)[scale];
            ++entry.m_items;
            entry.m_matches += comparison.m_is_match;
            entry.m_size_matches += is_size_match;
            entry.m_within_16 += is_within_16;
            entry.m_within_2 += is_within_2;
            entry.m_identical += is_identical;
          }
          summary << std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},"
            "{:.6f},{},{:.4f},{},{},{},{}", escape(to_string(path)),
            item.m_index, to_string(item.m_guid),
            escape(to_string(item.m_name)), to_string(item.m_type),
            to_name(scope), join(collector.m_features), scale,
            expected.width(), expected.height(), actual.width(),
            actual.height(), is_size_match ? 1 : 0, comparison.m_fraction,
            comparison.m_maximum_difference, comparison.m_mean_difference,
            comparison.m_is_match ? 1 : 0, is_within_16 ? 1 : 0,
            is_within_2 ? 1 : 0, is_identical ? 1 : 0);
          for(auto count : difference.m_counts) {
            summary << ',' << count;
          }
          summary << std::format(",{:.2f},{:.2f}\n", build_time,
            render_time);
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
    std::cout << std::format("\n{} items: {} in scope, {} out of scope\n",
      item_counts[0] + item_counts[1], item_counts[1], item_counts[0]);
    for(auto& reason : reasons) {
      std::cout << std::format("out of scope for {}: {} ({} for it alone)\n",
        reason.first, reason.second[0], reason.second[1]);
    }
    for(auto scope : {Scope::FULL, Scope::EXCLUDED}) {
      for(auto scale = 1; scale <= 2; ++scale) {
        auto& tally = scope == Scope::EXCLUDED ? all[scale] :
          tallies[static_cast<int>(scope)][scale];
        std::cout << std::format("{} {}x: {} of {} match, {} sizes match, {} "
          "within 16, {} within 2, {} identical\n",
          scope == Scope::EXCLUDED ? "all" : to_name(scope), scale,
          tally.m_matches, tally.m_items, tally.m_size_matches,
          tally.m_within_16, tally.m_within_2, tally.m_identical);
      }
    }
    return 0;
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
  QImageReader::setAllocationLimit(0);
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
      "--references <directory> [--fonts <directory>]... [--filter <text>] "
      "[--all] [--dump <guid>] [--tolerance <difference> <fraction>]\n"
      "       GoldenReport --time <xd file>...\n";
    return 2;
  }
  auto options = Options();
  options.m_corpus = std::filesystem::path(argv[1]);
  options.m_output = std::filesystem::path(argv[2]);
  for(auto i = 3; i < argc; ++i) {
    auto argument = std::string_view(argv[i]);
    if(argument == "--references" && i + 1 < argc) {
      options.m_references = std::filesystem::path(argv[++i]);
    } else if(argument == "--fonts" && i + 1 < argc) {
      options.m_fonts.push_back(std::filesystem::path(argv[++i]));
    } else if(argument == "--filter" && i + 1 < argc) {
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
  if(options.m_references.empty()) {
    std::cerr << "Missing --references <directory>.\n";
    return 2;
  }
  if(!options.m_fonts.empty()) {
    set_default_font_directories(options.m_fonts);
  }
  try {
    return run(options);
  } catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

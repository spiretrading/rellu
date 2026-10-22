#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <QBuffer>
#include <QImage>
#include <QImageWriter>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Io/PdfExport.hpp"
#include "Rellu/Rendering/CanvasRenderer.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Text/TextLayout.hpp"
#include "PdfFile.hpp"
#include "PdfFonts.hpp"

using namespace Rellu;

namespace {
  constexpr auto RASTER_SCALE = 4.0;
  constexpr auto SHADOW_SCALE = 2.0;
  constexpr auto BLUR_SCALE = 1.0;
  constexpr auto LARGE_SHADOW_BLUR = 16.0;
  constexpr auto BACKDROP_SCALE = 0.5;
  constexpr auto TOLERANCE = 0.01;
  constexpr auto EQUIVALENT_Y_OFFSET = 0.015;
  constexpr auto PROCEDURES = "[/PDF /Text /ImageB /ImageC /ImageI]";
  constexpr auto PRODUCER = "Rellu";
  constexpr auto MAXIMUM_TREE_NODE = std::size_t(8);

  double to_float(double value) {
    return static_cast<float>(value);
  }

  Matrix to_float(const Matrix& matrix) {
    return Matrix(to_float(matrix.m_a), to_float(matrix.m_b),
      to_float(matrix.m_c), to_float(matrix.m_d), to_float(matrix.m_tx),
      to_float(matrix.m_ty));
  }

  std::string to_pdf_values(const Matrix& matrix) {
    return to_pdf_number(matrix.m_a) + ' ' + to_pdf_number(matrix.m_b) + ' ' +
      to_pdf_number(matrix.m_c) + ' ' + to_pdf_number(matrix.m_d) + ' ' +
      to_pdf_number(matrix.m_tx) + ' ' + to_pdf_number(matrix.m_ty);
  }

  std::string to_pdf_matrix(const Matrix& matrix) {
    return '[' + to_pdf_values(matrix) + ']';
  }

  std::string to_pdf_rect(const Rect& rect) {
    return to_pdf_array({to_pdf_number(rect.get_left()),
      to_pdf_number(rect.get_top()), to_pdf_number(rect.get_right()),
      to_pdf_number(rect.get_bottom())});
  }

  std::string to_pdf_rgb(const Color& color) {
    return to_pdf_color(color.m_red) + ' ' + to_pdf_color(color.m_green) +
      ' ' + to_pdf_color(color.m_blue) + ' ';
  }

  const char* to_pdf_blend(BlendMode mode) {
    switch(mode) {
      case BlendMode::MULTIPLY:
        return "/Multiply";
      case BlendMode::SCREEN:
        return "/Screen";
      case BlendMode::OVERLAY:
        return "/Overlay";
      case BlendMode::DARKEN:
        return "/Darken";
      case BlendMode::LIGHTEN:
        return "/Lighten";
      case BlendMode::COLOR_DODGE:
        return "/ColorDodge";
      case BlendMode::COLOR_BURN:
        return "/ColorBurn";
      case BlendMode::HARD_LIGHT:
        return "/HardLight";
      case BlendMode::SOFT_LIGHT:
        return "/SoftLight";
      case BlendMode::DIFFERENCE:
        return "/Difference";
      case BlendMode::EXCLUSION:
        return "/Exclusion";
      case BlendMode::HUE:
        return "/Hue";
      case BlendMode::SATURATION:
        return "/Saturation";
      case BlendMode::COLOR:
        return "/Color";
      case BlendMode::LUMINOSITY:
        return "/Luminosity";
      default:
        return "/Normal";
    }
  }

  int to_pdf_cap(StrokeCap cap) {
    if(cap == StrokeCap::ROUND) {
      return 1;
    } else if(cap == StrokeCap::SQUARE) {
      return 2;
    }
    return 0;
  }

  int to_pdf_join(StrokeJoin join) {
    if(join == StrokeJoin::ROUND) {
      return 1;
    } else if(join == StrokeJoin::BEVEL) {
      return 2;
    }
    return 0;
  }

  Rect round_out(const Rect& rect) {
    auto left = std::floor(to_float(rect.get_left()));
    auto top = std::floor(to_float(rect.get_top()));
    auto right = std::ceil(to_float(rect.get_right()));
    auto bottom = std::ceil(to_float(rect.get_bottom()));
    return Rect(left, top, right - left, bottom - top);
  }

  Rect snap(const Rect& rect, double step) {
    auto left = std::floor(rect.get_left() / step) * step;
    auto top = std::floor(rect.get_top() / step) * step;
    auto right = std::ceil(rect.get_right() / step) * step;
    auto bottom = std::ceil(rect.get_bottom() / step) * step;
    return Rect(left, top, right - left, bottom - top);
  }

  Rect map_bounds(const Matrix& matrix, const Rect& rect) {
    auto points = std::array<Point, 4>({matrix.map(rect.get_top_left()),
      matrix.map(Point(rect.get_right(), rect.get_top())),
      matrix.map(rect.get_bottom_right()),
      matrix.map(Point(rect.get_left(), rect.get_bottom()))});
    auto left = points[0].m_x;
    auto top = points[0].m_y;
    auto right = left;
    auto bottom = top;
    for(auto& point : points) {
      left = std::min(left, point.m_x);
      top = std::min(top, point.m_y);
      right = std::max(right, point.m_x);
      bottom = std::max(bottom, point.m_y);
    }
    return Rect(left, top, right - left, bottom - top);
  }

  std::optional<Rect> unite(
      const std::optional<Rect>& left, const Rect& right) {
    if(right.is_empty()) {
      return left;
    }
    if(!left) {
      return right;
    }
    return Rellu::unite(*left, right);
  }

  std::vector<std::size_t> match_layers(const DrawList& list) {
    auto ends = std::vector<std::size_t>(list.size(), list.size());
    auto open = std::vector<std::size_t>();
    for(auto i = std::size_t(0); i != list.size(); ++i) {
      if(std::holds_alternative<BeginLayerCommand>(list[i])) {
        open.push_back(i);
      } else if(std::holds_alternative<EndLayerCommand>(list[i]) &&
          !open.empty()) {
        ends[open.back()] = i;
        open.pop_back();
      }
    }
    return ends;
  }

  std::optional<Rect> get_list_bounds(const DrawList& list,
      const std::vector<std::size_t>& ends, std::size_t begin,
      std::size_t end) {
    auto bounds = std::optional<Rect>();
    for(auto i = begin; i < end; ++i) {
      auto& command = list[i];
      if(auto fill = std::get_if<FillCommand>(&command)) {
        if(fill->m_path->is_empty()) {
          continue;
        }
        auto region = fill->m_bounds ?
          map_bounds(fill->m_transform, *fill->m_bounds) :
          get_bounds(*fill->m_path, fill->m_transform);
        if(fill->m_clip) {
          region = intersect(region, *fill->m_clip);
        }
        bounds = unite(bounds, region);
      } else if(auto blur = std::get_if<BackdropBlurCommand>(&command)) {
        auto region = get_bounds(*blur->m_path, blur->m_transform);
        if(blur->m_clip) {
          region = intersect(region, *blur->m_clip);
        }
        bounds = unite(bounds, region);
      } else if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
        auto last = std::min(ends[i], end);
        auto inner = get_list_bounds(list, ends, i + 1, last);
        i = last;
        if(!inner) {
          continue;
        }
        auto region = *inner;
        for(auto& shadow : layer->m_drop_shadows) {
          auto cast = inflate(Rect(inner->m_x + shadow.m_offset.m_x,
            inner->m_y + shadow.m_offset.m_y, inner->m_width,
            inner->m_height), 3 * shadow.m_radius);
          region = Rellu::unite(region, cast);
        }
        if(layer->m_blur > 0) {
          region = inflate(region, 3 * layer->m_blur);
        }
        if(layer->m_mask) {
          region = intersect(region, get_bounds(
            *layer->m_mask->m_path, layer->m_mask->m_transform));
        }
        if(layer->m_clip) {
          region = intersect(region, *layer->m_clip);
        }
        bounds = unite(bounds, region);
      }
    }
    return bounds;
  }

  bool is_invisible(const Paint& paint) {
    if(auto solid = std::get_if<SolidPaint>(&paint)) {
      return solid->m_color.m_alpha <= 0;
    } else if(auto gradient = std::get_if<GradientPaint>(&paint)) {
      return std::all_of(gradient->m_stops.begin(), gradient->m_stops.end(),
        [] (const auto& stop) {
          return stop.m_color.m_alpha <= 0;
        });
    } else if(auto image = std::get_if<ImagePaint>(&paint)) {
      return image->m_opacity <= 0;
    }
    return false;
  }

  QImage to_straight(const QImage& image) {
    return image.convertToFormat(QImage::Format_RGBA8888);
  }

  struct ClipElement {
    std::optional<Rect> m_rect;
    std::shared_ptr<const Path> m_path;
    Matrix m_transform;

    bool operator ==(const ClipElement& element) const {
      return m_rect == element.m_rect && m_path == element.m_path &&
        m_transform == element.m_transform;
    }
  };

  struct Clip {
    std::vector<ClipElement> m_elements;

    bool is_open() const {
      return m_elements.empty();
    }

    bool is_rect() const {
      return std::all_of(m_elements.begin(), m_elements.end(),
        [] (const auto& element) {
          return element.m_rect.has_value();
        });
    }

    void add(const Rect& rect) {
      m_elements.push_back(ClipElement(rect, nullptr, Matrix()));
    }

    void add(std::shared_ptr<const Path> path, const Matrix& matrix) {
      if(auto rect = find_pdf_rect(transform(*path, matrix))) {
        add(*rect);
      } else {
        m_elements.push_back(ClipElement(std::nullopt, std::move(path),
          matrix));
      }
    }

    Rect get_bounds(const Rect& bounds) const {
      auto result = bounds;
      for(auto& element : m_elements) {
        if(element.m_rect) {
          result = intersect(result, *element.m_rect);
        } else {
          result = intersect(result,
            Rellu::get_bounds(*element.m_path, element.m_transform));
        }
      }
      return result;
    }

    bool operator ==(const Clip&) const = default;
  };

  struct StackEntry {
    Matrix m_matrix;
    Clip m_clip;
    Color m_color = Color(0, 0, 0, 1);
    int m_shader = -1;
    int m_state = -1;
  };

  class Device {
    public:
      Device(const Matrix& transform, const Matrix& initial, const Rect& bounds,
          bool is_page)
        : m_transform(transform),
          m_initial(initial),
          m_bounds(bounds),
          m_is_page(is_page),
          m_depth(0) {}

      const Matrix& get_transform() const {
        return m_transform;
      }

      const Matrix& get_initial() const {
        return m_initial;
      }

      const Rect& get_bounds() const {
        return m_bounds;
      }

      bool is_page() const {
        return m_is_page;
      }

      std::string& get_content() {
        return m_content;
      }

      void add_state(int id) {
        m_states.insert(id);
      }

      void add_pattern(int id) {
        m_patterns.insert(id);
      }

      void add_object(int id) {
        m_objects.insert(id);
      }

      void add_font(int id) {
        m_fonts.insert(id);
      }

      bool update_clip(const Clip& clip) {
        if(!clip.is_open() &&
            clip.get_bounds(inflate(m_bounds, 1)).is_empty()) {
          return false;
        }
        if(clip == current().m_clip) {
          return true;
        }
        while(m_depth > 0) {
          pop();
          if(clip == current().m_clip) {
            return true;
          }
        }
        if(clip.is_open()) {
          return true;
        }
        push();
        current().m_clip = clip;
        if(clip.is_rect()) {
          auto rect = clip.get_bounds(inflate(m_bounds, 1));
          m_content += to_pdf_path(make_rectangle(rect)) + "W* n\n";
          return true;
        }
        for(auto& element : clip.m_elements) {
          if(element.m_rect) {
            m_content += to_pdf_path(make_rectangle(*element.m_rect)) +
              "W n\n";
          } else {
            auto mapped = transform(*element.m_path, element.m_transform);
            m_content += to_pdf_path(mapped, 0.25, true);
            if(mapped.get_fill_rule() == FillRule::EVEN_ODD) {
              m_content += "W* n\n";
            } else {
              m_content += "W n\n";
            }
          }
        }
        return true;
      }

      void update_matrix(const Matrix& input) {
        auto matrix = to_float(input);
        if(matrix == current().m_matrix) {
          return;
        }
        if(!current().m_matrix.is_identity()) {
          pop();
        }
        if(matrix.is_identity()) {
          return;
        }
        push();
        m_content += to_pdf_values(matrix) + " cm\n";
        current().m_matrix = matrix;
      }

      void update_drawing(const Color& color, int shader, int state) {
        if(shader >= 0) {
          if(shader != current().m_shader) {
            m_content += std::format(
              "/Pattern CS/Pattern cs/P{} SCN/P{} scn\n", shader, shader);
            current().m_shader = shader;
          }
        } else if(!is_same(color, current().m_color) ||
            current().m_shader >= 0) {
          auto components = to_pdf_rgb(color);
          m_content += components + "RG " + components + "rg\n";
          current().m_color = color;
          current().m_shader = -1;
        }
        if(state != current().m_state) {
          m_content += std::format("/G{} gs\n", state);
          current().m_state = state;
          add_state(state);
        }
      }

      std::string get_stream() {
        while(m_depth > 0) {
          pop();
        }
        if(m_content.empty()) {
          return std::string();
        }
        auto stream = std::string();
        if(!m_initial.is_identity()) {
          stream = to_pdf_values(to_float(m_initial)) + " cm\n";
        }
        return stream + m_content;
      }

      std::string get_resources() const {
        auto resources = PdfDictionary();
        resources.add("ProcSet", PROCEDURES);
        auto add = [&] (const char* name, char prefix,
            const std::set<int>& ids) {
          if(ids.empty()) {
            return;
          }
          auto dictionary = PdfDictionary();
          for(auto id : ids) {
            dictionary.add(std::format("{}{}", prefix, id),
              to_pdf_reference(id));
          }
          resources.add(name, dictionary.to_string());
        };
        add("ExtGState", 'G', m_states);
        add("Pattern", 'P', m_patterns);
        add("XObject", 'X', m_objects);
        add("Font", 'F', m_fonts);
        return resources.to_string();
      }

    private:
      Matrix m_transform;
      Matrix m_initial;
      Rect m_bounds;
      bool m_is_page;
      std::string m_content;
      std::set<int> m_states;
      std::set<int> m_patterns;
      std::set<int> m_objects;
      std::set<int> m_fonts;
      std::array<StackEntry, 3> m_entries;
      int m_depth;

      static bool is_same(const Color& left, const Color& right) {
        return to_float(left.m_red) == to_float(right.m_red) &&
          to_float(left.m_green) == to_float(right.m_green) &&
          to_float(left.m_blue) == to_float(right.m_blue);
      }

      StackEntry& current() {
        return m_entries[static_cast<std::size_t>(m_depth)];
      }

      void push() {
        m_content += "q\n";
        ++m_depth;
        m_entries[static_cast<std::size_t>(m_depth)] =
          m_entries[static_cast<std::size_t>(m_depth - 1)];
      }

      void pop() {
        m_content += "Q\n";
        m_entries[static_cast<std::size_t>(m_depth)] = StackEntry();
        --m_depth;
      }
  };

  class GlyphPositioner {
    public:
      explicit GlyphPositioner(std::string& content)
        : m_content(&content),
          m_origin(0, 0),
          m_advance(0),
          m_is_initialized(false),
          m_is_in_text(false),
          m_is_agreed(true),
          m_is_font_agreed(true),
          m_is_multibyte(true) {}

      ~GlyphPositioner() {
        flush();
      }

      void flush() {
        if(m_is_in_text) {
          *m_content += "> Tj\n";
          m_is_in_text = false;
        }
      }

      void set_font(bool is_multibyte, bool is_agreed) {
        flush();
        m_is_multibyte = is_multibyte;
        m_is_font_agreed = is_agreed;
      }

      void write(std::uint16_t code, float advance, float x, float y) {
        if(!m_is_initialized) {
          *m_content += "1 0 0 -1 0 0 Tm\n";
          m_origin = std::pair(0.0f, 0.0f);
          m_is_initialized = true;
        }
        auto dx = x - m_origin.first;
        auto dy = y - m_origin.second;
        if(!m_is_agreed || dx != m_advance || dy != 0) {
          flush();
          *m_content +=
            to_pdf_number(dx) + ' ' + to_pdf_number(-dy) + " Td ";
          m_origin = std::pair(x, y);
          m_advance = 0;
          m_is_agreed = m_is_font_agreed;
        }
        m_advance += advance;
        if(!m_is_in_text) {
          *m_content += '<';
          m_is_in_text = true;
        }
        if(m_is_multibyte) {
          *m_content += to_pdf_hex(code);
        } else {
          *m_content += std::format("{:02X}", code);
        }
      }

    private:
      std::string* m_content;
      std::pair<float, float> m_origin;
      float m_advance;
      bool m_is_initialized;
      bool m_is_in_text;
      bool m_is_agreed;
      bool m_is_font_agreed;
      bool m_is_multibyte;
  };

  struct Page {
    int m_id;
    int m_contents;
    std::string m_resources;
    Size m_size;
  };

  class Writer {
    public:
      explicit Writer(const Document& document)
        : m_document(&document),
          m_builder(document, TOLERANCE),
          m_fonts(m_file),
          m_info(m_file.reserve()) {
        auto info = PdfDictionary();
        info.add("Producer", std::format("({})", PRODUCER));
        m_file.write(m_info, info.to_string());
      }

      void add_page(const NodeId& id) {
        auto node = m_document->find(id);
        if(!node) {
          throw std::runtime_error("The exported node does not exist.");
        }
        auto page = m_file.reserve();
        auto list = DrawList();
        auto region = Rect(0, 0, 0, 0);
        if(auto artboard = node->get_artboard()) {
          list = m_builder.build(id);
          auto origin = get_world_transform(*m_document, id).map(Point(0, 0));
          region = Rect(origin.m_x, origin.m_y, artboard->m_frame.m_width,
            artboard->m_frame.m_height);
          if(!list.empty()) {
            if(auto background = std::get_if<FillCommand>(&list.front());
                background && *background->m_path == make_rectangle(Rect(
                  0, 0, region.m_width, region.m_height)) &&
                background->m_transform ==
                  make_translation(origin.m_x, origin.m_y)) {
              background->m_clip = std::nullopt;
            }
          }
        } else {
          list = m_builder.build_unclipped(id);
          auto ends = match_layers(list);
          region = get_list_bounds(list, ends, 0, list.size()).value_or(
            Rect(0, 0, 0, 0));
        }
        auto width = std::ceil(to_float(region.m_width * RASTER_SCALE));
        auto height = std::ceil(to_float(region.m_height * RASTER_SCALE));
        auto size = Size(to_float(width / RASTER_SCALE),
          to_float(height / RASTER_SCALE));
        auto device = Device(make_scale(RASTER_SCALE, RASTER_SCALE) *
          make_translation(-region.m_x, -region.m_y), Matrix(1 / RASTER_SCALE,
            0, 0, -1 / RASTER_SCALE, 0, height / RASTER_SCALE),
          Rect(0, 0, width, height), true);
        m_list = &list;
        m_ends = match_layers(list);
        draw(device, 0, list.size());
        m_list = nullptr;
        auto contents = m_file.reserve();
        m_file.write_stream(contents, PdfDictionary(), device.get_stream());
        m_pages.push_back(Page(page, contents, device.get_resources(), size));
      }

      QByteArray finish() {
        if(m_pages.empty()) {
          throw std::runtime_error("There are no pages to export.");
        }
        struct TreeNode {
          PdfDictionary m_dictionary;
          int m_id;
          int m_count;
        };
        auto layer = std::vector<TreeNode>();
        for(auto i = std::size_t(0); i != m_pages.size(); ++i) {
          auto& page = m_pages[i];
          auto dictionary = PdfDictionary("Page");
          dictionary.add("Resources", page.m_resources);
          dictionary.add("MediaBox", to_pdf_array({"0", "0",
            to_pdf_number(page.m_size.m_width),
            to_pdf_number(page.m_size.m_height)}));
          dictionary.add("Contents", to_pdf_reference(page.m_contents));
          dictionary.add("StructParents", std::to_string(i));
          layer.push_back(TreeNode(std::move(dictionary), page.m_id, 1));
        }
        auto first = true;
        while(first || layer.size() > 1) {
          first = false;
          auto next = std::vector<TreeNode>();
          auto count = layer.size();
          auto index = std::size_t(0);
          auto result_size = (count - 1) / MAXIMUM_TREE_NODE + 1;
          for(auto i = std::size_t(0); i != result_size; ++i) {
            if(count != 1 && index + 1 == count) {
              next.push_back(std::move(layer[index++]));
              continue;
            }
            auto parent = m_file.reserve();
            auto kids = std::vector<std::string>();
            auto descendants = 0;
            for(auto j = std::size_t(0);
                j != MAXIMUM_TREE_NODE && index < count; ++j) {
              auto& node = layer[index++];
              node.m_dictionary.add("Parent", to_pdf_reference(parent));
              m_file.write(node.m_id, node.m_dictionary.to_string());
              kids.push_back(to_pdf_reference(node.m_id));
              descendants += node.m_count;
            }
            auto pages = PdfDictionary("Pages");
            pages.add("Count", std::to_string(descendants));
            pages.add("Kids", to_pdf_array(kids));
            next.push_back(TreeNode(std::move(pages), parent, descendants));
          }
          layer = std::move(next);
        }
        auto& root = layer.front();
        m_file.write(root.m_id, root.m_dictionary.to_string());
        auto catalog = PdfDictionary("Catalog");
        catalog.add("Pages", to_pdf_reference(root.m_id));
        auto catalog_id = m_file.reserve();
        m_file.write(catalog_id, catalog.to_string());
        m_fonts.finish();
        return m_file.finish(catalog_id, m_info);
      }

    private:
      const Document* m_document;
      SceneBuilder m_builder;
      PdfFile m_file;
      PdfFonts m_fonts;
      int m_info;
      std::vector<Page> m_pages;
      const DrawList* m_list = nullptr;
      std::vector<std::size_t> m_ends;
      std::map<std::string, int> m_states;
      std::map<std::string, int> m_shaders;
      std::unordered_map<qint64, int> m_images;
      std::unique_ptr<CanvasRenderer> m_renderer;
      std::unique_ptr<OffscreenRenderer> m_offscreen;
      std::optional<Rect> m_run;

      int get_state(PdfDictionary dictionary) {
        auto key = dictionary.to_string();
        if(auto state = m_states.find(key); state != m_states.end()) {
          return state->second;
        }
        auto id = m_file.reserve();
        m_file.write(id, key);
        m_states.emplace(std::move(key), id);
        return id;
      }

      int get_fill_state(double alpha, BlendMode mode) {
        auto state = PdfDictionary();
        state.add("ca", to_pdf_color(alpha));
        state.add("BM", to_pdf_blend(mode));
        return get_state(std::move(state));
      }

      int get_stroke_state(const StrokeLine& line, double alpha) {
        auto state = PdfDictionary();
        state.add("CA", to_pdf_color(alpha));
        state.add("ca", to_pdf_color(alpha));
        state.add("LC", std::to_string(to_pdf_cap(line.m_cap)));
        state.add("LJ", std::to_string(to_pdf_join(line.m_join)));
        state.add("LW", to_pdf_number(line.m_width));
        state.add("ML", to_pdf_number(line.m_miter_limit));
        state.add("SA", "true");
        state.add("BM", "/Normal");
        return get_state(std::move(state));
      }

      Clip make_clip(const Device& device, const std::optional<Rect>& clip) {
        auto result = Clip();
        if(clip && !(m_run && *clip == *m_run)) {
          auto rect = device.get_transform().map(*clip);
          if(device.is_page() || !rect.contains(device.get_bounds())) {
            result.add(rect);
          }
        }
        return result;
      }

      Rect get_clip_bounds(const Device& device, const Clip& clip) {
        return round_out(map_bounds(
          device.get_initial(), clip.get_bounds(device.get_bounds())));
      }

      const std::optional<Rect>& get_clip(std::size_t index) const {
        static const auto NONE = std::optional<Rect>();
        auto& command = (*m_list)[index];
        if(auto fill = std::get_if<FillCommand>(&command)) {
          return fill->m_clip;
        } else if(auto blur = std::get_if<BackdropBlurCommand>(&command)) {
          return blur->m_clip;
        } else if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
          return layer->m_clip;
        }
        return NONE;
      }

      bool is_run(const Device& device, const std::optional<Rect>& clip) {
        if(!clip || (m_run && *clip == *m_run)) {
          return false;
        }
        return !device.get_transform().map(*clip).contains(device.get_bounds());
      }

      void draw(Device& device, std::size_t begin, std::size_t end) {
        auto& list = *m_list;
        for(auto i = begin; i < end; ++i) {
          auto& command = list[i];
          if(is_run(device, get_clip(i))) {
            auto clip = *get_clip(i);
            auto last = i;
            while(last < end && get_clip(last) == clip) {
              if(std::holds_alternative<BeginLayerCommand>(list[last])) {
                last = std::min(m_ends[last], end);
              }
              ++last;
            }
            draw_run(device, i, last, clip);
            i = last - 1;
          } else if(auto fill = std::get_if<FillCommand>(&command)) {
            draw_fill(device, *fill, i);
          } else if(auto blur = std::get_if<BackdropBlurCommand>(&command)) {
            auto last = i + 1;
            while(last < end) {
              auto next = std::get_if<FillCommand>(&list[last]);
              if(!next || next->m_path != blur->m_path ||
                  !(next->m_transform == blur->m_transform)) {
                break;
              }
              ++last;
            }
            draw_backdrop(device, *blur, last);
            i = last - 1;
          } else if(std::holds_alternative<BeginLayerCommand>(command)) {
            auto last = std::min(m_ends[i], end);
            draw_layer(device, i, last);
            i = last;
          }
        }
      }

      void draw_run(Device& device, std::size_t begin, std::size_t end,
          const Rect& rect) {
        auto clip = make_clip(device, rect);
        auto area = round_out(clip.get_bounds(device.get_bounds()));
        if(area.is_empty()) {
          return;
        }
        auto form = Device(make_translation(-area.m_x, -area.m_y) *
          device.get_transform(), Matrix(),
          Rect(0, 0, area.m_width, area.m_height), false);
        auto previous = std::exchange(m_run, rect);
        draw(form, begin, end);
        m_run = previous;
        draw_form(device, form, clip, area,
          get_fill_state(1, BlendMode::NORMAL));
      }

      void draw_form(Device& device, Device& form, const Clip& clip,
          const Rect& area, int state) {
        auto stream = form.get_stream();
        if(stream.empty()) {
          return;
        }
        auto dictionary = PdfDictionary("XObject");
        dictionary.add("Subtype", "/Form");
        dictionary.add("Resources", form.get_resources());
        dictionary.add("BBox", to_pdf_array({"0", "0",
          to_pdf_number(area.m_width), to_pdf_number(area.m_height)}));
        dictionary.add(
          "Group", "<</Type /Group\n/S /Transparency\n/I true>>");
        auto id = m_file.reserve();
        m_file.write_stream(id, std::move(dictionary), std::move(stream));
        if(!device.update_clip(clip)) {
          return;
        }
        device.update_matrix(make_translation(area.m_x, area.m_y));
        device.update_drawing(Color(0, 0, 0, 1), -1, state);
        device.add_object(id);
        device.get_content() += std::format("/X{} Do\n", id);
      }

      void draw_fill(Device& device, const FillCommand& fill,
          std::size_t index) {
        if(is_invisible(fill.m_paint)) {
          return;
        }
        if(auto solid = std::get_if<SolidPaint>(&fill.m_paint)) {
          if(auto region = m_builder.find_text(*fill.m_path)) {
            if(draw_text(device, fill, *region, solid->m_color)) {
              return;
            }
          }
        }
        if(auto gradient = std::get_if<GradientPaint>(&fill.m_paint);
            gradient && (gradient->m_projection ||
              std::holds_alternative<AngularGradient>(
                gradient->m_geometry))) {
          draw_raster(device, index, index + 1, fill.m_clip, BLUR_SCALE);
          return;
        }
        if(auto image = std::get_if<ImagePaint>(&fill.m_paint);
            image && image->m_projection) {
          draw_raster(device, index, index + 1, fill.m_clip, BLUR_SCALE);
          return;
        }
        auto clip = make_clip(device, fill.m_clip);
        if(!device.update_clip(clip)) {
          return;
        }
        auto ctm = device.get_transform() * fill.m_transform;
        auto is_stroke = fill.m_stroke.has_value();
        auto color = Color(0, 0, 0, 1);
        auto alpha = 1.0;
        auto shader = -1;
        if(auto solid = std::get_if<SolidPaint>(&fill.m_paint)) {
          color = solid->m_color;
          alpha = color.m_alpha;
        } else if(auto gradient = std::get_if<GradientPaint>(&fill.m_paint)) {
          shader = get_gradient(
            device, *gradient, ctm, get_clip_bounds(device, clip));
        } else if(auto image = std::get_if<ImagePaint>(&fill.m_paint)) {
          alpha = image->m_opacity;
          auto source = *image->m_image;
          auto transform = image->m_transform;
          if(m_run) {
            crop(source, transform, fill.m_transform, *m_run);
          }
          shader = get_image_shader(device, source,
            device.get_initial() * ctm * transform,
            get_clip_bounds(device, clip));
        }
        if(shader >= 0) {
          device.add_pattern(shader);
        } else if(!std::holds_alternative<SolidPaint>(fill.m_paint)) {
          return;
        }
        device.update_matrix(ctm);
        auto state = is_stroke ? get_stroke_state(*fill.m_stroke, alpha) :
          get_fill_state(alpha, BlendMode::NORMAL);
        device.update_drawing(color, shader, state);
        auto scale = std::sqrt(std::abs(to_float(ctm).get_determinant()));
        auto tolerance = scale > 0 ? 0.25 / (scale * scale) : 0.25;
        auto& content = device.get_content();
        if(is_stroke) {
          content += to_pdf_path(*fill.m_stroke->m_path, tolerance);
          content += "S\n";
        } else {
          content += to_pdf_path(*fill.m_path, tolerance);
          content += get_pdf_fill(*fill.m_path);
        }
      }

      bool draw_text(Device& device, const FillCommand& fill,
          const TextRegion& region, const Color& color) {
        auto& layout = *region.m_layout;
        auto is_drawn = [&] (const PlacedGlyph& glyph) {
          return glyph.m_color == region.m_color;
        };
        for(auto& glyph : layout.m_glyphs) {
          if(is_drawn(glyph) &&
              !glyph.m_typeface->get_color_layers(glyph.m_index).empty()) {
            return false;
          }
        }
        auto clip = make_clip(device, fill.m_clip);
        if(!device.update_clip(clip)) {
          return true;
        }
        device.update_matrix(device.get_transform() * fill.m_transform);
        device.update_drawing(
          color, -1, get_fill_state(color.m_alpha, BlendMode::NORMAL));
        auto& content = device.get_content();
        auto glyphs = std::vector<PlacedGlyph>();
        auto blank = layout.m_blanks.begin();
        for(auto j = std::size_t(0); j <= layout.m_glyphs.size(); ++j) {
          while(blank != layout.m_blanks.end() && blank->m_position == j) {
            glyphs.push_back(blank->m_glyph);
            ++blank;
          }
          if(j != layout.m_glyphs.size()) {
            glyphs.push_back(layout.m_glyphs[j]);
          }
        }
        auto i = std::size_t(0);
        while(i != glyphs.size()) {
          if(!is_drawn(glyphs[i])) {
            ++i;
            continue;
          }
          auto& first = glyphs[i];
          content += "BT\n";
          {
            auto positioner = GlyphPositioner(content);
            auto font = -1;
            while(i != glyphs.size() && (!is_drawn(glyphs[i]) ||
                (glyphs[i].m_origin.m_y == first.m_origin.m_y &&
                  glyphs[i].m_size == first.m_size &&
                  glyphs[i].m_typeface == first.m_typeface))) {
              auto& glyph = glyphs[i];
              ++i;
              if(!is_drawn(glyph)) {
                continue;
              }
              auto encoded = m_fonts.use(glyph.m_typeface, glyph.m_index);
              if(!encoded) {
                continue;
              }
              if(encoded->m_font != font) {
                font = encoded->m_font;
                positioner.set_font(
                  encoded->m_is_multibyte, encoded->m_is_agreed);
                device.add_font(font);
                content += std::format("/F{} {} Tf\n", font,
                  to_pdf_number(glyph.m_size));
              }
              positioner.write(encoded->m_code,
                static_cast<float>(glyph.m_size) *
                  static_cast<float>(encoded->m_advance),
                static_cast<float>(glyph.m_origin.m_x),
                static_cast<float>(glyph.m_origin.m_y));
            }
          }
          content += "ET\n";
        }
        for(auto& decoration : layout.m_decorations) {
          if(decoration.m_color == region.m_color) {
            content += to_pdf_path(make_rectangle(decoration.m_rect));
            content += "f\n";
          }
        }
        return true;
      }

      void draw_layer(Device& device, std::size_t begin, std::size_t end) {
        auto& layer = std::get<BeginLayerCommand>((*m_list)[begin]);
        if(layer.m_blur > 0) {
          draw_blur(device, begin, end, layer);
          return;
        }
        auto is_form = layer.m_opacity < 1 ||
          layer.m_blend_mode != BlendMode::NORMAL || layer.m_mask;
        if(!is_form) {
          draw_effects(device, begin, end);
          return;
        }
        auto clip = make_clip(device, layer.m_clip);
        if(layer.m_mask) {
          clip.add(layer.m_mask->m_path,
            device.get_transform() * layer.m_mask->m_transform);
        }
        auto area = round_out(clip.get_bounds(device.get_bounds()));
        if(area.is_empty()) {
          return;
        }
        auto form = Device(make_translation(-area.m_x, -area.m_y) *
          device.get_transform(), Matrix(),
          Rect(0, 0, area.m_width, area.m_height), false);
        draw_effects(form, begin, end);
        auto opacity =
          std::lround(std::clamp(layer.m_opacity, 0.0, 1.0) * 255) / 255.0;
        draw_form(device, form, clip, area,
          get_fill_state(opacity, layer.m_blend_mode));
      }

      void draw_effects(Device& device, std::size_t begin, std::size_t end) {
        auto& layer = std::get<BeginLayerCommand>((*m_list)[begin]);
        for(auto& shadow : layer.m_drop_shadows) {
          draw_drop_shadow(device, begin + 1, end, shadow, layer.m_clip);
        }
        draw(device, begin + 1, end);
        for(auto& shadow : layer.m_inner_shadows) {
          draw_inner_shadow(device, begin + 1, end, shadow, layer.m_clip);
        }
      }

      DrawList slice(std::size_t begin, std::size_t end) const {
        return DrawList(m_list->begin() + static_cast<std::ptrdiff_t>(begin),
          m_list->begin() + static_cast<std::ptrdiff_t>(end));
      }

      DrawList blur_slice(std::size_t begin, std::size_t end, double radius) {
        auto list = DrawList();
        auto layer = BeginLayerCommand();
        layer.m_blur = radius;
        list.push_back(layer);
        auto content = slice(begin, end);
        list.insert(list.end(), content.begin(), content.end());
        list.push_back(EndLayerCommand());
        return list;
      }

      Rect to_effect_region(
          const Device& device, const Rect& region, double scale) {
        auto area =
          snap(device.get_transform().map(region), RASTER_SCALE / scale);
        auto inverse = invert(device.get_transform()).value_or(Matrix());
        return inverse.map(area);
      }

      QImage render(const DrawList& list, const Rect& region, double scale) {
        if(!m_renderer) {
          m_renderer = std::make_unique<CanvasRenderer>();
          m_renderer->set_background(QColor(0, 0, 0, 0));
          m_offscreen = std::make_unique<OffscreenRenderer>(*m_renderer);
        }
        auto size = QSize(
          std::max(1, static_cast<int>(std::lround(region.m_width * scale))),
          std::max(1, static_cast<int>(std::lround(region.m_height * scale))));
        m_renderer->set_draw_list(std::make_shared<DrawList>(list));
        m_renderer->set_view(make_translation(-region.m_x, -region.m_y));
        auto image = m_offscreen->render(size, scale);
        image.setDevicePixelRatio(1);
        return image;
      }

      QImage colorize(const QImage& coverage, const Color& color) {
        auto image = QImage(coverage.size(), QImage::Format_RGBA8888);
        auto red = static_cast<uchar>(std::lround(color.m_red * 255));
        auto green = static_cast<uchar>(std::lround(color.m_green * 255));
        auto blue = static_cast<uchar>(std::lround(color.m_blue * 255));
        for(auto y = 0; y != image.height(); ++y) {
          auto input = coverage.constScanLine(y);
          auto output = image.scanLine(y);
          for(auto x = 0; x != image.width(); ++x) {
            output[4 * x] = red;
            output[4 * x + 1] = green;
            output[4 * x + 2] = blue;
            output[4 * x + 3] = static_cast<uchar>(
              std::lround(input[4 * x + 3] * color.m_alpha));
          }
        }
        return image;
      }

      static bool is_visible(const QImage& image) {
        for(auto y = 0; y != image.height(); ++y) {
          auto line = image.constScanLine(y);
          for(auto x = 0; x != image.width(); ++x) {
            if(line[4 * x + 3] != 0) {
              return true;
            }
          }
        }
        return false;
      }

      void draw_drop_shadow(Device& device, std::size_t begin,
          std::size_t end, const Shadow& shadow,
          const std::optional<Rect>& clip) {
        auto bounds = get_list_bounds(*m_list, m_ends, begin, end);
        if(!bounds || shadow.m_color.m_alpha <= 0) {
          return;
        }
        auto blur = 2 * shadow.m_radius;
        auto scale = blur >= LARGE_SHADOW_BLUR ? BLUR_SCALE : SHADOW_SCALE;
        auto shift = 1 - blur / 2;
        auto region = inflate(Rect(bounds->m_x + shadow.m_offset.m_x + shift,
          bounds->m_y + shadow.m_offset.m_y + shift, bounds->m_width,
          bounds->m_height), 3 * blur + 1);
        auto source = Rect(region.m_x - shadow.m_offset.m_x,
          region.m_y - shadow.m_offset.m_y, region.m_width, region.m_height);
        auto image = colorize(render(blur_slice(begin, end, shadow.m_radius),
          source, scale), shadow.m_color);
        if(is_visible(image)) {
          draw_pattern_image(device, image, region.get_top_left(), scale,
            make_clip(device, clip));
        }
      }

      void draw_inner_shadow(Device& device, std::size_t begin,
          std::size_t end, const Shadow& shadow,
          const std::optional<Rect>& clip) {
        auto bounds = get_list_bounds(*m_list, m_ends, begin, end);
        if(!bounds || shadow.m_color.m_alpha <= 0) {
          return;
        }
        auto region = Rellu::unite(*bounds, Rect(
          bounds->m_x + shadow.m_offset.m_x, bounds->m_y + shadow.m_offset.m_y,
          bounds->m_width, bounds->m_height));
        auto content = render(slice(begin, end), region, SHADOW_SCALE);
        auto source = Rect(region.m_x - shadow.m_offset.m_x,
          region.m_y - shadow.m_offset.m_y, region.m_width, region.m_height);
        auto blurred = render(
          blur_slice(begin, end, shadow.m_radius), source, SHADOW_SCALE);
        for(auto y = 0; y != content.height(); ++y) {
          auto inside = content.scanLine(y);
          auto outside = blurred.constScanLine(y);
          for(auto x = 0; x != content.width(); ++x) {
            inside[4 * x + 3] = static_cast<uchar>(std::lround(
              inside[4 * x + 3] * (255 - outside[4 * x + 3]) / 255.0));
          }
        }
        auto image = colorize(content, shadow.m_color);
        if(!is_visible(image)) {
          return;
        }
        auto shape = make_clip(device, clip);
        if(end == begin + 1) {
          if(auto fill = std::get_if<FillCommand>(&(*m_list)[begin])) {
            shape.add(fill->m_path,
              device.get_transform() * fill->m_transform);
          }
        } else {
          shape.add(device.get_transform().map(*bounds));
        }
        draw_pattern_image(
          device, image, region.get_top_left(), SHADOW_SCALE, shape);
      }

      void draw_blur(Device& device, std::size_t begin, std::size_t end,
          const BeginLayerCommand& layer) {
        auto bounds = get_list_bounds(*m_list, m_ends, begin + 1, end);
        if(!bounds) {
          return;
        }
        auto amount = layer.m_blur;
        auto shift = 1 - 3 * amount;
        auto region = inflate(Rect(bounds->m_x + shift, bounds->m_y + shift,
          bounds->m_width, bounds->m_height), 6 * amount + 1);
        auto image =
          to_straight(render(slice(begin, end + 1), region, BLUR_SCALE));
        draw_pattern_image(device, image, region.get_top_left(), BLUR_SCALE,
          make_clip(device, layer.m_clip));
      }

      void draw_raster(Device& device, std::size_t begin, std::size_t end,
          const std::optional<Rect>& clip, double scale) {
        auto bounds = get_list_bounds(*m_list, m_ends, begin, end);
        if(!bounds) {
          return;
        }
        auto region = to_effect_region(device, *bounds, scale);
        auto image = to_straight(render(slice(begin, end), region, scale));
        draw_pattern_image(device, image, region.get_top_left(), scale,
          make_clip(device, clip));
      }

      void draw_backdrop(Device& device, const BackdropBlurCommand& blur,
          std::size_t end) {
        auto bounds = get_bounds(*blur.m_path, blur.m_transform);
        if(blur.m_clip) {
          bounds = intersect(bounds, *blur.m_clip);
        }
        if(bounds.is_empty()) {
          return;
        }
        auto region = bounds;
        auto list = slice(0, end);
        auto open = 0;
        for(auto& command : list) {
          if(std::holds_alternative<BeginLayerCommand>(command)) {
            ++open;
          } else if(std::holds_alternative<EndLayerCommand>(command)) {
            --open;
          }
        }
        for(auto i = 0; i < open; ++i) {
          list.push_back(EndLayerCommand());
        }
        auto image =
          to_straight(render(list, region, BACKDROP_SCALE));
        auto clip = make_clip(device, blur.m_clip);
        clip.add(blur.m_path, device.get_transform() * blur.m_transform);
        if(!device.update_clip(clip)) {
          return;
        }
        auto id = write_image(image);
        device.update_matrix(device.get_transform() *
          make_translation(region.m_x, region.m_y) *
          make_scale(region.m_width, region.m_height) *
          Matrix(1, 0, 0, -1, 0, 1));
        device.update_drawing(
          Color(0, 0, 0, 1), -1, get_fill_state(1, BlendMode::NORMAL));
        device.add_object(id);
        device.get_content() += std::format("/X{} Do\n", id);
      }

      void crop(QImage& image, Matrix& transform, const Matrix& placement,
          const Rect& clip) {
        auto inverse = invert(placement * transform);
        if(!inverse) {
          return;
        }
        auto visible = map_bounds(*inverse, clip);
        auto left = std::max(0, static_cast<int>(std::floor(visible.m_x)) - 1);
        auto top = std::max(0, static_cast<int>(std::floor(visible.m_y)) - 1);
        auto right = std::min(image.width(),
          static_cast<int>(std::ceil(visible.get_right())) + 1);
        auto bottom = std::min(image.height(),
          static_cast<int>(std::ceil(visible.get_bottom())) + 1);
        if(right <= left || bottom <= top ||
            (left == 0 && top == 0 && right == image.width() &&
              bottom == image.height())) {
          return;
        }
        image = image.copy(left, top, right - left, bottom - top);
        transform = transform * make_translation(left, top);
      }

      void draw_pattern_image(Device& device, const QImage& image,
          Point origin, double scale, const Clip& clip) {
        if(!device.update_clip(clip)) {
          return;
        }
        auto matrix = device.get_transform() *
          make_translation(origin.m_x, origin.m_y) *
          make_scale(1 / scale, 1 / scale);
        auto state = get_fill_state(1, BlendMode::NORMAL);
        auto shader = get_image_shader(device, image,
          device.get_initial() * matrix, get_clip_bounds(device, clip));
        device.add_pattern(shader);
        device.update_matrix(matrix);
        device.update_drawing(Color(0, 0, 0, 1), shader, state);
        device.get_content() += std::format("0 0 {} {} re\nf\n",
          image.width(), image.height());
      }

      int write_image(const QImage& source) {
        auto image = to_straight(source);
        auto key = source.cacheKey();
        if(auto cached = m_images.find(key); cached != m_images.end()) {
          return cached->second;
        }
        auto width = image.width();
        auto height = image.height();
        auto is_opaque = true;
        for(auto y = 0; y != height && is_opaque; ++y) {
          auto line = image.constScanLine(y);
          for(auto x = 0; x != width; ++x) {
            if(line[4 * x + 3] != 255) {
              is_opaque = false;
              break;
            }
          }
        }
        auto id = m_file.reserve();
        if(is_opaque) {
          auto bytes = QByteArray();
          auto buffer = QBuffer(&bytes);
          buffer.open(QIODevice::WriteOnly);
          auto writer = QImageWriter(&buffer, "jpeg");
          writer.setQuality(100);
          if(writer.write(image.convertToFormat(QImage::Format_RGB888))) {
            auto dictionary = PdfDictionary("XObject");
            dictionary.add("Subtype", "/Image");
            dictionary.add("Width", std::to_string(width));
            dictionary.add("Height", std::to_string(height));
            dictionary.add("ColorSpace", "/DeviceRGB");
            dictionary.add("BitsPerComponent", "8");
            dictionary.add("Filter", "/DCTDecode");
            dictionary.add("ColorTransform", "0");
            m_file.write_raw_stream(id, std::move(dictionary),
              std::string_view(bytes.constData(),
                static_cast<std::size_t>(bytes.size())));
            m_images.emplace(key, id);
            return id;
          }
        }
        auto colors = std::string();
        colors.reserve(static_cast<std::size_t>(3 * width * height));
        auto alphas = std::string();
        alphas.reserve(static_cast<std::size_t>(width * height));
        for(auto y = 0; y != height; ++y) {
          auto line = image.constScanLine(y);
          for(auto x = 0; x != width; ++x) {
            auto pixel = line + 4 * x;
            auto red = static_cast<unsigned int>(pixel[0]);
            auto green = static_cast<unsigned int>(pixel[1]);
            auto blue = static_cast<unsigned int>(pixel[2]);
            if(pixel[3] == 0 && red == 0 && green == 0 && blue == 0) {
              auto count = 0u;
              auto sums = std::array<unsigned int, 3>({0, 0, 0});
              for(auto j = std::max(0, y - 1);
                  j <= std::min(y + 1, height - 1); ++j) {
                auto row = image.constScanLine(j);
                for(auto i = std::max(0, x - 1);
                    i <= std::min(x + 1, width - 1); ++i) {
                  auto other = row + 4 * i;
                  if(other[0] || other[1] || other[2] || other[3]) {
                    sums[0] += other[0];
                    sums[1] += other[1];
                    sums[2] += other[2];
                    ++count;
                  }
                }
              }
              if(count != 0) {
                red = sums[0] / count;
                green = sums[1] / count;
                blue = sums[2] / count;
              }
            }
            colors.push_back(static_cast<char>(red));
            colors.push_back(static_cast<char>(green));
            colors.push_back(static_cast<char>(blue));
            alphas.push_back(static_cast<char>(pixel[3]));
          }
        }
        auto mask = is_opaque ? -1 : m_file.reserve();
        auto dictionary = PdfDictionary("XObject");
        dictionary.add("Subtype", "/Image");
        dictionary.add("Width", std::to_string(width));
        dictionary.add("Height", std::to_string(height));
        dictionary.add("ColorSpace", "/DeviceRGB");
        if(!is_opaque) {
          dictionary.add("SMask", to_pdf_reference(mask));
        }
        dictionary.add("BitsPerComponent", "8");
        dictionary.add("Filter", "/FlateDecode");
        m_file.write_raw_stream(id, std::move(dictionary), deflate(colors));
        if(!is_opaque) {
          auto alpha = PdfDictionary("XObject");
          alpha.add("Subtype", "/Image");
          alpha.add("Width", std::to_string(width));
          alpha.add("Height", std::to_string(height));
          alpha.add("ColorSpace", "/DeviceGray");
          alpha.add("BitsPerComponent", "8");
          alpha.add("Filter", "/FlateDecode");
          m_file.write_raw_stream(mask, std::move(alpha), deflate(alphas));
        }
        m_images.emplace(key, id);
        return id;
      }

      int get_image_shader(Device& device, const QImage& source,
          const Matrix& matrix, const Rect& box) {
        auto final_matrix = to_float(matrix);
        auto key = std::format("image {} {} {}", source.cacheKey(),
          to_pdf_matrix(final_matrix), to_pdf_rect(box));
        if(auto shader = m_shaders.find(key); shader != m_shaders.end()) {
          return shader->second;
        }
        auto image = to_straight(source);
        auto inverse = invert(final_matrix);
        if(!inverse) {
          return -1;
        }
        auto bounds = map_bounds(*inverse, box);
        bounds = Rect(to_float(bounds.m_x), to_float(bounds.m_y),
          to_float(bounds.m_width), to_float(bounds.m_height));
        auto width = static_cast<double>(image.width());
        auto height = static_cast<double>(image.height());
        auto size = Rect(0, 0, std::ceil(bounds.m_width),
          std::ceil(bounds.m_height));
        auto left = bounds.get_left();
        auto top = bounds.get_top();
        auto right = bounds.get_right();
        auto bottom = bounds.get_bottom();
        auto canvas = Device(make_translation(-left, -top), Matrix(), size,
          false);
        auto pattern_box = Rect(-left, -top, width, height);
        auto image_id = write_image(source);
        auto draw_bitmap = [&] (int id, const Matrix& transform) {
          canvas.update_clip(Clip());
          canvas.update_matrix(make_translation(-left, -top) * transform *
            Matrix(1, 0, 0, -1, 0, 1));
          canvas.update_drawing(
            Color(0, 0, 0, 1), -1, get_fill_state(1, BlendMode::NORMAL));
          canvas.add_object(id);
          canvas.get_content() += std::format("/X{} Do\n", id);
        };
        if(Rect(0, 0, width, height).intersects(bounds)) {
          draw_bitmap(image_id, make_scale(width, height));
        }
        auto pixel = [&] (int x, int y) {
          auto line = image.constScanLine(y);
          auto data = line + 4 * x;
          return Color(data[0] / 255.0, data[1] / 255.0, data[2] / 255.0,
            data[3] / 255.0);
        };
        auto corner = [&] (double x0, double y0, double x1, double y1,
            int x, int y) {
          auto rect = Rect(x0, y0, x1 - x0, y1 - y0);
          auto color = pixel(x, y);
          if(rect.is_empty() || color.m_alpha <= 0) {
            return;
          }
          canvas.update_matrix(make_translation(-left, -top));
          canvas.update_drawing(
            color, -1, get_fill_state(color.m_alpha, BlendMode::NORMAL));
          canvas.get_content() += to_pdf_path(make_rectangle(rect)) + "f\n";
        };
        auto last_x = image.width() - 1;
        auto last_y = image.height() - 1;
        corner(left, top, 0, 0, 0, 0);
        corner(width, top, right, 0, last_x, 0);
        corner(width, height, right, bottom, last_x, last_y);
        corner(left, height, 0, bottom, 0, last_y);
        if(left < 0) {
          draw_bitmap(write_image(image.copy(0, 0, 1, image.height())),
            Matrix(-left, 0, 0, height, left, 0));
          pattern_box = Rect(0, pattern_box.m_y,
            pattern_box.get_right(), pattern_box.m_height);
        }
        if(right > width) {
          draw_bitmap(write_image(image.copy(last_x, 0, 1, image.height())),
            Matrix(right - width, 0, 0, height, width, 0));
          pattern_box.m_width = bounds.m_width - pattern_box.m_x;
        }
        if(top < 0) {
          draw_bitmap(write_image(image.copy(0, 0, image.width(), 1)),
            Matrix(width, 0, 0, -top, 0, top));
          pattern_box = Rect(pattern_box.m_x, 0, pattern_box.m_width,
            pattern_box.get_bottom());
        }
        if(bottom > height) {
          draw_bitmap(write_image(image.copy(0, last_y, image.width(), 1)),
            Matrix(width, 0, 0, bottom - height, 0, height));
          pattern_box.m_height = bounds.m_height - pattern_box.m_y;
        }
        auto dictionary = PdfDictionary("Pattern");
        dictionary.add("PatternType", "1");
        dictionary.add("PaintType", "1");
        dictionary.add("TilingType", "1");
        dictionary.add("BBox", to_pdf_rect(pattern_box));
        dictionary.add("XStep", to_pdf_number(pattern_box.m_width));
        dictionary.add("YStep", to_pdf_number(pattern_box.m_height));
        dictionary.add("Resources", canvas.get_resources());
        auto pattern_matrix =
          final_matrix * make_translation(left, top);
        if(!to_float(pattern_matrix).is_identity()) {
          dictionary.add("Matrix", to_pdf_matrix(to_float(pattern_matrix)));
        }
        auto id = m_file.reserve();
        m_file.write_stream(id, std::move(dictionary), canvas.get_stream());
        m_shaders.emplace(std::move(key), id);
        return id;
      }

      std::string make_interpolation(const Color& first, const Color& second) {
        auto function = PdfDictionary();
        function.add("C0", to_pdf_array({to_pdf_byte_color(first.m_red),
          to_pdf_byte_color(first.m_green), to_pdf_byte_color(first.m_blue)}));
        function.add("C1", to_pdf_array({to_pdf_byte_color(second.m_red),
          to_pdf_byte_color(second.m_green),
          to_pdf_byte_color(second.m_blue)}));
        function.add("Domain", "[0 1]");
        function.add("FunctionType", "2");
        function.add("N", "1");
        return function.to_string();
      }

      std::string make_stitching(std::vector<Color> colors,
          std::vector<float> offsets) {
        auto count = colors.size();
        auto i = std::size_t(1);
        while(i + 1 < count) {
          if(offsets[i - 1] > offsets[i]) {
            offsets[i] = offsets[i - 1];
          }
          if(offsets[i - 1] == offsets[i] && offsets[i] == offsets[i + 1]) {
            --count;
            colors.erase(colors.begin() + static_cast<std::ptrdiff_t>(i));
            offsets.erase(offsets.begin() + static_cast<std::ptrdiff_t>(i));
          } else {
            ++i;
          }
        }
        for(i = 1; i + 1 < count; ++i) {
          if(offsets[i - 1] == offsets[i]) {
            offsets[i] += 0.00001f;
          }
        }
        if(count >= 2 && offsets[count - 2] == offsets[count - 1]) {
          offsets[count - 2] -= 0.00001f;
        }
        if(count == 2) {
          return make_interpolation(colors[0], colors[1]);
        }
        auto function = PdfDictionary();
        function.add("Domain", "[0 1]");
        function.add("FunctionType", "3");
        auto encode = std::vector<std::string>();
        auto bounds = std::vector<std::string>();
        auto functions = std::vector<std::string>();
        for(auto index = std::size_t(1); index < count; ++index) {
          if(index > 1) {
            bounds.push_back(to_pdf_number(offsets[index - 1]));
          }
          encode.push_back("0");
          encode.push_back("1");
          functions.push_back(
            make_interpolation(colors[index - 1], colors[index]));
        }
        function.add("Encode", to_pdf_array(encode));
        function.add("Bounds", to_pdf_array(bounds));
        function.add("Functions", to_pdf_array(functions));
        return function.to_string();
      }

      int write_function_shader(const GradientPaint& gradient,
          const std::vector<Color>& colors, const std::vector<float>& offsets,
          const Matrix& matrix, const Matrix& local) {
        auto key = std::format("function {}", to_pdf_matrix(matrix));
        for(auto i = std::size_t(0); i != colors.size(); ++i) {
          key += ' ' + to_pdf_byte_color(colors[i].m_red) + ' ' +
            to_pdf_byte_color(colors[i].m_green) + ' ' +
            to_pdf_byte_color(colors[i].m_blue) + ' ' +
            to_pdf_number(offsets[i]);
        }
        auto shading = PdfDictionary();
        shading.add("Function", make_stitching(colors, offsets));
        auto type = "2";
        auto coordinates = std::vector<std::string>();
        if(auto linear = std::get_if<LinearGradient>(&gradient.m_geometry)) {
          coordinates = {to_pdf_number(linear->m_start.m_x),
            to_pdf_number(linear->m_start.m_y),
            to_pdf_number(linear->m_end.m_x),
            to_pdf_number(linear->m_end.m_y)};
        } else if(auto radial =
            std::get_if<RadialGradient>(&gradient.m_geometry)) {
          type = "3";
          auto focus = local.map(radial->m_focus);
          auto center = local.map(radial->m_center);
          coordinates = {to_pdf_number(focus.m_x), to_pdf_number(focus.m_y),
            "0", to_pdf_number(center.m_x), to_pdf_number(center.m_y),
            to_pdf_number(radial->m_radius * local.m_a)};
        }
        shading.add("Extend", "[true true]");
        shading.add("Coords", to_pdf_array(coordinates));
        shading.add("ShadingType", type);
        shading.add("ColorSpace", "/DeviceRGB");
        key += ' ' + to_pdf_array(coordinates);
        if(auto shader = m_shaders.find(key); shader != m_shaders.end()) {
          return shader->second;
        }
        auto pattern = PdfDictionary("Pattern");
        pattern.add("PatternType", "2");
        pattern.add("Matrix", to_pdf_matrix(matrix));
        pattern.add("Shading", shading.to_string());
        auto id = m_file.reserve();
        m_file.write(id, pattern.to_string());
        m_shaders.emplace(std::move(key), id);
        return id;
      }

      int get_gradient(Device& device, const GradientPaint& gradient,
          const Matrix& ctm, const Rect& box) {
        auto colors = std::vector<Color>();
        auto offsets = std::vector<float>();
        auto& stops = gradient.m_stops;
        if(stops.empty()) {
          return -1;
        }
        auto previous = 0.0f;
        if(stops.front().m_offset != 0) {
          colors.push_back(stops.front().m_color);
          offsets.push_back(0);
        }
        for(auto& stop : stops) {
          auto offset =
            std::clamp(static_cast<float>(stop.m_offset), previous, 1.0f);
          colors.push_back(stop.m_color);
          offsets.push_back(offset);
          previous = offset;
        }
        if(stops.back().m_offset != 1) {
          colors.push_back(stops.back().m_color);
          offsets.push_back(1);
        }
        if(colors.size() == 1) {
          colors.push_back(colors.front());
          offsets = {0, 1};
        }
        auto local = Matrix();
        auto shader_transform = gradient.m_transform;
        if(std::holds_alternative<RadialGradient>(gradient.m_geometry) &&
            shader_transform.m_b == 0 && shader_transform.m_c == 0 &&
            shader_transform.m_a == shader_transform.m_d &&
            shader_transform.m_a > 0) {
          local = shader_transform;
          shader_transform = Matrix();
        }
        auto matrix =
          to_float(device.get_initial() * ctm * shader_transform);
        auto has_alpha = std::any_of(colors.begin(), colors.end(),
          [] (const auto& color) {
            return std::lround(color.m_alpha * 255) != 255;
          });
        auto opaque = colors;
        for(auto& color : opaque) {
          color.m_alpha = 1;
        }
        auto color_shader =
          write_function_shader(gradient, opaque, offsets, matrix, local);
        if(!has_alpha) {
          return color_shader;
        }
        auto key = std::format("alpha {} {}", color_shader, to_pdf_rect(box));
        for(auto& color : colors) {
          key += ' ' + std::to_string(std::lround(color.m_alpha * 255));
        }
        if(auto shader = m_shaders.find(key); shader != m_shaders.end()) {
          return shader->second;
        }
        auto luminosity = colors;
        for(auto& color : luminosity) {
          auto alpha = std::lround(color.m_alpha * 255) / 255.0;
          color = Color(alpha, alpha, alpha, 1);
        }
        auto luminosity_shader =
          write_function_shader(
            gradient, luminosity, offsets, matrix, local);
        auto mask_content = std::format(
          "/Pattern CS/Pattern cs/P{} SCN/P{} scn\n{}f*\n",
          luminosity_shader, luminosity_shader,
          to_pdf_path(make_rectangle(box)));
        auto mask_resources = PdfDictionary();
        mask_resources.add("ProcSet", PROCEDURES);
        auto mask_patterns = PdfDictionary();
        mask_patterns.add(std::format("P{}", luminosity_shader),
          to_pdf_reference(luminosity_shader));
        mask_resources.add("Pattern", mask_patterns.to_string());
        auto form = PdfDictionary("XObject");
        form.add("Subtype", "/Form");
        form.add("Resources", mask_resources.to_string());
        form.add("BBox", to_pdf_rect(box));
        form.add("Group",
          "<</Type /Group\n/S /Transparency\n/CS /DeviceRGB\n/I true>>");
        auto form_id = m_file.reserve();
        m_file.write_stream(form_id, std::move(form), std::move(mask_content));
        auto mask = PdfDictionary("Mask");
        mask.add("S", "/Luminosity");
        mask.add("G", to_pdf_reference(form_id));
        auto state = PdfDictionary("ExtGState");
        state.add("SMask", mask.to_string());
        auto state_id = get_state(std::move(state));
        auto content = std::format(
          "/G{} gs\n/Pattern CS/Pattern cs/P{} SCN/P{} scn\n{}f*\n",
          state_id, color_shader, color_shader,
          to_pdf_path(make_rectangle(box)));
        auto resources = PdfDictionary();
        resources.add("ProcSet", PROCEDURES);
        auto states = PdfDictionary();
        states.add(std::format("G{}", state_id), to_pdf_reference(state_id));
        resources.add("ExtGState", states.to_string());
        auto patterns = PdfDictionary();
        patterns.add(std::format("P{}", color_shader),
          to_pdf_reference(color_shader));
        resources.add("Pattern", patterns.to_string());
        auto pattern = PdfDictionary("Pattern");
        pattern.add("PatternType", "1");
        pattern.add("PaintType", "1");
        pattern.add("TilingType", "1");
        pattern.add("BBox", to_pdf_rect(box));
        pattern.add("XStep", to_pdf_number(box.m_width));
        pattern.add("YStep", to_pdf_number(box.m_height));
        pattern.add("Resources", resources.to_string());
        auto id = m_file.reserve();
        m_file.write_stream(id, std::move(pattern), std::move(content));
        m_shaders.emplace(std::move(key), id);
        return id;
      }
  };

  Rect get_page_bounds(const Document& document, const NodeId& id) {
    auto& node = document.get(id);
    if(auto artboard = node.get_artboard()) {
      auto origin = get_world_transform(document, id).map(Point(0, 0));
      return Rect(origin.m_x, origin.m_y, artboard->m_frame.m_width,
        artboard->m_frame.m_height);
    }
    auto bounds = get_world_bounds(document, id, BoundsType::GEOMETRY,
      [] (const Text& text) {
        return measure_text(text, get_default_font_library());
      });
    return bounds.value_or(Rect(0, 0, 0, 0));
  }
}

std::vector<NodeId> Rellu::sort_pdf_pages(
    const Document& document, std::vector<NodeId> nodes) {
  if(nodes.size() < 2) {
    return nodes;
  }
  auto bounds = std::unordered_map<NodeId, Rect>();
  for(auto& id : nodes) {
    bounds.emplace(id, get_page_bounds(document, id));
  }
  auto by_y = nodes;
  std::stable_sort(by_y.begin(), by_y.end(), [&] (const auto& left,
      const auto& right) {
    return bounds.at(left).m_y < bounds.at(right).m_y;
  });
  auto classes = std::map<double, int>();
  auto row = 0;
  for(auto i = std::size_t(0); i < by_y.size(); ++row) {
    auto& first = bounds.at(by_y[i]);
    auto limit = first.m_y + first.m_height * EQUIVALENT_Y_OFFSET;
    while(i < by_y.size()) {
      auto& current = bounds.at(by_y[i]);
      if(current.m_y > limit) {
        break;
      }
      classes[current.m_y] = row;
      ++i;
    }
  }
  std::stable_sort(nodes.begin(), nodes.end(), [&] (const auto& left,
      const auto& right) {
    auto& first = bounds.at(left);
    auto& second = bounds.at(right);
    auto first_class = classes.at(first.m_y);
    auto second_class = classes.at(second.m_y);
    if(first_class != second_class) {
      return first_class < second_class;
    }
    return first.m_x < second.m_x;
  });
  return nodes;
}

QByteArray Rellu::write_pdf(
    const Document& document, const std::vector<NodeId>& nodes) {
  auto writer = Writer(document);
  for(auto& id : nodes) {
    writer.add_page(id);
  }
  return writer.finish();
}

void Rellu::write_pdf_export(const Document& document,
    const std::vector<NodeId>& nodes, const std::filesystem::path& path,
    const ExportOptions& options) {
  auto bytes = write_pdf(document, sort_pdf_pages(document, nodes));
  auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
  if(!file) {
    throw std::runtime_error("Unable to open the PDF file.");
  }
  file.write(bytes.constData(), bytes.size());
  if(!file) {
    throw std::runtime_error("Unable to write the PDF file.");
  }
}

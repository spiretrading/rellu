#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_DRIVER_H
#include FT_MODULE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include "GlyphRenderer.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  constexpr auto SAMPLES = 4;
  constexpr auto CURVE_STEPS = 16;

  struct Edge {
    double m_x0;
    double m_y0;
    double m_x1;
    double m_y1;
  };

  struct Flattener {
    std::vector<Edge>* m_edges;
    Matrix m_transform;
    double m_x = 0;
    double m_y = 0;
    double m_start_x = 0;
    double m_start_y = 0;
    bool m_is_open = false;

    double to_x(const FT_Vector* vector) const {
      return m_transform.m_a * vector->x / 64.0 -
        m_transform.m_c * vector->y / 64.0 + m_transform.m_tx;
    }

    double to_y(const FT_Vector* vector) const {
      return m_transform.m_b * vector->x / 64.0 -
        m_transform.m_d * vector->y / 64.0 + m_transform.m_ty;
    }

    void line_to(double x, double y) {
      m_edges->push_back(Edge(m_x, m_y, x, y));
      m_x = x;
      m_y = y;
    }

    void close() {
      if(m_is_open && (m_x != m_start_x || m_y != m_start_y)) {
        line_to(m_start_x, m_start_y);
      }
      m_is_open = false;
    }
  };

  int move_to(const FT_Vector* to, void* user) {
    auto& flattener = *static_cast<Flattener*>(user);
    flattener.close();
    flattener.m_x = flattener.m_start_x = flattener.to_x(to);
    flattener.m_y = flattener.m_start_y = flattener.to_y(to);
    flattener.m_is_open = true;
    return 0;
  }

  int line_to(const FT_Vector* to, void* user) {
    auto& flattener = *static_cast<Flattener*>(user);
    flattener.line_to(flattener.to_x(to), flattener.to_y(to));
    return 0;
  }

  int conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto& flattener = *static_cast<Flattener*>(user);
    auto x0 = flattener.m_x;
    auto y0 = flattener.m_y;
    auto cx = flattener.to_x(control);
    auto cy = flattener.to_y(control);
    auto x1 = flattener.to_x(to);
    auto y1 = flattener.to_y(to);
    for(auto i = 1; i <= CURVE_STEPS; ++i) {
      auto t = static_cast<double>(i) / CURVE_STEPS;
      auto u = 1 - t;
      flattener.line_to(u * u * x0 + 2 * u * t * cx + t * t * x1,
        u * u * y0 + 2 * u * t * cy + t * t * y1);
    }
    return 0;
  }

  int cubic_to(const FT_Vector* first, const FT_Vector* second,
      const FT_Vector* to, void* user) {
    auto& flattener = *static_cast<Flattener*>(user);
    auto x0 = flattener.m_x;
    auto y0 = flattener.m_y;
    auto ax = flattener.to_x(first);
    auto ay = flattener.to_y(first);
    auto bx = flattener.to_x(second);
    auto by = flattener.to_y(second);
    auto x1 = flattener.to_x(to);
    auto y1 = flattener.to_y(to);
    for(auto i = 1; i <= CURVE_STEPS; ++i) {
      auto t = static_cast<double>(i) / CURVE_STEPS;
      auto u = 1 - t;
      flattener.line_to(
        u * u * u * x0 + 3 * u * u * t * ax + 3 * u * t * t * bx +
          t * t * t * x1,
        u * u * u * y0 + 3 * u * u * t * ay + 3 * u * t * t * by +
          t * t * t * y1);
    }
    return 0;
  }

  void sample(const std::vector<Edge>& edges, CoverageMask& mask) {
    auto left = std::numeric_limits<double>::infinity();
    auto top = left;
    auto right = -left;
    auto bottom = -left;
    for(auto& edge : edges) {
      left = std::min({left, edge.m_x0, edge.m_x1});
      right = std::max({right, edge.m_x0, edge.m_x1});
      top = std::min({top, edge.m_y0, edge.m_y1});
      bottom = std::max({bottom, edge.m_y0, edge.m_y1});
    }
    mask = CoverageMask();
    if(edges.empty()) {
      return;
    }
    mask.m_left = static_cast<int>(std::floor(left));
    mask.m_top = static_cast<int>(std::floor(top));
    mask.m_width = static_cast<int>(std::floor(right)) - mask.m_left + 1;
    mask.m_height = static_cast<int>(std::floor(bottom)) - mask.m_top + 1;
    mask.m_coverage.assign(mask.m_width * mask.m_height, 0);
    auto crossings = std::vector<std::pair<double, int>>();
    for(auto row = 0; row != mask.m_height; ++row) {
      for(auto j = 0; j != SAMPLES; ++j) {
        auto y = mask.m_top + row + (j + 0.5) / SAMPLES;
        crossings.clear();
        for(auto& edge : edges) {
          auto y0 = edge.m_y0;
          auto y1 = edge.m_y1;
          auto x0 = edge.m_x0;
          auto x1 = edge.m_x1;
          auto direction = 1;
          if(y0 > y1) {
            std::swap(y0, y1);
            std::swap(x0, x1);
            direction = -1;
          }
          if(y0 != y1 && y >= y0 && y < y1) {
            crossings.emplace_back(
              x0 + (y - y0) * (x1 - x0) / (y1 - y0), direction);
          }
        }
        std::sort(crossings.begin(), crossings.end());
        auto winding = 0;
        for(auto k = std::size_t(0); k + 1 < crossings.size(); ++k) {
          winding += crossings[k].second;
          if(winding == 0) {
            continue;
          }
          auto first = static_cast<int>(std::ceil(
            (crossings[k].first - mask.m_left) * SAMPLES - 0.5));
          auto last = static_cast<int>(std::ceil(
            (crossings[k + 1].first - mask.m_left) * SAMPLES - 0.5)) - 1;
          first = std::max(first, 0);
          last = std::min(last, mask.m_width * SAMPLES - 1);
          for(auto s = first; s <= last; ++s) {
            ++mask.m_coverage[row * mask.m_width + s / SAMPLES];
          }
        }
      }
    }
  }

  class FreeTypeRenderer : public GlyphRenderer {
    public:
      FreeTypeRenderer()
          : m_library(nullptr) {
        FT_Init_FreeType(&m_library);
        auto version = FT_UInt(TT_INTERPRETER_VERSION_40);
        FT_Property_Set(m_library, "truetype", "interpreter-version",
          &version);
      }

      ~FreeTypeRenderer() override {
        for(auto& face : m_faces) {
          if(face.second.second) {
            FT_Done_Face(face.second.second);
          }
        }
        FT_Done_FreeType(m_library);
      }

      bool render(const std::shared_ptr<const Typeface>& typeface,
          std::uint32_t glyph, double size, const Matrix& transform,
          CoverageMask& mask) override {
        auto face = get_face(typeface);
        if(!face) {
          return false;
        }
        mask = CoverageMask();
        if(FT_Set_Char_Size(face, 0,
            static_cast<FT_F26Dot6>(std::lround(size * 64)), 72, 72) ||
            FT_Load_Glyph(face, glyph, FT_LOAD_NO_BITMAP) ||
            face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
          return true;
        }
        auto edges = std::vector<Edge>();
        auto flattener = Flattener(&edges, transform);
        auto funcs = FT_Outline_Funcs(move_to, line_to, conic_to, cubic_to,
          0, 0);
        FT_Outline_Decompose(&face->glyph->outline, &funcs, &flattener);
        flattener.close();
        sample(edges, mask);
        return true;
      }

    private:
      FT_Library m_library;
      std::unordered_map<const Typeface*,
        std::pair<std::shared_ptr<const Typeface>, FT_Face>> m_faces;

      FT_Face get_face(const std::shared_ptr<const Typeface>& typeface) {
        auto& entry = m_faces[typeface.get()];
        if(!entry.first) {
          entry.first = typeface;
          auto data = typeface->get_data();
          if(FT_New_Memory_Face(m_library,
              reinterpret_cast<const FT_Byte*>(data.data()),
              static_cast<FT_Long>(data.size()),
              static_cast<FT_Long>(typeface->get_index()), &entry.second)) {
            entry.second = nullptr;
          } else if(auto instance = typeface->get_instance()) {
            FT_Set_Named_Instance(entry.second, *instance + 1);
          }
        }
        return entry.second;
      }
  };
}

std::unique_ptr<GlyphRenderer> Details::make_free_type_renderer() {
  return std::make_unique<FreeTypeRenderer>();
}

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>
#include "Rellu/Text/TextRasterizer.hpp"
#include "GlyphRenderer.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
#ifdef _WIN32
  constexpr auto DEFAULT_RASTERIZATION = GlyphRasterization::DIRECT_WRITE;
#else
  constexpr auto DEFAULT_RASTERIZATION = GlyphRasterization::FREE_TYPE;
#endif
  constexpr auto SUBPIXELS = 4;
  constexpr auto LUMINANCE_BITS = 3;
  constexpr auto LUMINANCE_LEVELS = 1 << LUMINANCE_BITS;
  constexpr auto CONTRAST = 128.0f / 255.0f;
  constexpr auto MAXIMUM_RASTERIZED_SIZE = 256.0;
  constexpr auto MAXIMUM_LAYERS = std::size_t(32);

  using OpacityTable = std::array<double, 17>;

  int scale_luminance(int level) {
    auto base = level << (8 - LUMINANCE_BITS);
    auto luminance = base;
    for(auto shift = LUMINANCE_BITS; shift < 8; shift += LUMINANCE_BITS) {
      luminance |= base >> shift;
    }
    return luminance;
  }

  int quantize(double component) {
    auto value = std::clamp(static_cast<int>(std::lround(component * 255)),
      0, 255);
    return scale_luminance(value >> (8 - LUMINANCE_BITS));
  }

  float to_linear(float value) {
    if(value <= 0.04045f) {
      return value / 12.92f;
    }
    return std::pow((value + 0.055f) / 1.055f, 2.4f);
  }

  float from_linear(float value) {
    if(value <= 0.0031308f) {
      return value * 12.92f;
    }
    return 1.055f * std::pow(value, 1 / 2.4f) - 0.055f;
  }

  OpacityTable make_opacities(int luminance) {
    auto source = static_cast<float>(luminance) / 255.0f;
    auto linear_source = to_linear(source);
    auto destination = 1.0f - source;
    auto linear_destination = to_linear(destination);
    auto contrast = CONTRAST * linear_destination;
    auto table = OpacityTable();
    for(auto coverage = 0; coverage <= 16; ++coverage) {
      auto raw = static_cast<int>(std::lround(coverage * 255 / 16.0));
      auto alpha = static_cast<float>(raw) / 255.0f;
      alpha += (1.0f - alpha) * contrast * alpha;
      auto result = alpha;
      if(std::abs(source - destination) >= 1.0f / 256.0f) {
        auto output = from_linear(
          linear_source * alpha + (1.0f - alpha) * linear_destination);
        result = (output - destination) / (source - destination);
      }
      table[coverage] = static_cast<int>(std::floor(255.0f * result + 0.5f)) /
        255.0;
    }
    return table;
  }

  const OpacityTable& get_opacities(Color color) {
    static const auto TABLES = [] {
      auto tables = std::array<OpacityTable, LUMINANCE_LEVELS>();
      for(auto level = 0; level != LUMINANCE_LEVELS; ++level) {
        tables[level] = make_opacities(scale_luminance(level));
      }
      return tables;
    }();
    auto luminance = (quantize(color.m_red) * 54 +
      quantize(color.m_green) * 183 + quantize(color.m_blue) * 19) >> 8;
    return TABLES[luminance >> (8 - LUMINANCE_BITS)];
  }

  struct GlyphKey {
    const Typeface* m_typeface;
    std::uint32_t m_glyph;
    double m_size;
    Matrix m_transform;

    bool operator ==(const GlyphKey&) const = default;
  };

  struct GlyphKeyHash {
    std::size_t operator ()(const GlyphKey& key) const {
      auto seed = std::hash<const void*>()(key.m_typeface);
      auto combine = [&] (std::size_t value) {
        seed ^= value + 0x9e3779b97f4a7c15 + (seed << 6) + (seed >> 2);
      };
      combine(std::hash<std::uint32_t>()(key.m_glyph));
      combine(std::hash<double>()(key.m_size));
      combine(std::hash<double>()(key.m_transform.m_a));
      combine(std::hash<double>()(key.m_transform.m_b));
      combine(std::hash<double>()(key.m_transform.m_c));
      combine(std::hash<double>()(key.m_transform.m_d));
      combine(std::hash<double>()(key.m_transform.m_tx));
      combine(std::hash<double>()(key.m_transform.m_ty));
      return seed;
    }
  };

  struct Placement {
    const CoverageMask* m_mask;
    int m_x;
    int m_y;
  };

  CoverageMask make_rectangle(const Rect& rect) {
    auto mask = CoverageMask();
    if(rect.is_empty()) {
      return mask;
    }
    mask.m_left = static_cast<int>(std::floor(rect.get_left()));
    mask.m_top = static_cast<int>(std::floor(rect.get_top()));
    mask.m_width =
      static_cast<int>(std::ceil(rect.get_right())) - mask.m_left;
    mask.m_height =
      static_cast<int>(std::ceil(rect.get_bottom())) - mask.m_top;
    mask.m_coverage.assign(mask.m_width * mask.m_height, 0);
    for(auto row = 0; row != mask.m_height; ++row) {
      auto top = std::max(rect.get_top(), double(mask.m_top + row));
      auto bottom =
        std::min(rect.get_bottom(), double(mask.m_top + row + 1));
      for(auto column = 0; column != mask.m_width; ++column) {
        auto left = std::max(rect.get_left(), double(mask.m_left + column));
        auto right =
          std::min(rect.get_right(), double(mask.m_left + column + 1));
        auto area = std::max(0.0, right - left) * std::max(0.0, bottom - top);
        mask.m_coverage[row * mask.m_width + column] =
          static_cast<std::uint8_t>(std::lround(area * 16));
      }
    }
    return mask;
  }

  Rect get_bounds(const std::vector<Placement>& placements) {
    auto left = std::numeric_limits<int>::max();
    auto top = left;
    auto right = std::numeric_limits<int>::min();
    auto bottom = right;
    for(auto& placement : placements) {
      auto& mask = *placement.m_mask;
      if(mask.m_width == 0 || mask.m_height == 0) {
        continue;
      }
      left = std::min(left, placement.m_x + mask.m_left);
      top = std::min(top, placement.m_y + mask.m_top);
      right = std::max(right, placement.m_x + mask.m_left + mask.m_width);
      bottom = std::max(bottom, placement.m_y + mask.m_top + mask.m_height);
    }
    if(left >= right || top >= bottom) {
      return Rect(0, 0, 0, 0);
    }
    return Rect(left, top, right - left, bottom - top);
  }

  std::vector<std::vector<Placement>> separate(
      const std::vector<Placement>& placements) {
    auto layers = std::vector<std::vector<Placement>>();
    auto bounds = get_bounds(placements);
    if(bounds.is_empty()) {
      return layers;
    }
    auto left = static_cast<int>(bounds.m_x);
    auto top = static_cast<int>(bounds.m_y);
    auto width = static_cast<int>(bounds.m_width);
    auto height = static_cast<int>(bounds.m_height);
    auto occupancy = std::vector<std::uint32_t>(width * height, 0);
    for(auto& placement : placements) {
      auto& mask = *placement.m_mask;
      auto x = placement.m_x + mask.m_left - left;
      auto y = placement.m_y + mask.m_top - top;
      auto used = std::uint32_t(0);
      for(auto row = 0; row != mask.m_height; ++row) {
        auto source = mask.m_coverage.data() + row * mask.m_width;
        auto target = occupancy.data() + (y + row) * width + x;
        for(auto column = 0; column != mask.m_width; ++column) {
          if(source[column] != 0) {
            used |= target[column];
          }
        }
      }
      auto layer = std::size_t(0);
      while(layer + 1 != MAXIMUM_LAYERS &&
          (used & (std::uint32_t(1) << layer))) {
        ++layer;
      }
      for(auto row = 0; row != mask.m_height; ++row) {
        auto source = mask.m_coverage.data() + row * mask.m_width;
        auto target = occupancy.data() + (y + row) * width + x;
        for(auto column = 0; column != mask.m_width; ++column) {
          if(source[column] != 0) {
            target[column] |= std::uint32_t(1) << layer;
          }
        }
      }
      if(layers.size() <= layer) {
        layers.resize(layer + 1);
      }
      layers[layer].push_back(placement);
    }
    return layers;
  }

  CoverageMask combine(const std::vector<Placement>& placements) {
    auto bounds = get_bounds(placements);
    auto result = CoverageMask();
    if(bounds.is_empty()) {
      return result;
    }
    auto left = static_cast<int>(bounds.m_x);
    auto top = static_cast<int>(bounds.m_y);
    auto right = left + static_cast<int>(bounds.m_width);
    auto bottom = top + static_cast<int>(bounds.m_height);
    result.m_left = left;
    result.m_top = top;
    result.m_width = right - left;
    result.m_height = bottom - top;
    result.m_coverage.assign(result.m_width * result.m_height, 0);
    for(auto& placement : placements) {
      auto& mask = *placement.m_mask;
      auto x = placement.m_x + mask.m_left - left;
      auto y = placement.m_y + mask.m_top - top;
      for(auto row = 0; row != mask.m_height; ++row) {
        auto source = mask.m_coverage.data() + row * mask.m_width;
        auto target =
          result.m_coverage.data() + (y + row) * result.m_width + x;
        for(auto column = 0; column != mask.m_width; ++column) {
          target[column] = static_cast<std::uint8_t>(
            std::min(16, target[column] + source[column]));
        }
      }
    }
    return result;
  }
}

struct TextRasterizer::Backend {
  GlyphRasterization m_rasterization;
  std::unique_ptr<GlyphRenderer> m_direct_write;
  std::unique_ptr<GlyphRenderer> m_free_type;
  std::mutex m_mutex;
  std::unordered_map<GlyphKey, CoverageMask, GlyphKeyHash> m_glyphs;

  const CoverageMask& get(
      const PlacedGlyph& glyph, double size, const Matrix& transform) {
    auto key =
      GlyphKey(glyph.m_typeface.get(), glyph.m_index, size, transform);
    auto entry = m_glyphs.find(key);
    if(entry != m_glyphs.end()) {
      return entry->second;
    }
    auto mask = CoverageMask();
    if(!m_direct_write || !m_direct_write->render(
        glyph.m_typeface, glyph.m_index, size, transform, mask)) {
      if(!m_free_type) {
        m_free_type = make_free_type_renderer();
      }
      m_free_type->render(
        glyph.m_typeface, glyph.m_index, size, transform, mask);
    }
    return m_glyphs.emplace(key, std::move(mask)).first->second;
  }
};

TextRasterizer::TextRasterizer()
  : TextRasterizer(DEFAULT_RASTERIZATION) {}

TextRasterizer::TextRasterizer(GlyphRasterization rasterization)
    : m_backend(std::make_unique<Backend>()) {
  if(rasterization == GlyphRasterization::DIRECT_WRITE) {
    m_backend->m_direct_write = make_direct_write_renderer();
  }
  if(m_backend->m_direct_write) {
    m_backend->m_rasterization = GlyphRasterization::DIRECT_WRITE;
  } else {
    m_backend->m_rasterization = GlyphRasterization::FREE_TYPE;
  }
}

TextRasterizer::~TextRasterizer() = default;

GlyphRasterization TextRasterizer::get_rasterization() const {
  return m_backend->m_rasterization;
}

bool TextRasterizer::is_supported(const Matrix& transform) {
  return static_cast<bool>(invert(transform));
}

bool TextRasterizer::is_outlined(
    const PlacedGlyph& glyph, const Matrix& transform) {
  auto size = glyph.m_size * glyph.m_size;
  auto limit = MAXIMUM_RASTERIZED_SIZE * MAXIMUM_RASTERIZED_SIZE;
  return size * (transform.m_a * transform.m_a +
    transform.m_b * transform.m_b) > limit ||
    size * (transform.m_c * transform.m_c +
      transform.m_d * transform.m_d) > limit;
}

Point TextRasterizer::get_origin(
    const PlacedGlyph& glyph, const Matrix& transform) {
  auto origin = transform.map(glyph.m_origin);
  auto x = std::floor(origin.m_x * SUBPIXELS + 0.5) / SUBPIXELS;
  auto y = std::floor(origin.m_y * SUBPIXELS + 0.5) / SUBPIXELS;
  if(transform.m_a == 0 && transform.m_d == 0) {
    x = std::floor(x + 0.5);
    y = std::floor(y + 0.5);
  } else if(transform.m_b == 0 && transform.m_c == 0) {
    y = std::floor(origin.m_y + 0.5);
  }
  return Point(x, y);
}

std::vector<TextRaster> TextRasterizer::rasterize(
    const TextLayout& layout, const Matrix& transform) const {
  auto rasters = std::vector<TextRaster>();
  if(!is_supported(transform)) {
    return rasters;
  }
  auto scale = std::sqrt(std::abs(transform.get_determinant()));
  auto residual = Matrix(transform.m_a / scale, transform.m_b / scale,
    transform.m_c / scale, transform.m_d / scale, 0, 0);
  auto colors = std::vector<std::pair<Color, std::vector<Placement>>>();
  auto get_placements = [&] (std::size_t first, Color color) ->
      std::vector<Placement>& {
    for(auto i = first; i != colors.size(); ++i) {
      if(colors[i].first == color) {
        return colors[i].second;
      }
    }
    return colors.emplace_back(color, std::vector<Placement>()).second;
  };
  auto lock = std::lock_guard(m_backend->m_mutex);
  for(auto& glyph : layout.m_glyphs) {
    if(is_outlined(glyph, transform) ||
        !glyph.m_typeface->get_color_layers(glyph.m_index).empty()) {
      continue;
    }
    auto origin = get_origin(glyph, transform);
    auto column = std::floor(origin.m_x);
    auto row = std::floor(origin.m_y);
    auto offset = residual;
    offset.m_tx = origin.m_x - column;
    offset.m_ty = origin.m_y - row;
    auto& mask = m_backend->get(glyph, glyph.m_size * scale, offset);
    get_placements(0, glyph.m_color).push_back(Placement(
      &mask, static_cast<int>(column), static_cast<int>(row)));
  }
  auto decorations = std::vector<CoverageMask>();
  decorations.reserve(layout.m_decorations.size());
  for(auto& decoration : layout.m_decorations) {
    decorations.push_back(make_rectangle(transform.map(decoration.m_rect)));
  }
  auto first = colors.size();
  for(auto i = std::size_t(0); i != decorations.size(); ++i) {
    get_placements(first, layout.m_decorations[i].m_color).push_back(
      Placement(&decorations[i], 0, 0));
  }
  for(auto i = std::size_t(0); i != colors.size(); ++i) {
    auto layers = std::vector<std::vector<Placement>>();
    if(i < first) {
      layers = separate(colors[i].second);
    } else {
      layers.push_back(std::move(colors[i].second));
    }
    for(auto& layer : layers) {
      auto mask = combine(layer);
      if(!mask.m_coverage.empty()) {
        rasters.push_back(TextRaster(std::move(mask), colors[i].first));
      }
    }
  }
  return rasters;
}

double Rellu::get_text_opacity(int coverage, Color color) {
  return get_opacities(color)[std::clamp(coverage, 0, 16)];
}

const TextRasterizer& Rellu::get_default_text_rasterizer() {
  static auto rasterizer = TextRasterizer();
  return rasterizer;
}

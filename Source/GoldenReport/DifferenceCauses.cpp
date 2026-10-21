#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <variant>
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "DifferenceCauses.hpp"

using namespace Rellu;

namespace {
  constexpr auto COVERED_FLAG = std::uint8_t(1);
  constexpr auto TEXT_FLAG = std::uint8_t(2);
  constexpr auto EDGE_FLAG = std::uint8_t(4);
  constexpr auto BLUR_SHADOW_FLAG = std::uint8_t(8);
  constexpr auto GRADIENT_FLAG = std::uint8_t(16);
  constexpr auto IMAGE_FLAG = std::uint8_t(32);

  struct Frame {
    std::size_t m_begin;
    const BeginLayerCommand* m_layer;
    bool m_is_blurred;
    std::vector<std::uint8_t> m_content;
  };

  void for_each_coverage(const StripScene& scene, std::size_t index,
      QSize size, const auto& visit) {
    for(auto i = scene.m_offsets[index]; i != scene.m_offsets[index + 1];
        ++i) {
      auto& strip = scene.m_strips[i];
      auto left = static_cast<int>(strip.m_x);
      auto top = static_cast<int>(strip.m_y);
      auto right = std::min(left + static_cast<int>(strip.m_width),
        size.width());
      auto bottom = std::min(top + static_cast<int>(strip.m_height),
        size.height());
      for(auto y = std::max(top, 0); y < bottom; ++y) {
        for(auto x = std::max(left, 0); x < right; ++x) {
          auto coverage = 255;
          if(strip.m_column >= 0) {
            auto texel = scene.m_atlas[
              static_cast<std::size_t>(strip.m_row) * STRIP_ATLAS_WIDTH +
              static_cast<std::size_t>(strip.m_column) + (x - left)];
            coverage = static_cast<int>((texel >> (8 * (y - top))) & 0xFF);
          }
          if(coverage != 0) {
            visit(static_cast<std::size_t>(y) * size.width() + x, coverage);
          }
        }
      }
    }
  }

  bool is_integral(Point point) {
    return std::floor(point.m_x) == point.m_x &&
      std::floor(point.m_y) == point.m_y;
  }

  bool is_text(const FillCommand& fill, const std::optional<Matrix>& device) {
    if(!device || !(fill.m_transform == *device)) {
      return false;
    }
    for(auto& segment : fill.m_path->get_segments()) {
      if(segment.m_type == SegmentType::MOVE ||
          segment.m_type == SegmentType::LINE) {
        if(!is_integral(segment.m_points[0])) {
          return false;
        }
      } else if(segment.m_type != SegmentType::CLOSE) {
        return false;
      }
    }
    return true;
  }

  bool has_effects(const BeginLayerCommand& layer) {
    return !layer.m_drop_shadows.empty() || !layer.m_inner_shadows.empty() ||
      layer.m_blur > 0;
  }

  bool is_opaque(const BeginLayerCommand& layer) {
    return layer.m_opacity >= 1 && layer.m_blend_mode == BlendMode::NORMAL &&
      !layer.m_mask && layer.m_inner_shadows.empty() && !(layer.m_blur > 0);
  }

  bool is_opaque(const FillCommand& fill) {
    auto solid = std::get_if<SolidPaint>(&fill.m_paint);
    return solid && solid->m_color.m_alpha >= 1;
  }

  void add_footprint(const std::vector<std::uint8_t>& content, QSize size,
      int dx, int dy, int radius, std::vector<std::uint8_t>& footprint) {
    auto width = size.width();
    auto height = size.height();
    auto left = width;
    auto top = height;
    auto right = -1;
    auto bottom = -1;
    for(auto y = 0; y != height; ++y) {
      for(auto x = 0; x != width; ++x) {
        if(content[static_cast<std::size_t>(y) * width + x]) {
          left = std::min(left, x);
          right = std::max(right, x);
          top = std::min(top, y);
          bottom = std::max(bottom, y);
        }
      }
    }
    if(right < 0) {
      return;
    }
    auto area_width = right - left + 2;
    auto area = std::vector<int>(
      static_cast<std::size_t>(area_width) * (bottom - top + 2), 0);
    for(auto y = top; y <= bottom; ++y) {
      auto sum = 0;
      for(auto x = left; x <= right; ++x) {
        sum += content[static_cast<std::size_t>(y) * width + x] ? 1 : 0;
        area[static_cast<std::size_t>(y - top + 1) * area_width +
          (x - left + 1)] = sum + area[static_cast<std::size_t>(y - top) *
            area_width + (x - left + 1)];
      }
    }
    auto get_sum = [&] (int x0, int y0, int x1, int y1) {
      x0 = std::max(x0, left);
      y0 = std::max(y0, top);
      x1 = std::min(x1, right);
      y1 = std::min(y1, bottom);
      if(x0 > x1 || y0 > y1) {
        return 0;
      }
      auto at = [&] (int x, int y) {
        return area[static_cast<std::size_t>(y - top) * area_width +
          (x - left)];
      };
      return at(x1 + 1, y1 + 1) - at(x0, y1 + 1) - at(x1 + 1, y0) +
        at(x0, y0);
    };
    for(auto y = std::max(0, top + dy - radius);
        y <= std::min(height - 1, bottom + dy + radius); ++y) {
      for(auto x = std::max(0, left + dx - radius);
          x <= std::min(width - 1, right + dx + radius); ++x) {
        if(get_sum(x - dx - radius, y - dy - radius, x - dx + radius,
            y - dy + radius) > 0) {
          footprint[static_cast<std::size_t>(y) * width + x] = 1;
        }
      }
    }
  }

  int to_extent(double radius, const Matrix& view) {
    auto scale =
      std::sqrt(std::abs(view.m_a * view.m_d - view.m_b * view.m_c));
    return static_cast<int>(std::ceil(3 * radius * scale));
  }

  std::vector<std::uint8_t> get_footprint(const Frame& frame,
      const Matrix& view, QSize size) {
    auto footprint = std::vector<std::uint8_t>(frame.m_content.size(), 0);
    auto& layer = *frame.m_layer;
    for(auto& shadow : layer.m_drop_shadows) {
      auto dx = view.m_a * shadow.m_offset.m_x + view.m_c * shadow.m_offset.m_y;
      auto dy = view.m_b * shadow.m_offset.m_x + view.m_d * shadow.m_offset.m_y;
      add_footprint(frame.m_content, size, static_cast<int>(std::lround(dx)),
        static_cast<int>(std::lround(dy)), to_extent(shadow.m_radius, view),
        footprint);
    }
    if(layer.m_blur > 0) {
      add_footprint(frame.m_content, size, 0, 0,
        to_extent(layer.m_blur, view), footprint);
    }
    if(!layer.m_inner_shadows.empty()) {
      for(auto i = std::size_t(0); i != footprint.size(); ++i) {
        footprint[i] |= frame.m_content[i];
      }
    }
    return footprint;
  }
}

std::string_view Rellu::to_name(DifferenceCause cause) {
  if(cause == DifferenceCause::TEXT) {
    return "text";
  } else if(cause == DifferenceCause::EDGE) {
    return "edge";
  } else if(cause == DifferenceCause::BLUR_SHADOW) {
    return "blur_shadow";
  } else if(cause == DifferenceCause::GRADIENT) {
    return "gradient";
  } else if(cause == DifferenceCause::IMAGE) {
    return "image";
  } else if(cause == DifferenceCause::BLENDING) {
    return "blending";
  }
  return "uncovered";
}

DifferenceTally& DifferenceTally::operator +=(const DifferenceTally& tally) {
  for(auto i = 0; i != DIFFERENCE_CAUSE_COUNT; ++i) {
    m_pixels[i] += tally.m_pixels[i];
    m_over_2[i] += tally.m_over_2[i];
    m_over_16[i] += tally.m_over_16[i];
    m_difference[i] += tally.m_difference[i];
  }
  return *this;
}

std::vector<DifferenceCause> Rellu::classify_differences(
    const DrawList& list, const Matrix& view, QSize size) {
  auto count = static_cast<std::size_t>(size.width()) * size.height();
  auto causes = std::vector<DifferenceCause>(count, DifferenceCause::UNCOVERED);
  if(count == 0) {
    return causes;
  }
  auto rasterizer = StripRasterizer();
  auto scene = StripScene();
  rasterizer.rasterize(list, view, size, scene);
  auto device = invert(view);
  auto top = std::vector<std::int64_t>(count, -1);
  auto opacities = std::vector<bool>({true});
  for(auto i = std::size_t(0); i != list.size(); ++i) {
    auto& command = list[i];
    if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
      opacities.push_back(opacities.back() && is_opaque(*layer));
    } else if(std::holds_alternative<EndLayerCommand>(command)) {
      if(opacities.size() > 1) {
        opacities.pop_back();
      }
    } else if(auto fill = std::get_if<FillCommand>(&command)) {
      if(opacities.back() && is_opaque(*fill)) {
        for_each_coverage(scene, i, size, [&] (auto pixel, auto coverage) {
          if(coverage == 255) {
            top[pixel] = static_cast<std::int64_t>(i);
          }
        });
      }
    }
  }
  auto flags = std::vector<std::uint8_t>(count, 0);
  auto frames = std::vector<Frame>();
  auto is_visible = [&] (std::size_t pixel, std::size_t index) {
    return static_cast<std::int64_t>(index) >= top[pixel];
  };
  auto mark = [&] (const std::vector<std::uint8_t>& footprint,
      std::size_t index) {
    for(auto pixel = std::size_t(0); pixel != count; ++pixel) {
      if(footprint[pixel] && is_visible(pixel, index)) {
        flags[pixel] |= BLUR_SHADOW_FLAG;
      }
    }
  };
  for(auto i = std::size_t(0); i != list.size(); ++i) {
    auto& command = list[i];
    if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
      auto& frame = frames.emplace_back(i, layer,
        layer->m_blur > 0 || (!frames.empty() && frames.back().m_is_blurred));
      if(has_effects(*layer)) {
        frame.m_content.assign(count, 0);
      }
      if(layer->m_mask) {
        for_each_coverage(scene, i, size, [&] (auto pixel, auto coverage) {
          if(coverage != 255 && is_visible(pixel, i)) {
            flags[pixel] |= EDGE_FLAG;
          }
        });
      }
    } else if(std::holds_alternative<EndLayerCommand>(command)) {
      if(frames.empty()) {
        continue;
      }
      auto frame = std::move(frames.back());
      frames.pop_back();
      if(frame.m_content.empty()) {
        continue;
      }
      auto footprint = get_footprint(frame, view, size);
      if(!frame.m_layer->m_inner_shadows.empty()) {
        mark(frame.m_content, i);
      }
      mark(footprint, frame.m_begin);
      for(auto& outer : frames) {
        if(!outer.m_content.empty()) {
          for(auto pixel = std::size_t(0); pixel != count; ++pixel) {
            outer.m_content[pixel] |= footprint[pixel];
          }
        }
      }
    } else if(auto fill = std::get_if<FillCommand>(&command)) {
      auto flag = COVERED_FLAG;
      if(is_text(*fill, device)) {
        flag |= TEXT_FLAG;
      }
      if(std::holds_alternative<GradientPaint>(fill->m_paint)) {
        flag |= GRADIENT_FLAG;
      } else if(std::holds_alternative<ImagePaint>(fill->m_paint)) {
        flag |= IMAGE_FLAG;
      }
      if(!frames.empty() && frames.back().m_is_blurred) {
        flag |= BLUR_SHADOW_FLAG;
      }
      for_each_coverage(scene, i, size, [&] (auto pixel, auto coverage) {
        for(auto& frame : frames) {
          if(!frame.m_content.empty()) {
            frame.m_content[pixel] = 1;
          }
        }
        if(is_visible(pixel, i)) {
          flags[pixel] |= flag;
          if(coverage != 255) {
            flags[pixel] |= EDGE_FLAG;
          }
        }
      });
    } else if(std::holds_alternative<BackdropBlurCommand>(command)) {
      for_each_coverage(scene, i, size, [&] (auto pixel, auto) {
        if(is_visible(pixel, i)) {
          flags[pixel] |= BLUR_SHADOW_FLAG;
        }
      });
    }
  }
  for(auto pixel = std::size_t(0); pixel != count; ++pixel) {
    auto flag = flags[pixel];
    if(flag & TEXT_FLAG) {
      causes[pixel] = DifferenceCause::TEXT;
    } else if(flag & EDGE_FLAG) {
      causes[pixel] = DifferenceCause::EDGE;
    } else if(flag & BLUR_SHADOW_FLAG) {
      causes[pixel] = DifferenceCause::BLUR_SHADOW;
    } else if(flag & GRADIENT_FLAG) {
      causes[pixel] = DifferenceCause::GRADIENT;
    } else if(flag & IMAGE_FLAG) {
      causes[pixel] = DifferenceCause::IMAGE;
    } else if(flag & COVERED_FLAG) {
      causes[pixel] = DifferenceCause::BLENDING;
    }
  }
  return causes;
}

DifferenceTally Rellu::tally_differences(
    const std::vector<DifferenceCause>& causes, const QImage& actual,
    const QImage& expected) {
  auto tally = DifferenceTally();
  auto left = actual.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
  auto right =
    expected.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
  auto width = std::min(left.width(), right.width());
  auto height = std::min(left.height(), right.height());
  for(auto y = 0; y < height; ++y) {
    auto first = left.constScanLine(y);
    auto second = right.constScanLine(y);
    for(auto x = 0; x < width; ++x) {
      auto difference = 0;
      for(auto channel = 0; channel != 4; ++channel) {
        difference = std::max(difference, std::abs(
          int(first[4 * x + channel]) - int(second[4 * x + channel])));
      }
      auto pixel = static_cast<std::size_t>(y) * left.width() + x;
      if(difference == 0 || pixel >= causes.size()) {
        continue;
      }
      auto cause = static_cast<int>(causes[pixel]);
      ++tally.m_pixels[cause];
      tally.m_difference[cause] += difference;
      if(difference > 2) {
        ++tally.m_over_2[cause];
      }
      if(difference > 16) {
        ++tally.m_over_16[cause];
      }
    }
  }
  return tally;
}

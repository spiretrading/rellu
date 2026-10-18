#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <future>
#include <initializer_list>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Supersampler.hpp"

using namespace Rellu;

namespace {
  constexpr auto TILE_WIDTH = 4;
  constexpr auto TILE_BITS = 16;
  constexpr auto INDEX_BITS = 27;
  constexpr auto BAND_SHIFT = TILE_BITS + INDEX_BITS + 1;
  constexpr auto TILE_SHIFT = INDEX_BITS + 1;
  constexpr auto MAXIMUM_SUBDIVISIONS = 1 << 16;
  constexpr auto CHUNK_SIZE = 64;
  constexpr auto INSERTION_SORT_LIMIT = 32;
  constexpr auto MINIMUM_SPAN_WIDTH = 16;

  struct Line {
    float m_x0;
    float m_y0;
    float m_x1;
    float m_y1;
  };

  struct Output {
    std::vector<Strip> m_strips;
    std::vector<std::uint32_t> m_texels;
  };

  struct Box {
    double m_left;
    double m_top;
    double m_right;
    double m_bottom;
  };

  auto make_entry(int band, int tile, std::size_t index, bool is_final) {
    return (static_cast<std::uint64_t>(band) << BAND_SHIFT) |
      (static_cast<std::uint64_t>(tile) << TILE_SHIFT) |
      (static_cast<std::uint64_t>(index) << 1) | (is_final ? 1 : 0);
  }

  auto get_band(std::uint64_t entry) {
    return static_cast<int>(entry >> BAND_SHIFT);
  }

  auto get_tile(std::uint64_t entry) {
    return static_cast<int>((entry >> TILE_SHIFT) & ((1 << TILE_BITS) - 1));
  }

  auto get_index(std::uint64_t entry) {
    return static_cast<std::size_t>((entry >> 1) & ((1 << INDEX_BITS) - 1));
  }

  auto is_final(std::uint64_t entry) {
    return (entry & 1) != 0;
  }

  auto get_subdivisions(double deviation) {
    auto count = std::ceil(std::sqrt(deviation / StripRasterizer::TOLERANCE));
    return static_cast<int>(
      std::clamp(count, 1.0, static_cast<double>(MAXIMUM_SUBDIVISIONS)));
  }

  auto to_premultiplied(const Color& color) {
    auto alpha = std::clamp(color.m_alpha, 0.0, 1.0);
    return std::array<qfloat16, 4>({
      qfloat16(std::clamp(color.m_red, 0.0, 1.0) * alpha),
      qfloat16(std::clamp(color.m_green, 0.0, 1.0) * alpha),
      qfloat16(std::clamp(color.m_blue, 0.0, 1.0) * alpha),
      qfloat16(alpha)});
  }

  auto to_premultiplied(const Paint& paint) {
    if(auto solid = std::get_if<SolidPaint>(&paint)) {
      return to_premultiplied(solid->m_color);
    } else if(auto image = std::get_if<ImagePaint>(&paint)) {
      return to_premultiplied(Color(1, 1, 1, image->m_opacity));
    }
    return to_premultiplied(Color(1, 1, 1, 1));
  }

  auto is_covered(float area, FillRule rule) {
    auto winding = static_cast<int>(std::floor(area + 0.5f));
    if(rule == FillRule::NON_ZERO) {
      return winding != 0;
    }
    return (winding & 1) != 0;
  }

  auto is_outside(std::initializer_list<Point> points, const Box& box) {
    auto left = std::numeric_limits<double>::infinity();
    auto top = left;
    auto right = -left;
    auto bottom = -left;
    for(auto& point : points) {
      left = std::min(left, point.m_x);
      top = std::min(top, point.m_y);
      right = std::max(right, point.m_x);
      bottom = std::max(bottom, point.m_y);
    }
    return right <= box.m_left || left >= box.m_right ||
      bottom <= box.m_top || top >= box.m_bottom;
  }

  auto make_box(
      const std::optional<Rect>& region, const Matrix& view, QSize size) {
    auto box = Box(0, 0, size.width(), size.height());
    if(region) {
      auto clip = view.map(*region);
      box.m_left = std::max(box.m_left, clip.get_left());
      box.m_top = std::max(box.m_top, clip.get_top());
      box.m_right = std::min(box.m_right, clip.get_right());
      box.m_bottom = std::min(box.m_bottom, clip.get_bottom());
    }
    return box;
  }

  void run(std::size_t count, int thread_count, const auto& task) {
    auto next = std::atomic<std::size_t>(0);
    auto work = [&] {
      for(auto index = next++; index < count; index = next++) {
        task(index);
      }
    };
    auto workers = std::vector<std::future<void>>();
    for(auto i = std::size_t(1);
        i < std::min(count, static_cast<std::size_t>(thread_count)); ++i) {
      workers.push_back(std::async(std::launch::async, work));
    }
    work();
    for(auto& worker : workers) {
      worker.get();
    }
  }

  class PathRasterizer {
    public:
      void rasterize(const Path& path, const Matrix& transform,
        const std::optional<Rect>& clip, const std::array<qfloat16, 4>& color,
        float paint, const Matrix& view, QSize size, int first_band,
        int end_band, bool is_clip, Output& output);
      void rasterize(const Path& path, const std::optional<StrokeLine>& stroke,
        const Matrix& transform, const std::optional<Rect>& clip,
        const std::array<qfloat16, 4>& color, float paint, const Matrix& view,
        QSize size, int first_band, int end_band, bool is_clip,
        Output& output);

    private:
      Supersampler m_supersampler;
      std::vector<Supersampler::Line> m_traced;
      QSize m_size;
      Box m_box;
      int m_first_band;
      int m_band_count;
      std::vector<Line> m_lines;
      std::vector<float> m_left;
      std::vector<std::uint64_t> m_entries;
      std::vector<std::uint64_t> m_unsorted;
      std::vector<std::uint32_t> m_offsets;
      std::vector<float> m_cells;
      std::vector<std::size_t> m_previous_spans;
      std::vector<std::size_t> m_current_spans;
      std::array<float, STRIP_HEIGHT> m_carry;
      FillRule m_rule;
      std::array<qfloat16, 4> m_color;
      float m_paint;
      Output* m_output;
      Point m_start;
      Point m_current;

      void move_to(Point point);
      void line_to(Point point);
      void quadratic_to(Point control, Point end);
      void cubic_to(Point first, Point second, Point end);
      void close();
      void add_line(Point start, Point end);
      void add_vertical(double x, double start, double end);
      void add_horizontal(double y, double start, double end);
      void add_left(double start, double end);
      void add_entries();
      void add_tiles(int band, int first_tile, int last_tile,
        std::size_t index);
      void sort_entries();
      void sweep();
      void fill_gap(int band, int rows, int left, int right);
      void accumulate(const Line& line, int x, int y, int columns);
      void add_strip(int band, int rows, int first_tile, int end_tile,
        const std::uint64_t* first, const std::uint64_t* last);
      std::optional<QRect> get_pixel_clip() const;
      void supersample(const Path& path, const Matrix& matrix, bool is_clip);
      void draw_hairline(const StrokeLine& stroke, const Matrix& matrix,
        double coverage);
      void add_coverage(int scale);
      void add_texels(int x, int y, int width, int rows,
        const std::uint32_t* texels);
  };

  void PathRasterizer::rasterize(const Path& path, const Matrix& transform,
      const std::optional<Rect>& clip, const std::array<qfloat16, 4>& color,
      float paint, const Matrix& view, QSize size, int first_band,
      int end_band, bool is_clip, Output& output) {
    rasterize(path, std::nullopt, transform, clip, color, paint, view, size,
      first_band, end_band, is_clip, output);
  }

  void PathRasterizer::rasterize(const Path& path,
      const std::optional<StrokeLine>& stroke, const Matrix& transform,
      const std::optional<Rect>& clip, const std::array<qfloat16, 4>& color,
      float paint, const Matrix& view, QSize size, int first_band,
      int end_band, bool is_clip, Output& output) {
    auto& segments = path.get_segments();
    if(segments.empty() || color[3] == qfloat16(0)) {
      return;
    }
    auto box = make_box(clip, view, size);
    if(!(box.m_left < box.m_right) || !(box.m_top < box.m_bottom)) {
      return;
    }
    auto matrix = view * transform;
    auto top = std::numeric_limits<double>::infinity();
    auto bottom = -top;
    auto left = top;
    auto right = -top;
    for(auto& segment : segments) {
      auto count = 1;
      if(segment.m_type == SegmentType::QUADRATIC) {
        count = 2;
      } else if(segment.m_type == SegmentType::CUBIC) {
        count = 3;
      } else if(segment.m_type == SegmentType::CLOSE) {
        count = 0;
      }
      for(auto i = 0; i != count; ++i) {
        auto point = matrix.map(segment.m_points[i]);
        top = std::min(top, point.m_y);
        bottom = std::max(bottom, point.m_y);
        left = std::min(left, point.m_x);
        right = std::max(right, point.m_x);
      }
    }
    if(stroke) {
      top -= 1;
      bottom += 1;
      left -= 1;
      right += 1;
    }
    if(!(bottom > box.m_top) || !(top < box.m_bottom) ||
        !(right > box.m_left) || !(left < box.m_right)) {
      return;
    }
    m_size = size;
    m_box = box;
    auto first_row = static_cast<int>(std::max(top, box.m_top));
    auto last_row = static_cast<int>(
      std::min(std::ceil(bottom), std::ceil(box.m_bottom)));
    m_first_band = std::max(first_band, first_row / STRIP_HEIGHT);
    m_band_count = std::min(end_band,
      (last_row + STRIP_HEIGHT - 1) / STRIP_HEIGHT) - m_first_band;
    if(m_band_count <= 0) {
      return;
    }
    m_rule = path.get_fill_rule();
    m_color = color;
    m_paint = paint;
    m_output = &output;
    auto antialiasing = Antialiasing::EXACT;
    if(stroke) {
      auto coverage = 1.0;
      if(Supersampler::is_hairline(stroke->m_width, matrix, coverage)) {
        draw_hairline(*stroke, matrix, coverage);
        return;
      }
      antialiasing = Supersampler::get_antialiasing(
        path, *stroke->m_path, stroke->m_width, matrix);
    } else {
      antialiasing = Supersampler::get_antialiasing(path, matrix);
    }
    if(antialiasing == Antialiasing::SUPERSAMPLED) {
      supersample(path, matrix, is_clip);
      return;
    }
    m_lines.clear();
    m_left.assign(m_band_count * STRIP_HEIGHT, 0);
    m_start = Point(0, 0);
    m_current = m_start;
    if(antialiasing == Antialiasing::ANALYTIC) {
      m_supersampler.trace(path, matrix, m_traced);
      for(auto& line : m_traced) {
        add_line(line.m_start, line.m_end);
      }
      add_entries();
      sort_entries();
      sweep();
      return;
    }
    for(auto& segment : segments) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        move_to(matrix.map(points[0]));
      } else if(segment.m_type == SegmentType::LINE) {
        line_to(matrix.map(points[0]));
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        quadratic_to(matrix.map(points[0]), matrix.map(points[1]));
      } else if(segment.m_type == SegmentType::CUBIC) {
        cubic_to(matrix.map(points[0]), matrix.map(points[1]),
          matrix.map(points[2]));
      } else {
        close();
      }
    }
    close();
    add_entries();
    sort_entries();
    sweep();
  }

  void PathRasterizer::move_to(Point point) {
    close();
    m_start = point;
    m_current = point;
  }

  void PathRasterizer::line_to(Point point) {
    add_line(m_current, point);
    m_current = point;
  }

  void PathRasterizer::quadratic_to(Point control, Point end) {
    auto start = m_current;
    auto evaluate = [&] (double t) {
      auto s = 1 - t;
      return s * s * start + 2 * s * t * control + t * t * end;
    };
    if(is_outside({start, control, end}, m_box)) {
      auto denominator = start.m_y - 2 * control.m_y + end.m_y;
      if(denominator != 0) {
        auto t = (start.m_y - control.m_y) / denominator;
        if(t > 0 && t < 1) {
          line_to(evaluate(t));
        }
      }
      line_to(end);
      return;
    }
    auto count = get_subdivisions(length(start - 2 * control + end) / 4);
    for(auto i = 1; i < count; ++i) {
      line_to(evaluate(static_cast<double>(i) / count));
    }
    line_to(end);
  }

  void PathRasterizer::cubic_to(Point first, Point second, Point end) {
    auto start = m_current;
    auto evaluate = [&] (double t) {
      auto s = 1 - t;
      return s * s * s * start + 3 * s * s * t * first +
        3 * s * t * t * second + t * t * t * end;
    };
    if(is_outside({start, first, second, end}, m_box)) {
      auto a = -start.m_y + 3 * first.m_y - 3 * second.m_y + end.m_y;
      auto b = 2 * (start.m_y - 2 * first.m_y + second.m_y);
      auto c = first.m_y - start.m_y;
      auto roots = std::array<double, 2>({-1, -1});
      if(std::abs(a) < 1e-12) {
        if(b != 0) {
          roots[0] = -c / b;
        }
      } else if(auto discriminant = b * b - 4 * a * c; discriminant >= 0) {
        auto root = std::sqrt(discriminant);
        roots[0] = (-b - root) / (2 * a);
        roots[1] = (-b + root) / (2 * a);
      }
      std::sort(roots.begin(), roots.end());
      for(auto t : roots) {
        if(t > 0 && t < 1) {
          line_to(evaluate(t));
        }
      }
      line_to(end);
      return;
    }
    auto curvature = std::max(length(start - 2 * first + second),
      length(first - 2 * second + end));
    auto count = get_subdivisions(3 * curvature / 4);
    for(auto i = 1; i < count; ++i) {
      line_to(evaluate(static_cast<double>(i) / count));
    }
    line_to(end);
  }

  void PathRasterizer::close() {
    if(m_current != m_start) {
      add_line(m_current, m_start);
    }
    m_current = m_start;
  }

  void PathRasterizer::add_line(Point start, Point end) {
    auto& box = m_box;
    if(start.m_y == end.m_y) {
      if(start.m_y <= box.m_top) {
        if(box.m_top > 0) {
          add_horizontal(box.m_top, start.m_x, end.m_x);
        }
      } else if(start.m_y >= box.m_bottom) {
        if(box.m_bottom < m_size.height()) {
          add_horizontal(box.m_bottom, start.m_x, end.m_x);
        }
      } else {
        add_horizontal(start.m_y, start.m_x, end.m_x);
      }
      return;
    }
    auto get_x = [&] (double y) {
      return start.m_x +
        (y - start.m_y) * (end.m_x - start.m_x) / (end.m_y - start.m_y);
    };
    auto clip_y = [&] (Point point) {
      if(point.m_y < box.m_top) {
        auto x = get_x(box.m_top);
        if(box.m_top > 0) {
          add_horizontal(box.m_top, point.m_x, x);
        }
        return Point(x, box.m_top);
      } else if(point.m_y > box.m_bottom) {
        auto x = get_x(box.m_bottom);
        if(box.m_bottom < m_size.height()) {
          add_horizontal(box.m_bottom, point.m_x, x);
        }
        return Point(x, box.m_bottom);
      }
      return point;
    };
    if(std::max(start.m_y, end.m_y) <= box.m_top) {
      if(box.m_top > 0) {
        add_horizontal(box.m_top, start.m_x, end.m_x);
      }
      return;
    } else if(std::min(start.m_y, end.m_y) >= box.m_bottom) {
      if(box.m_bottom < m_size.height()) {
        add_horizontal(box.m_bottom, start.m_x, end.m_x);
      }
      return;
    }
    auto clipped_start = clip_y(start);
    auto clipped_end = clip_y(end);
    start = clipped_start;
    end = clipped_end;
    if(start.m_x <= box.m_left && end.m_x <= box.m_left) {
      add_vertical(box.m_left, start.m_y, end.m_y);
      return;
    } else if(start.m_x >= box.m_right && end.m_x >= box.m_right) {
      add_vertical(box.m_right, start.m_y, end.m_y);
      return;
    }
    auto get_y = [&] (double x) {
      return start.m_y +
        (x - start.m_x) * (end.m_y - start.m_y) / (end.m_x - start.m_x);
    };
    if(start.m_x < box.m_left) {
      auto y = get_y(box.m_left);
      add_vertical(box.m_left, start.m_y, y);
      start = Point(box.m_left, y);
    } else if(end.m_x < box.m_left) {
      auto y = get_y(box.m_left);
      add_vertical(box.m_left, y, end.m_y);
      end = Point(box.m_left, y);
    }
    if(start.m_x > box.m_right) {
      auto y = get_y(box.m_right);
      add_vertical(box.m_right, start.m_y, y);
      start = Point(box.m_right, y);
    } else if(end.m_x > box.m_right) {
      auto y = get_y(box.m_right);
      add_vertical(box.m_right, y, end.m_y);
      end = Point(box.m_right, y);
    }
    if(start.m_y != end.m_y) {
      m_lines.push_back(Line(static_cast<float>(start.m_x),
        static_cast<float>(start.m_y), static_cast<float>(end.m_x),
        static_cast<float>(end.m_y)));
    }
  }

  void PathRasterizer::add_vertical(double x, double start, double end) {
    if(start == end) {
      return;
    } else if(x <= 0) {
      add_left(start, end);
    } else if(x < m_size.width()) {
      auto column = static_cast<float>(x);
      m_lines.push_back(Line(column, static_cast<float>(start), column,
        static_cast<float>(end)));
    }
  }

  void PathRasterizer::add_horizontal(double y, double start, double end) {
    auto left = std::max(std::min(start, end), std::max(m_box.m_left, 0.0));
    auto right = std::min(std::max(start, end), m_box.m_right);
    if(left < right) {
      auto row = static_cast<float>(y);
      m_lines.push_back(Line(
        static_cast<float>(left), row, static_cast<float>(right), row));
    }
  }

  void PathRasterizer::add_left(double start, double end) {
    auto sign = end > start ? 1.0 : -1.0;
    auto top = std::min(start, end);
    auto bottom = std::max(start, end);
    auto origin = m_first_band * STRIP_HEIGHT;
    auto first = std::max(static_cast<int>(top), origin);
    auto last = std::min(static_cast<int>(std::ceil(bottom)),
      origin + static_cast<int>(m_left.size()));
    for(auto row = first; row < last; ++row) {
      auto overlap = std::min(bottom, row + 1.0) - std::max(top, 1.0 * row);
      if(overlap > 0) {
        m_left[row - origin] += static_cast<float>(sign * overlap);
      }
    }
  }

  void PathRasterizer::add_entries() {
    m_unsorted.clear();
    m_offsets.assign(m_band_count + 1, 0);
    auto last_tile = (m_size.width() - 1) / TILE_WIDTH;
    auto to_tile = [&] (float x) {
      return std::clamp(
        static_cast<int>(std::floor(x)) / TILE_WIDTH, 0, last_tile);
    };
    for(auto i = std::size_t(0); i != m_lines.size(); ++i) {
      auto& line = m_lines[i];
      auto top = std::min(line.m_y0, line.m_y1);
      auto bottom = std::max(line.m_y0, line.m_y1);
      if(top == bottom) {
        auto band = static_cast<int>(top) / STRIP_HEIGHT;
        if(top != static_cast<float>(band * STRIP_HEIGHT) &&
            band >= m_first_band && band < m_first_band + m_band_count) {
          add_tiles(band - m_first_band,
            to_tile(std::min(line.m_x0, line.m_x1)),
            to_tile(std::max(line.m_x0, line.m_x1)), i);
        }
        continue;
      }
      auto slope = (line.m_x1 - line.m_x0) / (line.m_y1 - line.m_y0);
      auto first_band =
        std::max(m_first_band, static_cast<int>(top) / STRIP_HEIGHT);
      auto last_band = std::min(m_first_band + m_band_count - 1,
        std::max(static_cast<int>(top) / STRIP_HEIGHT,
          (static_cast<int>(std::ceil(bottom)) - 1) / STRIP_HEIGHT));
      for(auto band = first_band; band <= last_band; ++band) {
        auto band_top =
          std::max(top, static_cast<float>(band * STRIP_HEIGHT));
        auto band_bottom =
          std::min(bottom, static_cast<float>((band + 1) * STRIP_HEIGHT));
        if(band_top < band_bottom) {
          auto x0 = line.m_x0 + (band_top - line.m_y0) * slope;
          auto x1 = line.m_x0 + (band_bottom - line.m_y0) * slope;
          add_tiles(band - m_first_band, to_tile(std::min(x0, x1)),
            to_tile(std::max(x0, x1)), i);
        }
      }
    }
  }

  void PathRasterizer::add_tiles(
      int band, int first_tile, int last_tile, std::size_t index) {
    for(auto tile = first_tile; tile <= last_tile; ++tile) {
      m_unsorted.push_back(make_entry(band, tile, index, tile == last_tile));
    }
    m_offsets[band + 1] += last_tile - first_tile + 1;
  }

  void PathRasterizer::sort_entries() {
    for(auto band = 0; band != m_band_count; ++band) {
      m_offsets[band + 1] += m_offsets[band];
    }
    m_entries.resize(m_unsorted.size());
    for(auto entry : m_unsorted) {
      m_entries[m_offsets[get_band(entry)]++] = entry;
    }
    auto first = m_entries.begin();
    for(auto band = 0; band != m_band_count; ++band) {
      auto last = m_entries.begin() + m_offsets[band];
      if(last - first > INSERTION_SORT_LIMIT) {
        std::sort(first, last);
      } else if(first != last) {
        for(auto i = first + 1; i != last; ++i) {
          auto entry = *i;
          auto j = i;
          while(j != first && *(j - 1) > entry) {
            *j = *(j - 1);
            --j;
          }
          *j = entry;
        }
      }
      first = last;
    }
  }

  void PathRasterizer::sweep() {
    m_previous_spans.clear();
    auto entry = m_entries.data();
    auto end = entry + m_entries.size();
    auto is_in_band = [&] (int band) {
      return entry != end && get_band(*entry) == band;
    };
    for(auto band = 0; band != m_band_count; ++band) {
      auto y = (m_first_band + band) * STRIP_HEIGHT;
      auto rows = std::min(STRIP_HEIGHT, m_size.height() - y);
      auto is_uniform = true;
      for(auto row = 0; row != STRIP_HEIGHT; ++row) {
        if(row < rows) {
          m_carry[row] = m_left[band * STRIP_HEIGHT + row];
          is_uniform =
            is_uniform && std::abs(m_carry[row] - m_carry[0]) < 1e-3f;
        } else {
          m_carry[row] = 0;
        }
      }
      m_current_spans.clear();
      auto cursor = 0;
      auto is_forced = !is_uniform;
      while(is_forced || is_in_band(band)) {
        auto start = 0;
        if(!is_forced) {
          start = get_tile(*entry);
        }
        is_forced = false;
        fill_gap(band, rows, cursor * TILE_WIDTH, start * TILE_WIDTH);
        auto first = entry;
        auto tile = start;
        do {
          while(is_in_band(band) && get_tile(*entry) == tile) {
            ++entry;
          }
          ++tile;
        } while(is_in_band(band) && get_tile(*entry) == tile);
        add_strip(band, rows, start, tile, first, entry);
        cursor = tile;
      }
      fill_gap(band, rows, cursor * TILE_WIDTH, m_size.width());
      std::swap(m_previous_spans, m_current_spans);
    }
  }

  void PathRasterizer::fill_gap(int band, int rows, int left, int right) {
    if(left >= right || !is_covered(m_carry[0], m_rule)) {
      return;
    }
    auto& strips = m_output->m_strips;
    auto y = static_cast<float>((m_first_band + band) * STRIP_HEIGHT);
    auto x = static_cast<float>(left);
    auto width = static_cast<float>(right - left);
    auto match = std::find_if(m_previous_spans.begin(),
      m_previous_spans.end(), [&] (auto index) {
        return strips[index].m_x >= x;
      });
    if(match != m_previous_spans.end() && strips[*match].m_x == x &&
        strips[*match].m_width == width &&
        strips[*match].m_y + strips[*match].m_height == y) {
      strips[*match].m_height += static_cast<float>(rows);
      m_current_spans.push_back(*match);
      return;
    }
    m_current_spans.push_back(strips.size());
    strips.push_back(
      Strip(x, y, width, static_cast<float>(rows), m_color, -1, 0, m_paint));
  }

  void PathRasterizer::accumulate(
      const Line& line, int x, int y, int columns) {
    auto x0 = line.m_x0;
    auto y0 = line.m_y0;
    auto x1 = line.m_x1;
    auto y1 = line.m_y1;
    auto direction = 1.0f;
    if(y0 > y1) {
      std::swap(x0, x1);
      std::swap(y0, y1);
      direction = -1;
    }
    auto start = std::max(y0, static_cast<float>(y));
    auto finish = std::min(y1, static_cast<float>(y + STRIP_HEIGHT));
    if(start >= finish) {
      return;
    }
    auto slope = (x1 - x0) / (y1 - y0);
    auto origin = x0 - static_cast<float>(x);
    auto limit = static_cast<float>(columns - 2);
    auto get_x = [&] (float row_y) {
      return std::clamp(origin + (row_y - y0) * slope, 0.0f, limit);
    };
    auto current = get_x(start);
    auto first_row = static_cast<int>(start) - y;
    auto last_row = static_cast<int>(finish) - y;
    if(static_cast<float>(last_row + y) < finish) {
      ++last_row;
    }
    for(auto row = first_row; row < last_row; ++row) {
      auto row_top = std::max(start, static_cast<float>(y + row));
      auto row_bottom = std::min(finish, static_cast<float>(y + row + 1));
      auto next = get_x(row_bottom);
      auto height = direction * (row_bottom - row_top);
      auto cells = m_cells.data() + row * columns;
      auto left = std::min(current, next);
      auto right = std::max(current, next);
      auto left_cell = static_cast<int>(left);
      auto right_cell = static_cast<int>(right);
      if(static_cast<float>(right_cell) < right) {
        ++right_cell;
      }
      if(right_cell <= left_cell + 1) {
        auto middle =
          0.5f * (current + next) - static_cast<float>(left_cell);
        cells[left_cell] += height - height * middle;
        cells[left_cell + 1] += height * middle;
      } else {
        auto inverse = 1 / (right - left);
        auto left_fraction = left - static_cast<float>(left_cell);
        auto first_area =
          0.5f * inverse * (1 - left_fraction) * (1 - left_fraction);
        auto right_fraction = right - static_cast<float>(right_cell) + 1;
        auto last_area = 0.5f * inverse * right_fraction * right_fraction;
        cells[left_cell] += height * first_area;
        if(right_cell == left_cell + 2) {
          cells[left_cell + 1] += height * (1 - first_area - last_area);
        } else {
          auto second_area = inverse * (1.5f - left_fraction);
          cells[left_cell + 1] += height * (second_area - first_area);
          for(auto cell = left_cell + 2; cell < right_cell - 1; ++cell) {
            cells[cell] += height * inverse;
          }
          auto covered = second_area +
            static_cast<float>(right_cell - left_cell - 3) * inverse;
          cells[right_cell - 1] += height * (1 - covered - last_area);
        }
        cells[right_cell] += height * last_area;
      }
      current = next;
    }
  }

  void PathRasterizer::add_strip(int band, int rows, int first_tile,
      int end_tile, const std::uint64_t* first, const std::uint64_t* last) {
    auto y = (m_first_band + band) * STRIP_HEIGHT;
    auto x = first_tile * TILE_WIDTH;
    auto columns = (end_tile - first_tile) * TILE_WIDTH + 2;
    auto width = std::min(end_tile * TILE_WIDTH, m_size.width()) - x;
    m_cells.assign(STRIP_HEIGHT * columns, 0);
    for(auto entry = first; entry != last; ++entry) {
      if(is_final(*entry)) {
        accumulate(m_lines[get_index(*entry)], x, y, columns);
      }
    }
    auto& texels = m_output->m_texels;
    auto offset = texels.size();
    texels.resize(offset + width);
    auto output = texels.data() + offset;
    for(auto row = 0; row != rows; ++row) {
      auto cells = m_cells.data() + row * columns;
      auto area = m_carry[row];
      auto shift = 8 * row;
      if(m_rule == FillRule::NON_ZERO) {
        for(auto column = 0; column != width; ++column) {
          area += cells[column];
          output[column] |= static_cast<std::uint32_t>(static_cast<int>(
            std::min(std::abs(area), 1.0f) * 255 + 0.5f)) << shift;
        }
      } else {
        for(auto column = 0; column != width; ++column) {
          area += cells[column];
          auto coverage = std::abs(area - 2 * std::floor(0.5f * area + 0.5f));
          output[column] |=
            static_cast<std::uint32_t>(static_cast<int>(
              coverage * 255 + 0.5f)) << shift;
        }
      }
      for(auto column = width; column != columns; ++column) {
        area += cells[column];
      }
      m_carry[row] = area;
    }
    auto& strips = m_output->m_strips;
    for(auto column = 0; column < width; column += STRIP_ATLAS_WIDTH) {
      strips.push_back(Strip(static_cast<float>(x + column),
        static_cast<float>(y),
        static_cast<float>(std::min(STRIP_ATLAS_WIDTH, width - column)),
        static_cast<float>(rows), m_color,
        static_cast<float>(offset + column), 0, m_paint));
    }
  }

  std::optional<QRect> PathRasterizer::get_pixel_clip() const {
    auto top = std::max(static_cast<int>(std::floor(m_box.m_top)),
      m_first_band * STRIP_HEIGHT);
    auto bottom = std::min(static_cast<int>(std::ceil(m_box.m_bottom)),
      (m_first_band + m_band_count) * STRIP_HEIGHT);
    auto left = static_cast<int>(std::floor(m_box.m_left));
    auto right = static_cast<int>(std::ceil(m_box.m_right));
    if(top >= bottom || left >= right) {
      return std::nullopt;
    }
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
  }

  void PathRasterizer::supersample(
      const Path& path, const Matrix& matrix, bool is_clip) {
    if(auto clip = get_pixel_clip()) {
      m_supersampler.rasterize(path, matrix, *clip, is_clip);
      add_coverage(256);
    }
  }

  void PathRasterizer::draw_hairline(
      const StrokeLine& stroke, const Matrix& matrix, double coverage) {
    if(auto clip = get_pixel_clip()) {
      m_supersampler.rasterize_hairline(
        *stroke.m_path, matrix, stroke.m_cap, *clip);
      add_coverage(static_cast<int>(coverage * 256));
    }
  }

  void PathRasterizer::add_coverage(int scale) {
    auto& bounds = m_supersampler.get_bounds();
    if(bounds.isEmpty()) {
      return;
    }
    auto& coverage = m_supersampler.get_coverage();
    auto overlap = [] (double low, double high, int pixel) {
      return std::clamp(
        std::min(high, pixel + 1.0) - std::max(low, 1.0 * pixel), 0.0, 1.0);
    };
    auto width = bounds.width();
    auto texels = std::vector<std::uint32_t>(width);
    for(auto band = bounds.top() / STRIP_HEIGHT;
        band <= bounds.bottom() / STRIP_HEIGHT; ++band) {
      auto y = band * STRIP_HEIGHT;
      auto rows = std::min(STRIP_HEIGHT, m_size.height() - y);
      std::fill(texels.begin(), texels.end(), 0);
      for(auto row = 0; row != rows; ++row) {
        auto pixel_y = y + row;
        if(pixel_y < bounds.top() || pixel_y > bounds.bottom()) {
          continue;
        }
        auto vertical = overlap(m_box.m_top, m_box.m_bottom, pixel_y);
        auto line = coverage.data() +
          static_cast<std::size_t>(pixel_y - bounds.top()) * width;
        for(auto column = 0; column != width; ++column) {
          auto value = static_cast<int>(line[column]);
          if(scale < 256) {
            value = (value * scale) >> 8;
          }
          auto factor = vertical *
            overlap(m_box.m_left, m_box.m_right, bounds.left() + column);
          if(factor < 1) {
            value = static_cast<int>(value * factor + 0.5);
          }
          texels[column] |= static_cast<std::uint32_t>(value) << (8 * row);
        }
      }
      add_texels(bounds.left(), y, width, rows, texels.data());
    }
  }

  void PathRasterizer::add_texels(
      int x, int y, int width, int rows, const std::uint32_t* texels) {
    auto full = rows == STRIP_HEIGHT ? 0xFFFFFFFFu : (1u << (8 * rows)) - 1;
    auto& strips = m_output->m_strips;
    auto& output = m_output->m_texels;
    auto emit = [&] (int first, int last) {
      for(auto column = first; column < last; column += STRIP_ATLAS_WIDTH) {
        auto count = std::min(STRIP_ATLAS_WIDTH, last - column);
        auto offset = output.size();
        output.insert(output.end(), texels + column, texels + column + count);
        strips.push_back(Strip(static_cast<float>(x + column),
          static_cast<float>(y), static_cast<float>(count),
          static_cast<float>(rows), m_color, static_cast<float>(offset), 0,
          m_paint));
      }
    };
    auto column = 0;
    while(column != width) {
      if(texels[column] == 0) {
        ++column;
        continue;
      }
      auto start = column;
      while(column != width && texels[column] != 0) {
        if(texels[column] != full) {
          ++column;
          continue;
        }
        auto end = column;
        while(end != width && texels[end] == full) {
          ++end;
        }
        if(end - column >= MINIMUM_SPAN_WIDTH) {
          emit(start, column);
          strips.push_back(Strip(static_cast<float>(x + column),
            static_cast<float>(y), static_cast<float>(end - column),
            static_cast<float>(rows), m_color, -1, 0, m_paint));
          start = end;
        }
        column = end;
      }
      emit(start, column);
    }
  }
}

struct StripRasterizer::Chunk {
  PathRasterizer m_rasterizer;
  Output m_output;
  std::vector<std::uint32_t> m_offsets;
  std::size_t m_first_strip;
  int m_first_row;
  int m_row_count;
};

int StripScene::get_atlas_height() const {
  return static_cast<int>(m_atlas.size() / STRIP_ATLAS_WIDTH);
}

StripRasterizer::StripRasterizer()
  : StripRasterizer(
      static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))) {}

StripRasterizer::StripRasterizer(int thread_count)
  : m_thread_count(std::max(1, thread_count)) {}

StripRasterizer::~StripRasterizer() = default;

void StripRasterizer::rasterize(const DrawList& list, const Matrix& view,
    QSize size, StripScene& scene) {
  rasterize(to_sequence(list), view, size, scene);
}

void StripRasterizer::rasterize(const DrawSequence& commands,
    const Matrix& view, QSize size, StripScene& scene) {
  m_restrictions.assign(commands.size(), 0);
  rasterize(commands, view, size, 0, 0, scene);
}

void StripRasterizer::rasterize(const DrawSequence& commands,
    const Matrix& view, QSize size, const QRect& region, StripScene& scene) {
  m_restrictions.resize(commands.size());
  auto depth = 0;
  for(auto i = std::size_t(0); i != commands.size(); ++i) {
    auto& command = *commands[i];
    m_restrictions[i] =
      depth == 0 && std::holds_alternative<FillCommand>(command);
    if(std::holds_alternative<BeginLayerCommand>(command)) {
      ++depth;
    } else if(std::holds_alternative<EndLayerCommand>(command)) {
      depth = std::max(0, depth - 1);
    }
  }
  auto first_band = std::max(0, region.top()) / STRIP_HEIGHT;
  auto end_band = std::max(first_band,
    (region.y() + region.height() + STRIP_HEIGHT - 1) / STRIP_HEIGHT);
  rasterize(commands, view, size, first_band, end_band, scene);
}

void StripRasterizer::rasterize(const DrawSequence& commands,
    const Matrix& view, QSize size, int first_band, int end_band,
    StripScene& scene) {
  auto chunk_count = std::max<std::size_t>(1, std::min<std::size_t>(
    (commands.size() + CHUNK_SIZE - 1) / CHUNK_SIZE, 4 * m_thread_count));
  while(m_chunks.size() < chunk_count) {
    m_chunks.push_back(std::make_unique<Chunk>());
  }
  m_slots.resize(commands.size());
  scene.m_paints.clear();
  for(auto i = std::size_t(0); i != commands.size(); ++i) {
    auto& command = *commands[i];
    auto fill = std::get_if<FillCommand>(&command);
    if(std::holds_alternative<EndLayerCommand>(command) ||
        (fill && std::holds_alternative<SolidPaint>(fill->m_paint))) {
      m_slots[i] = -1;
    } else {
      m_slots[i] = static_cast<float>(scene.m_paints.size());
      scene.m_paints.push_back(static_cast<std::uint32_t>(i));
    }
  }
  run(chunk_count, m_thread_count, [&] (auto index) {
    auto& chunk = *m_chunks[index];
    auto& output = chunk.m_output;
    output.m_strips.clear();
    output.m_texels.clear();
    chunk.m_offsets.clear();
    auto first = commands.size() * index / chunk_count;
    auto last = commands.size() * (index + 1) / chunk_count;
    for(auto i = first; i != last; ++i) {
      chunk.m_offsets.push_back(
        static_cast<std::uint32_t>(output.m_strips.size()));
      auto& rasterizer = chunk.m_rasterizer;
      auto& command = *commands[i];
      if(auto fill = std::get_if<FillCommand>(&command)) {
        auto bands = std::pair(0, std::numeric_limits<int>::max());
        if(m_restrictions[i]) {
          bands = std::pair(first_band, end_band);
        }
        rasterizer.rasterize(*fill->m_path, fill->m_stroke, fill->m_transform,
          fill->m_clip, to_premultiplied(fill->m_paint), m_slots[i], view,
          size, bands.first, bands.second, false, output);
      } else if(auto layer = std::get_if<BeginLayerCommand>(&command)) {
        if(layer->m_mask) {
          rasterizer.rasterize(*layer->m_mask->m_path,
            layer->m_mask->m_transform, layer->m_clip,
            to_premultiplied(Color(1, 1, 1, layer->m_opacity)), m_slots[i],
            view, size, 0, std::numeric_limits<int>::max(), true, output);
        }
      } else if(auto blur = std::get_if<BackdropBlurCommand>(&command)) {
        rasterizer.rasterize(*blur->m_path, blur->m_transform, blur->m_clip,
          to_premultiplied(Color(1, 1, 1, 1)), m_slots[i], view, size, 0,
          std::numeric_limits<int>::max(), true, output);
      }
    }
    auto column = 0;
    auto row = 0;
    for(auto& strip : output.m_strips) {
      if(strip.m_column >= 0) {
        auto width = static_cast<int>(strip.m_width);
        if(column + width > STRIP_ATLAS_WIDTH) {
          ++row;
          column = 0;
        }
        strip.m_column = static_cast<float>(column);
        strip.m_row = static_cast<float>(row);
        column += width;
      }
    }
    chunk.m_row_count = column == 0 ? row : row + 1;
  });
  auto strip_count = std::size_t(0);
  auto row_count = 0;
  for(auto i = std::size_t(0); i != chunk_count; ++i) {
    auto& chunk = *m_chunks[i];
    chunk.m_first_strip = strip_count;
    chunk.m_first_row = row_count;
    strip_count += chunk.m_output.m_strips.size();
    row_count += chunk.m_row_count;
  }
  scene.m_strips.resize(strip_count);
  scene.m_atlas.resize(
    static_cast<std::size_t>(row_count) * STRIP_ATLAS_WIDTH);
  scene.m_offsets.resize(commands.size() + 1);
  scene.m_offsets.back() = static_cast<std::uint32_t>(strip_count);
  run(chunk_count, m_thread_count, [&] (auto index) {
    auto& chunk = *m_chunks[index];
    auto first = commands.size() * index / chunk_count;
    for(auto i = std::size_t(0); i != chunk.m_offsets.size(); ++i) {
      scene.m_offsets[first + i] = static_cast<std::uint32_t>(
        chunk.m_first_strip + chunk.m_offsets[i]);
    }
    auto texel = chunk.m_output.m_texels.data();
    auto destination = scene.m_strips.data() + chunk.m_first_strip;
    for(auto strip : chunk.m_output.m_strips) {
      if(strip.m_column >= 0) {
        strip.m_row += static_cast<float>(chunk.m_first_row);
        auto width = static_cast<std::size_t>(strip.m_width);
        std::memcpy(scene.m_atlas.data() +
          static_cast<std::size_t>(strip.m_row) * STRIP_ATLAS_WIDTH +
          static_cast<std::size_t>(strip.m_column), texel,
          width * sizeof(std::uint32_t));
        texel += width;
      }
      *destination++ = strip;
    }
  });
}

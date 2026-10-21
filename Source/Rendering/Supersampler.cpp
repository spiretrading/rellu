#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include "Supersampler.hpp"

using namespace Rellu;

namespace {
  constexpr auto SHIFT = 2;
  constexpr auto SCALE = 1 << SHIFT;
  constexpr auto SUPER_MASK = SCALE - 1;
  constexpr auto MAXIMUM_COEFFICIENT_SHIFT = 6;
  constexpr auto MASK_WIDTH = 32;
  constexpr auto MASK_STORAGE = 1024;
  constexpr auto CONIC_TOLERANCE = 0.25f;
  constexpr auto MAXIMUM_CONIC_POWER = 5;
  constexpr auto ARC_WEIGHT = static_cast<float>(std::numbers::sqrt2 / 2);
  constexpr auto MAXIMUM_COORDINATE = 1 << 20;
  constexpr auto COMPLEXITY_SAMPLES = 8;
  constexpr auto COMPLEXITY_THRESHOLD = 0.25f;

  enum class CurveType : unsigned char {
    LINE,
    QUADRATIC,
    CONIC,
    CUBIC
  };

  struct Vector {
    float m_x;
    float m_y;
  };

  Vector operator +(Vector left, Vector right) {
    return Vector(left.m_x + right.m_x, left.m_y + right.m_y);
  }

  Vector operator -(Vector left, Vector right) {
    return Vector(left.m_x - right.m_x, left.m_y - right.m_y);
  }

  Vector operator *(Vector vector, float scalar) {
    return Vector(vector.m_x * scalar, vector.m_y * scalar);
  }

  Vector mix(Vector start, Vector end, float t) {
    return Vector(std::fma(end.m_x - start.m_x, t, start.m_x),
      std::fma(end.m_y - start.m_y, t, start.m_y));
  }

  Vector to_vector(Point point) {
    return Vector(static_cast<float>(point.m_x), static_cast<float>(point.m_y));
  }

  std::int64_t to_fdot6(float value) {
    return static_cast<std::int64_t>(
      value * static_cast<float>(1 << (SHIFT + 6)));
  }

  std::int64_t round_fdot6(std::int64_t value) {
    return (value + 32) >> 6;
  }

  std::int64_t divide_fdot6(std::int64_t numerator, std::int64_t denominator) {
    return std::clamp((numerator * 65536) / denominator,
      static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
      static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()));
  }

  std::int64_t multiply_fixed(std::int64_t left, std::int64_t right) {
    return (left * right) >> 16;
  }

  std::int64_t round_fixed(std::int64_t value) {
    return (value + 32768) >> 16;
  }

  std::int64_t get_cheap_distance(std::int64_t dx, std::int64_t dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    if(dx > dy) {
      return dx + (dy >> 1);
    }
    return dy + (dx >> 1);
  }

  int to_shift(std::int64_t dx, std::int64_t dy) {
    auto distance = static_cast<std::uint32_t>(
      (get_cheap_distance(dx, dy) + (1 << 4)) >> 5);
    return (32 - std::countl_zero(distance)) >> 1;
  }

  std::int64_t get_cubic_deviation(
      std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d) {
    auto first = ((a * 8 - b * 15 + 6 * c + d) * 19) >> 9;
    auto second = ((a + 6 * b - c * 15 + d * 8) * 19) >> 9;
    return std::max(std::abs(first), std::abs(second));
  }

  bool divide_unit(float numerator, float denominator, float& ratio) {
    if(numerator < 0) {
      numerator = -numerator;
      denominator = -denominator;
    }
    if(denominator == 0 || numerator == 0 || numerator >= denominator) {
      return false;
    }
    auto value = numerator / denominator;
    if(std::isnan(value) || value == 0) {
      return false;
    }
    ratio = value;
    return true;
  }

  int find_unit_roots(float a, float b, float c, std::array<float, 2>& roots) {
    if(a == 0) {
      return divide_unit(-c, b, roots[0]) ? 1 : 0;
    }
    auto discriminant =
      static_cast<double>(b) * b - 4 * static_cast<double>(a) * c;
    if(discriminant < 0) {
      return 0;
    }
    auto root = static_cast<float>(std::sqrt(discriminant));
    if(!std::isfinite(root)) {
      return 0;
    }
    auto q = b < 0 ? -(b - root) / 2 : -(b + root) / 2;
    auto count = 0;
    if(divide_unit(q, a, roots[count])) {
      ++count;
    }
    if(divide_unit(c, q, roots[count])) {
      ++count;
    }
    if(count == 2) {
      if(roots[0] > roots[1]) {
        std::swap(roots[0], roots[1]);
      } else if(roots[0] == roots[1]) {
        count = 1;
      }
    }
    return count;
  }

  bool is_between(float a, float b, float c) {
    return (a - b) * (c - b) <= 0;
  }

  struct Conic {
    std::array<Vector, 3> m_points;
    float m_weight;
  };

  Conic make_conic(Vector start, Vector control, Vector end, float weight) {
    auto conic = Conic();
    conic.m_points = std::to_array({start, control, end});
    conic.m_weight = weight;
    return conic;
  }

  int get_conic_power(const Conic& conic) {
    auto a = conic.m_weight - 1;
    auto k = a / (4 * (2 + a));
    auto& points = conic.m_points;
    auto x = k * (points[0].m_x - 2 * points[1].m_x + points[2].m_x);
    auto y = k * (points[0].m_y - 2 * points[1].m_y + points[2].m_y);
    auto error = std::sqrt(x * x + y * y);
    auto power = 0;
    for(; power < MAXIMUM_CONIC_POWER; ++power) {
      if(error <= CONIC_TOLERANCE) {
        break;
      }
      error *= 0.25f;
    }
    return power;
  }

  std::array<Conic, 2> chop(const Conic& conic) {
    auto scale = 1 / (1 + conic.m_weight);
    auto weight = std::sqrt(0.5f + conic.m_weight * 0.5f);
    auto& points = conic.m_points;
    auto weighted = points[1] * conic.m_weight;
    auto middle =
      (points[0] + weighted * 2 + points[2]) * scale * 0.5f;
    auto halves = std::array<Conic, 2>();
    halves[0] = make_conic(
      points[0], (points[0] + weighted) * scale, middle, weight);
    halves[1] = make_conic(
      middle, (weighted + points[2]) * scale, points[2], weight);
    return halves;
  }

  void subdivide(const Conic& conic, int level, std::vector<Vector>& points) {
    if(level == 0) {
      points.push_back(conic.m_points[1]);
      points.push_back(conic.m_points[2]);
      return;
    }
    auto halves = chop(conic);
    auto start = conic.m_points[0].m_y;
    auto end = conic.m_points[2].m_y;
    if(is_between(start, conic.m_points[1].m_y, end)) {
      auto middle = halves[0].m_points[2].m_y;
      if(!is_between(start, middle, end)) {
        auto closer =
          std::abs(middle - start) < std::abs(middle - end) ? start : end;
        halves[0].m_points[2].m_y = closer;
        halves[1].m_points[0].m_y = closer;
      }
      if(!is_between(start, halves[0].m_points[1].m_y,
          halves[0].m_points[2].m_y)) {
        halves[0].m_points[1].m_y = start;
      }
      if(!is_between(halves[1].m_points[0].m_y, halves[1].m_points[1].m_y,
          end)) {
        halves[1].m_points[1].m_y = end;
      }
    }
    subdivide(halves[0], level - 1, points);
    subdivide(halves[1], level - 1, points);
  }

  std::array<Vector, 5> chop_quadratic(
      const std::array<Vector, 3>& points, float t) {
    auto first = mix(points[0], points[1], t);
    auto second = mix(points[1], points[2], t);
    return std::to_array(
      {points[0], first, mix(first, second, t), second, points[2]});
  }

  std::array<Vector, 7> chop_cubic(const Vector* points, float t) {
    auto ab = mix(points[0], points[1], t);
    auto bc = mix(points[1], points[2], t);
    auto cd = mix(points[2], points[3], t);
    auto abc = mix(ab, bc, t);
    auto bcd = mix(bc, cd, t);
    return std::to_array(
      {points[0], ab, abc, mix(abc, bcd, t), bcd, cd, points[3]});
  }

  struct Measure {
    int m_count = 0;
    float m_length = 0;
    float m_left = std::numeric_limits<float>::infinity();
    float m_top = std::numeric_limits<float>::infinity();
    float m_right = -std::numeric_limits<float>::infinity();
    float m_bottom = -std::numeric_limits<float>::infinity();

    bool is_bounded() const {
      return m_left >= -MAXIMUM_COORDINATE && m_right <= MAXIMUM_COORDINATE &&
        m_top >= -MAXIMUM_COORDINATE && m_bottom <= MAXIMUM_COORDINATE;
    }

    bool is_fat() const {
      if(std::ceil(m_right) - std::floor(m_left) < 3) {
        return false;
      }
      auto left = std::ceil(static_cast<double>(m_left) - 0.5);
      auto top = std::ceil(static_cast<double>(m_top) - 0.5);
      auto right = std::floor(static_cast<double>(m_right) + 0.5);
      auto bottom = std::floor(static_cast<double>(m_bottom) + 0.5);
      auto width = static_cast<int>(right - left);
      return width <= MASK_WIDTH &&
        ((width + 3) & ~3) * (bottom - top) <= MASK_STORAGE;
    }

    Antialiasing get_antialiasing() const {
      auto width = m_right - m_left;
      auto height = m_bottom - m_top;
      if(!(static_cast<float>(m_count) < height)) {
        return Antialiasing::SUPERSAMPLED;
      }
      if(m_count < COMPLEXITY_SAMPLES || !(width > 0 && height > 0)) {
        return Antialiasing::ANALYTIC;
      }
      auto average = m_length / (COMPLEXITY_SAMPLES - 1);
      auto count = static_cast<float>(m_count);
      auto intersections = count * count * average * average /
        (width * width + height * height);
      auto complexity = intersections / height;
      if(std::isnan(complexity) || complexity < COMPLEXITY_THRESHOLD) {
        return Antialiasing::ANALYTIC;
      }
      return Antialiasing::SUPERSAMPLED;
    }
  };

  Measure measure_points(const Path& path, const Matrix& matrix) {
    auto measure = Measure();
    auto previous = Vector(0, 0);
    auto current = Point();
    auto add = [&] (Point point) {
      auto mapped = to_vector(matrix.map(point));
      measure.m_left = std::min(measure.m_left, mapped.m_x);
      measure.m_top = std::min(measure.m_top, mapped.m_y);
      measure.m_right = std::max(measure.m_right, mapped.m_x);
      measure.m_bottom = std::max(measure.m_bottom, mapped.m_y);
      if(measure.m_count != 0 && measure.m_count < COMPLEXITY_SAMPLES) {
        measure.m_length += std::hypot(
          mapped.m_x - previous.m_x, mapped.m_y - previous.m_y);
      }
      previous = mapped;
      ++measure.m_count;
    };
    for(auto& segment : path.get_segments()) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE ||
          segment.m_type == SegmentType::LINE) {
        add(points[0]);
        current = points[0];
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        add(points[0]);
        add(points[1]);
        current = points[1];
      } else if(segment.m_type == SegmentType::CUBIC) {
        if(segment.m_is_arc) {
          add(current + (points[0] - current) / ARC_CONTROL);
        } else {
          add(points[0]);
          add(points[1]);
        }
        add(points[2]);
        current = points[2];
      } else {
        current = points[0];
      }
    }
    return measure;
  }

  double get_length(double x, double y) {
    x = std::abs(x);
    y = std::abs(y);
    if(x < y) {
      std::swap(x, y);
    }
    return x + y / 2;
  }

  bool is_rectangle(const Path& path, const Matrix& matrix) {
    auto corners = std::vector<Vector>();
    auto is_closed = false;
    for(auto& segment : path.get_segments()) {
      if(segment.m_type == SegmentType::MOVE) {
        if(!corners.empty()) {
          return false;
        }
        corners.push_back(to_vector(matrix.map(segment.m_points[0])));
      } else if(segment.m_type == SegmentType::LINE) {
        if(is_closed) {
          return false;
        }
        corners.push_back(to_vector(matrix.map(segment.m_points[0])));
      } else if(segment.m_type == SegmentType::CLOSE) {
        is_closed = true;
      } else {
        return false;
      }
    }
    if(corners.size() > 1 && corners.back().m_x == corners.front().m_x &&
        corners.back().m_y == corners.front().m_y) {
      corners.pop_back();
    }
    auto vertices = std::vector<Vector>();
    for(auto& corner : corners) {
      if(vertices.empty() || corner.m_x != vertices.back().m_x ||
          corner.m_y != vertices.back().m_y) {
        vertices.push_back(corner);
      }
    }
    if(vertices.size() < 4) {
      return false;
    }
    auto is_collinear = [&] (std::size_t i) {
      auto& previous = vertices[(i + vertices.size() - 1) % vertices.size()];
      auto& current = vertices[i];
      auto& next = vertices[(i + 1) % vertices.size()];
      return (previous.m_x == current.m_x && current.m_x == next.m_x) ||
        (previous.m_y == current.m_y && current.m_y == next.m_y);
    };
    auto reduced = std::vector<Vector>();
    for(auto i = std::size_t(0); i != vertices.size(); ++i) {
      if(!is_collinear(i)) {
        reduced.push_back(vertices[i]);
      }
    }
    if(reduced.size() != 4) {
      return false;
    }
    for(auto i = std::size_t(0); i != 4; ++i) {
      auto& current = reduced[i];
      auto& next = reduced[(i + 1) % 4];
      if(current.m_x != next.m_x && current.m_y != next.m_y) {
        return false;
      }
    }
    return true;
  }

  bool is_closed(const Path& path) {
    auto& segments = path.get_segments();
    if(segments.empty()) {
      return false;
    } else if(segments.back().m_type == SegmentType::CLOSE) {
      return true;
    }
    auto& last = segments.back();
    if(last.m_type != SegmentType::LINE) {
      return false;
    }
    return last.m_points[0] == segments.front().m_points[0];
  }

  bool is_rectangle(const CurveOutline& outline) {
    auto path = Path();
    auto index = std::size_t(0);
    for(auto verb : outline.m_verbs) {
      if(verb == CurveOutline::Verb::MOVE) {
        auto& point = outline.m_points[index++];
        path.move_to(Point(point.m_x, point.m_y));
      } else if(verb == CurveOutline::Verb::LINE) {
        auto& point = outline.m_points[index++];
        path.line_to(Point(point.m_x, point.m_y));
      } else if(verb == CurveOutline::Verb::CLOSE) {
        path.close();
      } else {
        return false;
      }
    }
    return is_rectangle(path, Matrix());
  }

  std::int64_t snap_y(std::int64_t y) {
    return ((y + (65536 >> 3)) >> 14) << 14;
  }

  std::int64_t to_analytic(float value) {
    return to_fdot6(value) << 8;
  }

  struct Curve {
    CurveType m_type;
    std::array<Vector, 4> m_points;
    float m_weight;

    Curve(CurveType type, Vector first, Vector second, Vector third = Vector(),
      Vector fourth = Vector(), float weight = 0) noexcept;
  };

  Curve::Curve(CurveType type, Vector first, Vector second,
      Vector third, Vector fourth, float weight) noexcept
    : m_type(type),
      m_points(std::to_array({first, second, third, fourth})),
      m_weight(weight) {}

  struct Edge {
    std::int64_t m_x;
    std::int64_t m_dx;
    int m_first;
    int m_last;
    int m_winding;
    int m_curve_count;
    int m_curve_shift;
    int m_cubic_shift;
    std::int64_t m_cx;
    std::int64_t m_cy;
    std::int64_t m_cdx;
    std::int64_t m_cdy;
    std::int64_t m_cddx;
    std::int64_t m_cddy;
    std::int64_t m_cdddx;
    std::int64_t m_cdddy;
    std::int64_t m_last_x;
    std::int64_t m_last_y;

    bool set_line(Vector start, Vector end);
    bool prepare_quadratic(const Vector* points);
    bool set_quadratic(const Vector* points);
    bool prepare_cubic(const Vector* points);
    bool set_cubic(const Vector* points);
    bool update_line(std::int64_t x0, std::int64_t y0, std::int64_t x1,
      std::int64_t y1);
    bool update_quadratic();
    bool update_cubic();
    bool update();
  };

  bool Edge::set_line(Vector start, Vector end) {
    auto x0 = to_fdot6(start.m_x);
    auto y0 = to_fdot6(start.m_y);
    auto x1 = to_fdot6(end.m_x);
    auto y1 = to_fdot6(end.m_y);
    auto winding = 1;
    if(y0 > y1) {
      std::swap(x0, x1);
      std::swap(y0, y1);
      winding = -1;
    }
    auto top = round_fdot6(y0);
    auto bottom = round_fdot6(y1);
    if(top == bottom) {
      return false;
    }
    auto slope = divide_fdot6(x1 - x0, y1 - y0);
    auto dy = (top << 6) + 32 - y0;
    m_x = (x0 + multiply_fixed(slope, dy)) * 1024;
    m_dx = slope;
    m_first = static_cast<int>(top);
    m_last = static_cast<int>(bottom - 1);
    m_winding = winding;
    m_curve_count = 0;
    m_curve_shift = 0;
    m_cubic_shift = 0;
    return true;
  }

  bool Edge::update_line(
      std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
    y0 >>= 10;
    y1 >>= 10;
    auto top = round_fdot6(y0);
    auto bottom = round_fdot6(y1);
    if(top == bottom) {
      return false;
    }
    x0 >>= 10;
    x1 >>= 10;
    auto slope = divide_fdot6(x1 - x0, y1 - y0);
    auto dy = (top << 6) + 32 - y0;
    m_x = (x0 + multiply_fixed(slope, dy)) * 1024;
    m_dx = slope;
    m_first = static_cast<int>(top);
    m_last = static_cast<int>(bottom - 1);
    return true;
  }

  bool Edge::prepare_quadratic(const Vector* points) {
    auto x0 = to_fdot6(points[0].m_x);
    auto y0 = to_fdot6(points[0].m_y);
    auto x1 = to_fdot6(points[1].m_x);
    auto y1 = to_fdot6(points[1].m_y);
    auto x2 = to_fdot6(points[2].m_x);
    auto y2 = to_fdot6(points[2].m_y);
    auto winding = 1;
    if(y0 > y2) {
      std::swap(x0, x2);
      std::swap(y0, y2);
      winding = -1;
    }
    auto top = round_fdot6(y0);
    auto bottom = round_fdot6(y2);
    if(top == bottom) {
      return false;
    }
    auto shift = to_shift(
      (2 * x1 - x0 - x2) >> 2, (2 * y1 - y0 - y2) >> 2);
    if(shift == 0) {
      shift = 1;
    } else if(shift > MAXIMUM_COEFFICIENT_SHIFT) {
      shift = MAXIMUM_COEFFICIENT_SHIFT;
    }
    m_winding = winding;
    m_curve_count = 1 << shift;
    m_curve_shift = shift - 1;
    m_cubic_shift = 0;
    auto a = (x0 - x1 - x1 + x2) * 512;
    auto b = (x1 - x0) * 1024;
    m_cx = x0 * 1024;
    m_cdx = b + (a >> shift);
    m_cddx = a >> (shift - 1);
    a = (y0 - y1 - y1 + y2) * 512;
    b = (y1 - y0) * 1024;
    m_cy = y0 * 1024;
    m_cdy = b + (a >> shift);
    m_cddy = a >> (shift - 1);
    m_last_x = x2 * 1024;
    m_last_y = y2 * 1024;
    return true;
  }

  bool Edge::set_quadratic(const Vector* points) {
    return prepare_quadratic(points) && update_quadratic();
  }

  bool Edge::update_quadratic() {
    auto count = m_curve_count;
    auto old_x = m_cx;
    auto old_y = m_cy;
    auto dx = m_cdx;
    auto dy = m_cdy;
    auto new_x = old_x;
    auto new_y = old_y;
    auto is_success = false;
    do {
      if(--count > 0) {
        new_x = old_x + (dx >> m_curve_shift);
        dx += m_cddx;
        new_y = old_y + (dy >> m_curve_shift);
        dy += m_cddy;
      } else {
        new_x = m_last_x;
        new_y = m_last_y;
      }
      is_success = update_line(old_x, old_y, new_x, new_y);
      old_x = new_x;
      old_y = new_y;
    } while(count > 0 && !is_success);
    m_cx = new_x;
    m_cy = new_y;
    m_cdx = dx;
    m_cdy = dy;
    m_curve_count = count;
    return is_success;
  }

  bool Edge::prepare_cubic(const Vector* points) {
    auto x = std::array<std::int64_t, 4>();
    auto y = std::array<std::int64_t, 4>();
    for(auto i = 0; i != 4; ++i) {
      x[i] = to_fdot6(points[i].m_x);
      y[i] = to_fdot6(points[i].m_y);
    }
    auto winding = 1;
    if(y[0] > y[3]) {
      std::reverse(x.begin(), x.end());
      std::reverse(y.begin(), y.end());
      winding = -1;
    }
    auto top = round_fdot6(y[0]);
    auto bottom = round_fdot6(y[3]);
    if(top == bottom) {
      return false;
    }
    auto shift = to_shift(get_cubic_deviation(x[0], x[1], x[2], x[3]),
      get_cubic_deviation(y[0], y[1], y[2], y[3])) + 1;
    if(shift > MAXIMUM_COEFFICIENT_SHIFT) {
      shift = MAXIMUM_COEFFICIENT_SHIFT;
    }
    auto up_shift = 6;
    auto down_shift = shift + up_shift - 10;
    if(down_shift < 0) {
      down_shift = 0;
      up_shift = 10 - shift;
    }
    m_winding = winding;
    m_curve_count = -(1 << shift);
    m_curve_shift = shift;
    m_cubic_shift = down_shift;
    auto up = std::int64_t(1) << up_shift;
    auto b = 3 * (x[1] - x[0]) * up;
    auto c = 3 * (x[0] - x[1] - x[1] + x[2]) * up;
    auto d = (x[3] + 3 * (x[1] - x[2]) - x[0]) * up;
    m_cx = x[0] * 1024;
    m_cdx = b + (c >> shift) + (d >> (2 * shift));
    m_cddx = 2 * c + ((3 * d) >> (shift - 1));
    m_cdddx = (3 * d) >> (shift - 1);
    b = 3 * (y[1] - y[0]) * up;
    c = 3 * (y[0] - y[1] - y[1] + y[2]) * up;
    d = (y[3] + 3 * (y[1] - y[2]) - y[0]) * up;
    m_cy = y[0] * 1024;
    m_cdy = b + (c >> shift) + (d >> (2 * shift));
    m_cddy = 2 * c + ((3 * d) >> (shift - 1));
    m_cdddy = (3 * d) >> (shift - 1);
    m_last_x = x[3] * 1024;
    m_last_y = y[3] * 1024;
    return true;
  }

  bool Edge::set_cubic(const Vector* points) {
    return prepare_cubic(points) && update_cubic();
  }

  bool Edge::update_cubic() {
    auto count = m_curve_count;
    auto old_x = m_cx;
    auto old_y = m_cy;
    auto new_x = old_x;
    auto new_y = old_y;
    auto is_success = false;
    do {
      if(++count < 0) {
        new_x = old_x + (m_cdx >> m_cubic_shift);
        m_cdx += m_cddx >> m_curve_shift;
        m_cddx += m_cdddx;
        new_y = old_y + (m_cdy >> m_cubic_shift);
        m_cdy += m_cddy >> m_curve_shift;
        m_cddy += m_cdddy;
      } else {
        new_x = m_last_x;
        new_y = m_last_y;
      }
      if(new_y < old_y) {
        new_y = old_y;
      }
      is_success = update_line(old_x, old_y, new_x, new_y);
      old_x = new_x;
      old_y = new_y;
    } while(count < 0 && !is_success);
    m_cx = new_x;
    m_cy = new_y;
    m_curve_count = count;
    return is_success;
  }

  bool Edge::update() {
    if(m_curve_count > 0) {
      return update_quadratic();
    } else if(m_curve_count < 0) {
      return update_cubic();
    }
    return false;
  }
}

struct Supersampler::Buffers {
  std::vector<Curve> m_curves;
  std::vector<Edge> m_edges;
  std::vector<Edge*> m_active;
  std::vector<std::pair<std::int64_t, int>> m_crossings;
  std::vector<Vector> m_quadratics;
  QRect m_bounds;
  std::vector<std::uint8_t> m_coverage;
  std::vector<Line>* m_lines = nullptr;

  void add_curves(const Path& path, const Matrix& matrix);
  void add_curves(const CurveOutline& outline);
  void rasterize(const QRect& clip, bool is_clip, FillRule rule);
  void add_edges();
  void walk(bool is_mask, FillRule rule);
  void blit(int x, int y, int width, bool is_mask);
  void add_traced(std::int64_t x0, std::int64_t y0, std::int64_t x1,
    std::int64_t y1, int winding);
  void trace_line(Vector start, Vector end);
  void trace_quadratic(const Vector* points);
  void trace_cubic(const Vector* points);
};

Supersampler::Supersampler()
  : m_buffers(std::make_unique<Buffers>()) {}

Supersampler::~Supersampler() = default;

Antialiasing Supersampler::get_antialiasing(
    const Path& path, const Matrix& matrix, PathDrawing drawing) {
  auto measure = measure_points(path, matrix);
  if(!measure.is_bounded()) {
    return Antialiasing::EXACT;
  } else if(is_rectangle(path, matrix)) {
    if(drawing == PathDrawing::RECTANGLE ||
        (drawing == PathDrawing::PATH && measure.is_fat())) {
      return Antialiasing::EXACT;
    }
    return Antialiasing::ANALYTIC;
  }
  return measure.get_antialiasing();
}

bool Supersampler::is_frame(const Path& line, StrokeJoin join,
    double miter_limit, const Matrix& matrix) {
  return join == StrokeJoin::MITER && miter_limit >= std::numbers::sqrt2 &&
    is_closed(line) && is_rectangle(line, matrix);
}

bool Supersampler::is_hairline(
    double width, const Matrix& matrix, double& coverage) {
  auto horizontal = get_length(matrix.m_a * width, matrix.m_b * width);
  auto vertical = get_length(matrix.m_c * width, matrix.m_d * width);
  if(horizontal <= 1 && vertical <= 1) {
    coverage = (horizontal + vertical) / 2;
    return true;
  }
  return false;
}

Antialiasing Supersampler::get_antialiasing(const CurveOutline& outline) {
  auto measure = Measure();
  auto previous = Vector(0, 0);
  for(auto& point : outline.m_points) {
    auto mapped = Vector(point.m_x, point.m_y);
    measure.m_left = std::min(measure.m_left, mapped.m_x);
    measure.m_top = std::min(measure.m_top, mapped.m_y);
    measure.m_right = std::max(measure.m_right, mapped.m_x);
    measure.m_bottom = std::max(measure.m_bottom, mapped.m_y);
    if(measure.m_count != 0 && measure.m_count < COMPLEXITY_SAMPLES) {
      measure.m_length += std::hypot(
        mapped.m_x - previous.m_x, mapped.m_y - previous.m_y);
    }
    previous = mapped;
    ++measure.m_count;
  }
  if(!measure.is_bounded()) {
    return Antialiasing::EXACT;
  }
  if(is_rectangle(outline)) {
    if(measure.is_fat()) {
      return Antialiasing::EXACT;
    }
    return Antialiasing::ANALYTIC;
  }
  return measure.get_antialiasing();
}

void Supersampler::rasterize(const Path& path, const Matrix& matrix,
    const QRect& clip, bool is_clip) {
  auto& buffers = *m_buffers;
  buffers.m_curves.clear();
  buffers.add_curves(path, matrix);
  buffers.rasterize(clip, is_clip, path.get_fill_rule());
}

void Supersampler::rasterize(
    const CurveOutline& outline, const QRect& clip, bool is_clip) {
  auto& buffers = *m_buffers;
  buffers.m_curves.clear();
  buffers.add_curves(outline);
  buffers.rasterize(clip, is_clip, FillRule::NON_ZERO);
}

void Supersampler::Buffers::rasterize(
    const QRect& clip, bool is_clip, FillRule rule) {
  m_edges.clear();
  m_coverage.clear();
  m_bounds = QRect();
  if(m_curves.empty()) {
    return;
  }
  auto left = std::numeric_limits<float>::infinity();
  auto top = left;
  auto right = -left;
  auto bottom = -left;
  for(auto& curve : m_curves) {
    auto count = curve.m_type == CurveType::LINE ? 2 :
      curve.m_type == CurveType::CUBIC ? 4 : 3;
    for(auto i = 0; i != count; ++i) {
      left = std::min(left, curve.m_points[i].m_x);
      top = std::min(top, curve.m_points[i].m_y);
      right = std::max(right, curve.m_points[i].m_x);
      bottom = std::max(bottom, curve.m_points[i].m_y);
    }
  }
  if(!(left >= -MAXIMUM_COORDINATE && right <= MAXIMUM_COORDINATE &&
      top >= -MAXIMUM_COORDINATE && bottom <= MAXIMUM_COORDINATE)) {
    return;
  }
  auto bounds = QRect(QPoint(static_cast<int>(std::floor(left)),
    static_cast<int>(std::floor(top))), QPoint(
      static_cast<int>(std::ceil(right)) - 1,
      static_cast<int>(std::ceil(bottom)) - 1));
  if(bounds.isEmpty()) {
    return;
  }
  auto is_mask = !is_clip && bounds.width() <= MASK_WIDTH &&
    ((bounds.width() + 3) & ~3) * bounds.height() <= MASK_STORAGE;
  m_bounds = bounds & clip;
  if(m_bounds.isEmpty()) {
    m_bounds = QRect();
    return;
  }
  m_coverage.assign(
    static_cast<std::size_t>(m_bounds.width()) * m_bounds.height(), 0);
  add_edges();
  walk(is_mask, rule);
}

void Supersampler::trace(
    const Path& path, const Matrix& matrix, std::vector<Line>& lines) {
  auto& buffers = *m_buffers;
  lines.clear();
  buffers.m_curves.clear();
  buffers.add_curves(path, matrix);
  buffers.m_lines = &lines;
  buffers.add_edges();
  buffers.m_lines = nullptr;
}

void Supersampler::trace(
    const CurveOutline& outline, std::vector<Line>& lines) {
  auto& buffers = *m_buffers;
  lines.clear();
  buffers.m_curves.clear();
  buffers.add_curves(outline);
  buffers.m_lines = &lines;
  buffers.add_edges();
  buffers.m_lines = nullptr;
}

void Supersampler::Buffers::add_curves(const CurveOutline& outline) {
  auto start = Vector(0, 0);
  auto current = start;
  auto is_open = false;
  auto close = [&] {
    if(is_open && (current.m_x != start.m_x || current.m_y != start.m_y)) {
      m_curves.push_back(Curve(CurveType::LINE, current, start));
    }
    current = start;
    is_open = false;
  };
  auto& points = outline.m_points;
  auto point = [&] (std::size_t index) {
    return Vector(points[index].m_x, points[index].m_y);
  };
  auto index = std::size_t(0);
  auto weight = std::size_t(0);
  for(auto verb : outline.m_verbs) {
    if(verb == CurveOutline::Verb::MOVE) {
      close();
      start = point(index++);
      current = start;
      is_open = true;
    } else if(verb == CurveOutline::Verb::LINE) {
      auto end = point(index++);
      m_curves.push_back(Curve(CurveType::LINE, current, end));
      current = end;
      is_open = true;
    } else if(verb == CurveOutline::Verb::QUADRATIC) {
      auto control = point(index);
      auto end = point(index + 1);
      index += 2;
      m_curves.push_back(Curve(CurveType::QUADRATIC, current, control, end));
      current = end;
      is_open = true;
    } else if(verb == CurveOutline::Verb::CONIC) {
      auto control = point(index);
      auto end = point(index + 1);
      index += 2;
      m_curves.push_back(Curve(CurveType::CONIC, current, control, end,
        Vector(), outline.m_weights[weight++]));
      current = end;
      is_open = true;
    } else if(verb == CurveOutline::Verb::CUBIC) {
      auto first = point(index);
      auto second = point(index + 1);
      auto end = point(index + 2);
      index += 3;
      m_curves.push_back(
        Curve(CurveType::CUBIC, current, first, second, end));
      current = end;
      is_open = true;
    } else {
      close();
    }
  }
  close();
}

const QRect& Supersampler::get_bounds() const {
  return m_buffers->m_bounds;
}

const std::vector<std::uint8_t>& Supersampler::get_coverage() const {
  return m_buffers->m_coverage;
}

void Supersampler::Buffers::add_curves(const Path& path, const Matrix& matrix) {
  auto start = Vector(0, 0);
  auto current = start;
  auto source = Point();
  auto is_open = false;
  auto close = [&] {
    if(is_open && (current.m_x != start.m_x || current.m_y != start.m_y)) {
      m_curves.push_back(
        Curve(CurveType::LINE, current, start));
    }
    current = start;
    is_open = false;
  };
  for(auto& segment : path.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      close();
      start = to_vector(matrix.map(points[0]));
      current = start;
      source = points[0];
      is_open = true;
    } else if(segment.m_type == SegmentType::LINE) {
      auto end = to_vector(matrix.map(points[0]));
      m_curves.push_back(Curve(CurveType::LINE, current, end));
      current = end;
      source = points[0];
      is_open = true;
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      auto end = to_vector(matrix.map(points[1]));
      m_curves.push_back(Curve(CurveType::QUADRATIC, current,
        to_vector(matrix.map(points[0])), end));
      current = end;
      source = points[1];
      is_open = true;
    } else if(segment.m_type == SegmentType::CUBIC) {
      auto end = to_vector(matrix.map(points[2]));
      if(segment.m_is_arc) {
        auto corner = source + (points[0] - source) / ARC_CONTROL;
        m_curves.push_back(Curve(CurveType::CONIC, current,
          to_vector(matrix.map(corner)), end, Vector(), ARC_WEIGHT));
      } else {
        m_curves.push_back(Curve(CurveType::CUBIC, current,
          to_vector(matrix.map(points[0])), to_vector(matrix.map(points[1])),
          end));
      }
      current = end;
      source = points[2];
      is_open = true;
    } else {
      close();
      source = points[0];
    }
  }
  close();
}

void Supersampler::Buffers::add_edges() {
  auto add_line = [&] (Vector start, Vector end) {
    if(m_lines) {
      trace_line(start, end);
      return;
    }
    auto edge = Edge();
    if(edge.set_line(start, end)) {
      m_edges.push_back(edge);
    }
  };
  auto add_monotonic_quadratic = [&] (const Vector* points) {
    if(m_lines) {
      trace_quadratic(points);
      return;
    }
    auto edge = Edge();
    if(edge.set_quadratic(points)) {
      m_edges.push_back(edge);
    }
  };
  auto add_monotonic_cubic = [&] (const Vector* points) {
    if(m_lines) {
      trace_cubic(points);
      return;
    }
    auto edge = Edge();
    if(edge.set_cubic(points)) {
      m_edges.push_back(edge);
    }
  };
  auto add_quadratic = [&] (const std::array<Vector, 3>& points) {
    auto a = points[0].m_y;
    auto b = points[1].m_y;
    auto c = points[2].m_y;
    auto ab = a - b;
    auto bc = b - c;
    if(ab < 0) {
      bc = -bc;
    }
    if(ab == 0 || bc < 0) {
      auto t = 0.0f;
      if(divide_unit(a - b, a - b - b + c, t)) {
        auto chopped = chop_quadratic(points, t);
        chopped[1].m_y = chopped[2].m_y;
        chopped[3].m_y = chopped[2].m_y;
        add_monotonic_quadratic(chopped.data());
        add_monotonic_quadratic(chopped.data() + 2);
        return;
      }
      b = std::abs(a - b) < std::abs(b - c) ? a : c;
    }
    auto monotonic = std::array<Vector, 3>();
    monotonic[0] = Vector(points[0].m_x, a);
    monotonic[1] = Vector(points[1].m_x, b);
    monotonic[2] = Vector(points[2].m_x, c);
    add_monotonic_quadratic(monotonic.data());
  };
  auto& quadratics = m_quadratics;
  for(auto& curve : m_curves) {
    auto& points = curve.m_points;
    if(curve.m_type == CurveType::LINE) {
      add_line(points[0], points[1]);
    } else if(curve.m_type == CurveType::QUADRATIC) {
      add_quadratic(std::to_array({points[0], points[1], points[2]}));
    } else if(curve.m_type == CurveType::CONIC) {
      auto conic =
        make_conic(points[0], points[1], points[2], curve.m_weight);
      quadratics.clear();
      quadratics.push_back(points[0]);
      subdivide(conic, get_conic_power(conic), quadratics);
      for(auto i = std::size_t(0); i + 2 < quadratics.size(); i += 2) {
        add_quadratic(std::to_array(
          {quadratics[i], quadratics[i + 1], quadratics[i + 2]}));
      }
    } else {
      auto roots = std::array<float, 2>();
      auto count = find_unit_roots(
        points[3].m_y - points[0].m_y + 3 * (points[1].m_y - points[2].m_y),
        2 * (points[0].m_y - points[1].m_y - points[1].m_y + points[2].m_y),
        points[1].m_y - points[0].m_y, roots);
      auto chopped = std::array<Vector, 10>();
      std::copy(points.begin(), points.end(), chopped.begin());
      if(count > 0) {
        auto first = chop_cubic(points.data(), roots[0]);
        std::copy(first.begin(), first.end(), chopped.begin());
        if(count == 2) {
          auto t = 0.0f;
          if(divide_unit(roots[1] - roots[0], 1 - roots[0], t)) {
            auto second = chop_cubic(chopped.data() + 3, t);
            std::copy(second.begin(), second.end(), chopped.begin() + 3);
          } else {
            chopped[7] = chopped[6];
            chopped[8] = chopped[6];
            chopped[9] = chopped[6];
          }
        }
        chopped[2].m_y = chopped[3].m_y;
        chopped[4].m_y = chopped[3].m_y;
        if(count == 2) {
          chopped[5].m_y = chopped[6].m_y;
          chopped[7].m_y = chopped[6].m_y;
        }
      }
      for(auto i = 0; i <= count; ++i) {
        add_monotonic_cubic(chopped.data() + 3 * i);
      }
    }
  }
}

void Supersampler::Buffers::walk(bool is_mask, FillRule rule) {
  auto first_row = m_bounds.top() * SCALE;
  auto end_row = (m_bounds.bottom() + 1) * SCALE;
  std::sort(m_edges.begin(), m_edges.end(), [] (const auto& a, const auto& b) {
    return a.m_first < b.m_first;
  });
  m_active.clear();
  auto next = std::size_t(0);
  auto mask = rule == FillRule::EVEN_ODD ? 1 : -1;
  auto& crossings = m_crossings;
  for(auto row = first_row; row < end_row; ++row) {
    while(next != m_edges.size() && m_edges[next].m_first <= row) {
      auto& edge = m_edges[next];
      ++next;
      auto is_alive = true;
      while(edge.m_last < row) {
        if(!edge.update()) {
          is_alive = false;
          break;
        }
      }
      if(!is_alive) {
        continue;
      }
      if(edge.m_first < row) {
        edge.m_x += edge.m_dx * (row - edge.m_first);
        edge.m_first = row;
      }
      m_active.push_back(&edge);
    }
    if(m_active.empty()) {
      if(next == m_edges.size()) {
        break;
      }
      continue;
    }
    crossings.clear();
    for(auto edge : m_active) {
      if(edge->m_first <= row) {
        crossings.emplace_back(round_fixed(edge->m_x), edge->m_winding);
      }
    }
    std::sort(crossings.begin(), crossings.end(),
      [] (const auto& a, const auto& b) {
        return a.first < b.first;
      });
    auto winding = 0;
    auto left = std::int64_t(0);
    for(auto& crossing : crossings) {
      if((winding & mask) == 0) {
        left = crossing.first;
      }
      winding += crossing.second;
      if((winding & mask) == 0 && crossing.first > left) {
        blit(static_cast<int>(left), row,
          static_cast<int>(crossing.first - left), is_mask);
      }
    }
    auto survivors = std::size_t(0);
    for(auto edge : m_active) {
      if(edge->m_first > row) {
        m_active[survivors++] = edge;
      } else if(edge->m_last == row) {
        if(edge->update()) {
          m_active[survivors++] = edge;
        }
      } else {
        edge->m_x += edge->m_dx;
        m_active[survivors++] = edge;
      }
    }
    m_active.resize(survivors);
  }
}

void Supersampler::Buffers::blit(int x, int y, int width, bool is_mask) {
  auto super_left = m_bounds.left() * SCALE;
  x -= super_left;
  if(x < 0) {
    width += x;
    x = 0;
  }
  width = std::min(width, m_bounds.width() * SCALE - x);
  if(width <= 0) {
    return;
  }
  auto row = m_coverage.data() + static_cast<std::size_t>(
    (y >> SHIFT) - m_bounds.top()) * m_bounds.width();
  auto start = x;
  auto stop = x + width;
  auto start_fraction = start & SUPER_MASK;
  auto stop_fraction = stop & SUPER_MASK;
  auto middle = (stop >> SHIFT) - (start >> SHIFT) - 1;
  auto maximum = (1 << (8 - SHIFT)) - (((y & SUPER_MASK) + 1) >> SHIFT);
  auto to_alpha = [] (int samples) {
    return samples << (8 - 2 * SHIFT);
  };
  auto add_start = [] (std::uint8_t& pixel, int alpha) {
    auto sum = pixel + alpha;
    pixel = static_cast<std::uint8_t>(sum - (sum >> 8));
  };
  auto pixel = row + (start >> SHIFT);
  if(is_mask) {
    if(middle < 0) {
      add_start(*pixel, to_alpha(stop_fraction - start_fraction));
      return;
    }
    add_start(*pixel, to_alpha(SCALE - start_fraction));
    ++pixel;
    for(auto i = 0; i != middle; ++i) {
      *pixel = static_cast<std::uint8_t>(*pixel + maximum);
      ++pixel;
    }
    if(stop_fraction != 0) {
      *pixel = static_cast<std::uint8_t>(*pixel + to_alpha(stop_fraction));
    }
    return;
  }
  if(middle < 0) {
    start_fraction = stop_fraction - start_fraction;
    middle = 0;
    stop_fraction = 0;
  } else if(start_fraction == 0) {
    ++middle;
  } else {
    start_fraction = SCALE - start_fraction;
  }
  if(start_fraction != 0) {
    add_start(*pixel, to_alpha(start_fraction));
    ++pixel;
  }
  for(auto i = 0; i != middle; ++i) {
    add_start(*pixel, maximum);
    ++pixel;
  }
  if(stop_fraction != 0) {
    *pixel = static_cast<std::uint8_t>(*pixel + to_alpha(stop_fraction));
  }
}

void Supersampler::Buffers::add_traced(std::int64_t x0, std::int64_t y0,
    std::int64_t x1, std::int64_t y1, int winding) {
  auto start = Point(x0 / 65536.0, y0 / 65536.0);
  auto end = Point(x1 / 65536.0, y1 / 65536.0);
  if(winding < 0) {
    std::swap(start, end);
  }
  m_lines->push_back(Line(start, end));
}

void Supersampler::Buffers::trace_line(Vector start, Vector end) {
  auto x0 = to_analytic(start.m_x);
  auto y0 = snap_y(to_analytic(start.m_y));
  auto x1 = to_analytic(end.m_x);
  auto y1 = snap_y(to_analytic(end.m_y));
  add_traced(x0, y0, x1, y1, 1);
}

void Supersampler::Buffers::trace_quadratic(const Vector* points) {
  auto edge = Edge();
  if(!edge.prepare_quadratic(points)) {
    return;
  }
  auto old_x = edge.m_cx >> SHIFT;
  auto old_y = snap_y(edge.m_cy >> SHIFT);
  auto dx = edge.m_cdx >> SHIFT;
  auto dy = edge.m_cdy >> SHIFT;
  auto ddx = edge.m_cddx >> SHIFT;
  auto ddy = edge.m_cddy >> SHIFT;
  auto last_x = edge.m_last_x >> SHIFT;
  auto last_y = snap_y(edge.m_last_y >> SHIFT);
  auto shift = edge.m_curve_shift;
  auto snapped_x = old_x;
  auto snapped_y = old_y;
  for(auto count = edge.m_curve_count; count > 0;) {
    auto new_x = last_x;
    auto new_y = last_y;
    auto new_snapped_x = last_x;
    auto new_snapped_y = last_y;
    if(--count > 0) {
      new_x = old_x + (dx >> shift);
      new_y = old_y + (dy >> shift);
      if(std::abs(dy >> shift) >= 2 * 65536) {
        auto difference = (new_y - snapped_y) >> 10;
        new_snapped_y =
          std::min(last_y, (new_y + 32768) & ~std::int64_t(65535));
        new_snapped_x = new_x;
        if(difference != 0) {
          auto slope =
            divide_fdot6((new_x - snapped_x) >> 10, difference);
          new_snapped_x = new_x - multiply_fixed(slope, new_y - new_snapped_y);
        }
      } else {
        new_snapped_y = std::min(last_y, snap_y(new_y));
        new_snapped_x = new_x;
      }
      dx += ddx;
      dy += ddy;
    }
    add_traced(snapped_x, snapped_y, new_snapped_x, new_snapped_y,
      edge.m_winding);
    old_x = new_x;
    old_y = new_y;
    snapped_x = new_snapped_x;
    snapped_y = new_snapped_y;
  }
}

void Supersampler::Buffers::trace_cubic(const Vector* points) {
  auto edge = Edge();
  if(!edge.prepare_cubic(points)) {
    return;
  }
  auto old_x = edge.m_cx >> SHIFT;
  auto old_y = snap_y(edge.m_cy >> SHIFT);
  auto dx = edge.m_cdx >> SHIFT;
  auto dy = edge.m_cdy >> SHIFT;
  auto ddx = edge.m_cddx >> SHIFT;
  auto ddy = edge.m_cddy >> SHIFT;
  auto dddx = edge.m_cdddx >> SHIFT;
  auto dddy = edge.m_cdddy >> SHIFT;
  auto last_x = edge.m_last_x >> SHIFT;
  auto last_y = snap_y(edge.m_last_y >> SHIFT);
  auto snapped_y = old_y;
  for(auto count = edge.m_curve_count; count < 0;) {
    auto new_x = last_x;
    auto new_y = last_y;
    if(++count < 0) {
      new_x = old_x + (dx >> edge.m_cubic_shift);
      dx += ddx >> edge.m_curve_shift;
      ddx += dddx;
      new_y = old_y + (dy >> edge.m_cubic_shift);
      dy += ddy >> edge.m_curve_shift;
      ddy += dddy;
    }
    if(new_y < old_y) {
      new_y = old_y;
    }
    auto new_snapped_y = snap_y(new_y);
    if(last_y < new_snapped_y) {
      new_snapped_y = last_y;
      count = 0;
    }
    add_traced(old_x, snapped_y, new_x, new_snapped_y, edge.m_winding);
    old_x = new_x;
    old_y = new_y;
    snapped_y = new_snapped_y;
  }
}

namespace {
  constexpr auto MAXIMUM_QUADRATIC_LEVEL = 5;
  constexpr auto MAXIMUM_CUBIC_LEVEL = 9;
  constexpr auto HAIRLINE_LIMIT = 32000.0f;
  constexpr auto SPLIT_LENGTH = 511 * 64;

  class HairlineBlitter {
    public:
      HairlineBlitter(const QRect& bounds,
        std::vector<std::uint8_t>& coverage) noexcept;

      void draw(std::int64_t x0, std::int64_t y0, std::int64_t x1,
        std::int64_t y1);

    private:
      QRect m_bounds;
      std::vector<std::uint8_t>* m_coverage;

      void blit(std::int64_t x, std::int64_t y, std::int64_t alpha);
  };

  HairlineBlitter::HairlineBlitter(const QRect& bounds,
      std::vector<std::uint8_t>& coverage) noexcept
    : m_bounds(bounds),
      m_coverage(&coverage) {}

  void HairlineBlitter::draw(
      std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
    if(std::abs(x1 - x0) > SPLIT_LENGTH || std::abs(y1 - y0) > SPLIT_LENGTH) {
      auto x = (x0 >> 1) + (x1 >> 1);
      auto y = (y0 >> 1) + (y1 >> 1);
      draw(x0, y0, x, y);
      draw(x, y, x1, y1);
      return;
    }
    auto half = std::int64_t(1) << 15;
    auto scale = [] (std::int64_t value, std::int64_t fraction) {
      return (value * fraction) >> 6;
    };
    auto is_horizontal = std::abs(x1 - x0) > std::abs(y1 - y0);
    if(!is_horizontal) {
      std::swap(x0, y0);
      std::swap(x1, y1);
    }
    if(x0 > x1) {
      std::swap(x0, x1);
      std::swap(y0, y1);
    }
    if(x0 == x1 && y0 == y1) {
      return;
    }
    auto pixel = [&] (std::int64_t major, std::int64_t minor,
        std::int64_t alpha) {
      if(is_horizontal) {
        blit(major, minor, alpha);
      } else {
        blit(minor, major, alpha);
      }
    };
    auto start = x0 >> 6;
    auto stop = (x1 + 63) >> 6;
    auto position = y0 * 1024;
    auto slope = std::int64_t(0);
    if(y0 != y1) {
      slope = ((y1 - y0) * 65536) / (x1 - x0);
      position += (slope * (32 - (x0 & 63)) + 32) >> 6;
    }
    auto start_scale = std::int64_t(0);
    auto stop_scale = std::int64_t(0);
    if(stop - start == 1) {
      start_scale = x1 - x0;
    } else {
      start_scale = 64 - (x0 & 63);
      stop_scale = x1 & 63;
    }
    auto draw_cap = [&] (std::int64_t major, std::int64_t fraction) {
      auto value = position + half;
      auto minor = value >> 16;
      auto alpha = (value >> 8) & 0xFF;
      pixel(major, minor, scale(alpha, fraction));
      pixel(major, minor - 1, scale(255 - alpha, fraction));
      position = value + slope - half;
    };
    draw_cap(start, start_scale);
    ++start;
    auto full = stop - start - (stop_scale > 0 ? 1 : 0);
    for(auto major = start; major < start + full; ++major) {
      auto value = position + half;
      auto minor = value >> 16;
      auto alpha = (value >> 8) & 0xFF;
      pixel(major, minor, alpha);
      pixel(major, minor - 1, 255 - alpha);
      position += slope;
    }
    if(stop_scale > 0) {
      draw_cap(stop - 1, stop_scale);
    }
  }

  void HairlineBlitter::blit(
      std::int64_t x, std::int64_t y, std::int64_t alpha) {
    if(alpha <= 0 || x < m_bounds.left() || x > m_bounds.right() ||
        y < m_bounds.top() || y > m_bounds.bottom()) {
      return;
    }
    auto& pixel = (*m_coverage)[static_cast<std::size_t>(
      y - m_bounds.top()) * m_bounds.width() + (x - m_bounds.left())];
    auto value = static_cast<int>(std::min<std::int64_t>(alpha, 255));
    pixel = static_cast<std::uint8_t>(
      pixel + (value * (255 - pixel) + 127) / 255);
  }

  struct Piece {
    CurveType m_type;
    std::array<Vector, 4> m_points;
  };

  int get_point_count(CurveType type) {
    if(type == CurveType::LINE) {
      return 2;
    } else if(type == CurveType::CUBIC) {
      return 4;
    }
    return 3;
  }

  void extend(Piece& piece, bool is_start, float outset) {
    auto count = get_point_count(piece.m_type);
    auto& points = piece.m_points;
    auto end = is_start ? 0 : count - 1;
    auto step = is_start ? 1 : -1;
    auto tangent = Vector(0, 0);
    auto controls = count - 1;
    auto index = end;
    do {
      index += step;
      tangent = points[end] - points[index];
    } while(tangent.m_x == 0 && tangent.m_y == 0 && --controls > 0);
    auto moved = 1;
    if(tangent.m_x == 0 && tangent.m_y == 0) {
      tangent = Vector(1, 0);
      moved = count - 1;
    } else {
      auto length = std::hypot(tangent.m_x, tangent.m_y);
      tangent = Vector(tangent.m_x / length, tangent.m_y / length);
      moved = count - controls;
    }
    for(auto i = 0; i != moved; ++i) {
      auto& point = points[end + i * step];
      point = point + tangent * outset;
    }
  }

  void add_quadratic_lines(const Vector* points, std::vector<Vector>& line) {
    auto dx = std::abs((points[0].m_x + points[2].m_x) / 2 - points[1].m_x);
    auto dy = std::abs((points[0].m_y + points[2].m_y) / 2 - points[1].m_y);
    auto ix = static_cast<std::uint32_t>(std::ceil(dx));
    auto iy = static_cast<std::uint32_t>(std::ceil(dy));
    auto distance = ix > iy ? ix + (iy >> 1) : iy + (ix >> 1);
    auto level = std::min(MAXIMUM_QUADRATIC_LEVEL,
      (33 - std::countl_zero(distance)) >> 1);
    auto count = 1 << level;
    auto a = points[2] - points[1] * 2 + points[0];
    auto b = (points[1] - points[0]) * 2;
    auto c = points[0];
    auto t = 0.0f;
    auto dt = 1.0f / static_cast<float>(count);
    for(auto i = 1; i < count; ++i) {
      t += dt;
      line.push_back((a * t + b) * t + c);
    }
    line.push_back(points[2]);
  }

  void add_cubic_lines(const Vector* points, std::vector<Vector>& line) {
    auto first = points[3] * (1.0f / 3) + points[0] * (2.0f / 3);
    auto second = points[0] * (1.0f / 3) + points[3] * (2.0f / 3);
    auto difference = std::max({std::abs(points[1].m_x - first.m_x),
      std::abs(points[1].m_y - first.m_y),
      std::abs(points[2].m_x - second.m_x),
      std::abs(points[2].m_y - second.m_y)});
    auto tolerance = 1.0f / 8;
    auto count = 1 << MAXIMUM_CUBIC_LEVEL;
    for(auto i = 0; i < MAXIMUM_CUBIC_LEVEL; ++i) {
      if(difference < tolerance) {
        count = 1 << i;
        break;
      }
      tolerance *= 4;
    }
    if(count == 1) {
      line.push_back(points[3]);
      return;
    }
    auto a = points[3] + (points[1] - points[2]) * 3 - points[0];
    auto b = (points[2] - points[1] * 2 + points[0]) * 3;
    auto c = (points[1] - points[0]) * 3;
    auto d = points[0];
    auto t = 0.0f;
    auto dt = 1.0f / static_cast<float>(count);
    for(auto i = 1; i < count; ++i) {
      t += dt;
      line.push_back(((a * t + b) * t + c) * t + d);
    }
    line.push_back(points[3]);
  }
}

void Supersampler::rasterize_hairline(const Path& line, const Matrix& matrix,
    StrokeCap cap, const QRect& clip) {
  auto& buffers = *m_buffers;
  buffers.m_coverage.clear();
  buffers.m_bounds = QRect();
  auto contours = std::vector<std::vector<Piece>>();
  auto closed = std::vector<bool>();
  auto start = Vector(0, 0);
  auto current = start;
  auto source = Point();
  auto left = std::numeric_limits<float>::infinity();
  auto top = left;
  auto right = -left;
  auto bottom = -left;
  auto map = [&] (Point point) {
    auto mapped = to_vector(matrix.map(point));
    left = std::min(left, mapped.m_x);
    top = std::min(top, mapped.m_y);
    right = std::max(right, mapped.m_x);
    bottom = std::max(bottom, mapped.m_y);
    return mapped;
  };
  auto add = [&] (CurveType type, Vector first, Vector second,
      Vector third = Vector(0, 0), Vector fourth = Vector(0, 0)) {
    if(contours.empty()) {
      contours.emplace_back();
      closed.push_back(false);
    }
    auto piece = Piece();
    piece.m_type = type;
    piece.m_points = std::to_array({first, second, third, fourth});
    contours.back().push_back(piece);
  };
  for(auto& segment : line.get_segments()) {
    auto& points = segment.m_points;
    if(segment.m_type == SegmentType::MOVE) {
      start = map(points[0]);
      current = start;
      source = points[0];
      contours.emplace_back();
      closed.push_back(false);
    } else if(segment.m_type == SegmentType::LINE) {
      auto end = map(points[0]);
      add(CurveType::LINE, current, end);
      current = end;
      source = points[0];
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      auto end = map(points[1]);
      add(CurveType::QUADRATIC, current, map(points[0]), end);
      current = end;
      source = points[1];
    } else if(segment.m_type == SegmentType::CUBIC) {
      auto end = map(points[2]);
      if(segment.m_is_arc) {
        add(CurveType::CONIC, current,
          map(source + (points[0] - source) / ARC_CONTROL), end);
      } else {
        add(CurveType::CUBIC, current, map(points[0]), map(points[1]), end);
      }
      current = end;
      source = points[2];
    } else {
      add(CurveType::LINE, current, start);
      current = start;
      source = points[0];
      if(!closed.empty()) {
        closed.back() = true;
      }
    }
  }
  if(!(left >= -HAIRLINE_LIMIT && right <= HAIRLINE_LIMIT &&
      top >= -HAIRLINE_LIMIT && bottom <= HAIRLINE_LIMIT)) {
    return;
  }
  auto bounds = QRect(QPoint(static_cast<int>(std::floor(left)) - 2,
    static_cast<int>(std::floor(top)) - 2), QPoint(
      static_cast<int>(std::ceil(right)) + 1,
      static_cast<int>(std::ceil(bottom)) + 1)) & clip;
  if(bounds.isEmpty()) {
    return;
  }
  buffers.m_bounds = bounds;
  buffers.m_coverage.assign(
    static_cast<std::size_t>(bounds.width()) * bounds.height(), 0);
  auto blitter = HairlineBlitter(bounds, buffers.m_coverage);
  auto outset = 0.0f;
  if(cap == StrokeCap::SQUARE) {
    outset = 0.5f;
  } else if(cap == StrokeCap::ROUND) {
    outset = static_cast<float>(std::numbers::pi / 8);
  }
  auto& points = buffers.m_quadratics;
  auto quadratics = std::vector<Vector>();
  for(auto i = std::size_t(0); i != contours.size(); ++i) {
    auto& pieces = contours[i];
    if(pieces.empty()) {
      continue;
    }
    if(outset > 0 && !closed[i]) {
      extend(pieces.front(), true, outset);
      extend(pieces.back(), false, outset);
    }
    for(auto& piece : pieces) {
      points.clear();
      points.push_back(piece.m_points[0]);
      if(piece.m_type == CurveType::LINE) {
        points.push_back(piece.m_points[1]);
      } else if(piece.m_type == CurveType::QUADRATIC) {
        add_quadratic_lines(piece.m_points.data(), points);
      } else if(piece.m_type == CurveType::CONIC) {
        quadratics.clear();
        quadratics.push_back(piece.m_points[0]);
        auto conic = make_conic(piece.m_points[0], piece.m_points[1],
          piece.m_points[2], ARC_WEIGHT);
        subdivide(conic, get_conic_power(conic), quadratics);
        for(auto j = std::size_t(0); j + 2 < quadratics.size(); j += 2) {
          add_quadratic_lines(quadratics.data() + j, points);
        }
      } else {
        add_cubic_lines(piece.m_points.data(), points);
      }
      for(auto j = std::size_t(1); j < points.size(); ++j) {
        blitter.draw(static_cast<std::int64_t>(points[j - 1].m_x * 64),
          static_cast<std::int64_t>(points[j - 1].m_y * 64),
          static_cast<std::int64_t>(points[j].m_x * 64),
          static_cast<std::int64_t>(points[j].m_y * 64));
      }
    }
  }
}

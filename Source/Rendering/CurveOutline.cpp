/*
 * Ported from Skia's SkStroke.cpp, SkStrokerPriv.cpp, SkGeometry.cpp,
 * SkPath.cpp, SkPathMakers.h and SkPoint.cpp.
 *
 * Copyright 2006 The Android Open Source Project
 * Copyright 2008 The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *   * Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>
#include "CurveOutline.hpp"

using namespace Rellu;

namespace {
  using Verb = CurveOutline::Verb;
  using Vertex = CurveOutline::Vertex;

  constexpr auto NEARLY_ZERO = 1.0f / (1 << 12);
  constexpr auto ROOT_2_OVER_2 = 0.707106781f;
  constexpr auto ROOT_2 = 1.41421356f;
  constexpr auto PI = 3.14159265f;
  constexpr auto TANGENT_LIMIT = 15;
  constexpr auto CUBIC_LIMIT = 24;
  constexpr auto CONIC_LIMIT = 33;
  constexpr auto QUADRATIC_LIMIT = 33;

  Vertex operator +(Vertex left, Vertex right) {
    return Vertex(left.m_x + right.m_x, left.m_y + right.m_y);
  }

  Vertex operator -(Vertex left, Vertex right) {
    return Vertex(left.m_x - right.m_x, left.m_y - right.m_y);
  }

  Vertex operator -(Vertex vertex) {
    return Vertex(-vertex.m_x, -vertex.m_y);
  }

  Vertex operator *(Vertex vertex, float scalar) {
    return Vertex(vertex.m_x * scalar, vertex.m_y * scalar);
  }

  Vertex operator *(float scalar, Vertex vertex) {
    return Vertex(scalar * vertex.m_x, scalar * vertex.m_y);
  }

  bool operator ==(Vertex left, Vertex right) {
    return left.m_x == right.m_x && left.m_y == right.m_y;
  }

  float dot(Vertex left, Vertex right) {
    return left.m_x * right.m_x + left.m_y * right.m_y;
  }

  float cross(Vertex left, Vertex right) {
    return left.m_x * right.m_y - left.m_y * right.m_x;
  }

  Vertex twice(Vertex vertex) {
    return vertex + vertex;
  }

  float get_distance_squared(Vertex point, Vertex origin) {
    auto x = point.m_x - origin.m_x;
    auto y = point.m_y - origin.m_y;
    return x * x + y * y;
  }

  bool can_normalize(Vertex vector) {
    return std::isfinite(vector.m_x) && std::isfinite(vector.m_y) &&
      (vector.m_x != 0 || vector.m_y != 0);
  }

  bool is_degenerate(Vertex vector) {
    return !can_normalize(vector);
  }

  bool is_nearly_zero(float value, float tolerance = NEARLY_ZERO) {
    return std::abs(value) <= tolerance;
  }

  bool set_length(Vertex& vector, float x, float y, float length) {
    auto magnitude =
      std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y);
    auto scale = length / magnitude;
    x = static_cast<float>(x * scale);
    y = static_cast<float>(y * scale);
    if(!std::isfinite(x) || !std::isfinite(y) || (x == 0 && y == 0)) {
      vector = Vertex(0, 0);
      return false;
    }
    vector = Vertex(x, y);
    return true;
  }

  bool set_length(Vertex& vector, float length) {
    return set_length(vector, vector.m_x, vector.m_y, length);
  }

  Vertex rotate_counterclockwise(Vertex vector) {
    return Vertex(vector.m_y, -vector.m_x);
  }

  Vertex rotate_clockwise(Vertex vector) {
    return Vertex(-vector.m_y, vector.m_x);
  }

  int get_point_count(Verb verb) {
    if(verb == Verb::MOVE || verb == Verb::LINE) {
      return 1;
    } else if(verb == Verb::QUADRATIC || verb == Verb::CONIC) {
      return 2;
    } else if(verb == Verb::CUBIC) {
      return 3;
    }
    return 0;
  }

  struct Builder {
    CurveOutline m_outline;
    int m_last_move = -1;

    int get_count() const {
      return static_cast<int>(m_outline.m_points.size());
    }

    void move_to(Vertex point) {
      m_last_move = get_count();
      m_outline.m_verbs.push_back(Verb::MOVE);
      m_outline.m_points.push_back(point);
    }

    void inject_move() {
      if(m_last_move < 0) {
        auto point = Vertex(0, 0);
        if(!m_outline.m_verbs.empty()) {
          point = m_outline.m_points[~m_last_move];
        }
        move_to(point);
      }
    }

    void line_to(Vertex point) {
      inject_move();
      m_outline.m_verbs.push_back(Verb::LINE);
      m_outline.m_points.push_back(point);
    }

    void quadratic_to(Vertex control, Vertex end) {
      inject_move();
      m_outline.m_verbs.push_back(Verb::QUADRATIC);
      m_outline.m_points.push_back(control);
      m_outline.m_points.push_back(end);
    }

    void conic_to(Vertex control, Vertex end, float weight) {
      if(!(weight > 0)) {
        line_to(end);
      } else if(!std::isfinite(weight)) {
        line_to(control);
        line_to(end);
      } else if(weight == 1) {
        quadratic_to(control, end);
      } else {
        inject_move();
        m_outline.m_verbs.push_back(Verb::CONIC);
        m_outline.m_points.push_back(control);
        m_outline.m_points.push_back(end);
        m_outline.m_weights.push_back(weight);
      }
    }

    void cubic_to(Vertex first, Vertex second, Vertex end) {
      inject_move();
      m_outline.m_verbs.push_back(Verb::CUBIC);
      m_outline.m_points.push_back(first);
      m_outline.m_points.push_back(second);
      m_outline.m_points.push_back(end);
    }

    void close() {
      if(!m_outline.m_verbs.empty() &&
          m_outline.m_verbs.back() != Verb::CLOSE) {
        m_outline.m_verbs.push_back(Verb::CLOSE);
      }
      if(m_last_move >= 0) {
        m_last_move = ~m_last_move;
      }
    }

    Vertex get_last() const {
      if(m_outline.m_points.empty()) {
        return Vertex(0, 0);
      }
      return m_outline.m_points.back();
    }

    void set_last(Vertex point) {
      if(m_outline.m_points.empty()) {
        move_to(point);
      } else {
        m_outline.m_points.back() = point;
      }
    }

    bool is_zero_length_since(int start) const {
      auto count = get_count() - start;
      if(count < 2) {
        return true;
      }
      auto& first = m_outline.m_points[start];
      for(auto i = 1; i < count; ++i) {
        if(!(first == m_outline.m_points[start + i])) {
          return false;
        }
      }
      return true;
    }

    void reverse_path_to(const Builder& path) {
      auto& verbs = path.m_outline.m_verbs;
      auto& points = path.m_outline.m_points;
      if(verbs.empty()) {
        return;
      }
      auto index = path.get_count() - 1;
      auto weight = path.m_outline.m_weights.size();
      for(auto i = verbs.size(); i-- > 0;) {
        auto verb = verbs[i];
        index -= get_point_count(verb);
        if(verb == Verb::MOVE) {
          return;
        } else if(verb == Verb::LINE) {
          line_to(points[index]);
        } else if(verb == Verb::QUADRATIC) {
          quadratic_to(points[index + 1], points[index]);
        } else if(verb == Verb::CONIC) {
          conic_to(points[index + 1], points[index],
            path.m_outline.m_weights[--weight]);
        } else if(verb == Verb::CUBIC) {
          cubic_to(points[index + 2], points[index + 1], points[index]);
        }
      }
    }

    void add_path(const Builder& path) {
      m_last_move = get_count() + path.m_last_move;
      auto& source = path.m_outline;
      m_outline.m_verbs.insert(m_outline.m_verbs.end(), source.m_verbs.begin(),
        source.m_verbs.end());
      m_outline.m_points.insert(m_outline.m_points.end(),
        source.m_points.begin(), source.m_points.end());
      m_outline.m_weights.insert(m_outline.m_weights.end(),
        source.m_weights.begin(), source.m_weights.end());
    }

    void add_circle(Vertex center, float radius) {
      if(!(radius > 0)) {
        return;
      }
      auto left = center.m_x - radius;
      auto top = center.m_y - radius;
      auto right = center.m_x + radius;
      auto bottom = center.m_y + radius;
      auto x = left * 0.5f + right * 0.5f;
      auto y = top * 0.5f + bottom * 0.5f;
      auto oval = std::to_array({Vertex(x, top), Vertex(right, y),
        Vertex(x, bottom), Vertex(left, y)});
      auto corners = std::to_array({Vertex(left, top), Vertex(right, top),
        Vertex(right, bottom), Vertex(left, bottom)});
      move_to(oval[1]);
      for(auto i = 0; i != 4; ++i) {
        conic_to(corners[(i + 2) % 4], oval[(i + 2) % 4], ROOT_2_OVER_2);
      }
      close();
    }

    void add_rectangle(float left, float top, float right, float bottom,
        bool is_clockwise) {
      auto corners = std::to_array({Vertex(left, top), Vertex(right, top),
        Vertex(right, bottom), Vertex(left, bottom)});
      auto advance = is_clockwise ? 1 : 3;
      auto index = 0;
      move_to(corners[index]);
      for(auto i = 0; i != 3; ++i) {
        index = (index + advance) % 4;
        line_to(corners[index]);
      }
      close();
    }

    void add_polygon(const std::array<Vertex, 8>& points) {
      m_last_move = get_count();
      m_outline.m_verbs.push_back(Verb::MOVE);
      m_outline.m_points.push_back(points[0]);
      for(auto i = std::size_t(1); i != points.size(); ++i) {
        m_outline.m_verbs.push_back(Verb::LINE);
        m_outline.m_points.push_back(points[i]);
      }
      m_outline.m_verbs.push_back(Verb::CLOSE);
      m_last_move = ~m_last_move;
    }

    void add_round_rectangle(float left, float top, float right, float bottom,
        float radius, bool is_clockwise) {
      auto start = is_clockwise ? 6 : 7;
      auto points = std::to_array({Vertex(left + radius, top),
        Vertex(right - radius, top), Vertex(right, top + radius),
        Vertex(right, bottom - radius), Vertex(right - radius, bottom),
        Vertex(left + radius, bottom), Vertex(left, bottom - radius),
        Vertex(left, top + radius)});
      auto corners = std::to_array({Vertex(left, top), Vertex(right, top),
        Vertex(right, bottom), Vertex(left, bottom)});
      auto advance = is_clockwise ? 1 : 7;
      auto corner_advance = is_clockwise ? 1 : 3;
      auto index = start;
      auto corner = (start / 2 + (is_clockwise ? 0 : 1)) % 4;
      auto next_point = [&] {
        index = (index + advance) % 8;
        return points[index];
      };
      auto next_corner = [&] {
        corner = (corner + corner_advance) % 4;
        return corners[corner];
      };
      move_to(points[index]);
      if((start & 1) == (is_clockwise ? 1 : 0)) {
        for(auto i = 0; i != 3; ++i) {
          auto control = next_corner();
          conic_to(control, next_point(), ROOT_2_OVER_2);
          line_to(next_point());
        }
        auto control = next_corner();
        conic_to(control, next_point(), ROOT_2_OVER_2);
      } else {
        for(auto i = 0; i != 4; ++i) {
          line_to(next_point());
          auto control = next_corner();
          conic_to(control, next_point(), ROOT_2_OVER_2);
        }
      }
      close();
    }

    void rewind() {
      m_outline.m_verbs.clear();
      m_outline.m_points.clear();
      m_outline.m_weights.clear();
      m_last_move = -1;
    }
  };

  struct Conic {
    std::array<Vertex, 3> m_points;
    float m_weight;

    Vertex evaluate(float t) const {
      auto& p = m_points;
      auto weighted = p[1] * m_weight;
      auto a = p[2] - twice(weighted) + p[0];
      auto b = twice(weighted - p[0]);
      auto c = p[0];
      auto denominator_b = 2 * (m_weight - 1);
      auto denominator_a = 0 - denominator_b;
      auto numerator = (a * t + b) * t + c;
      auto denominator = (denominator_a * t + denominator_b) * t + 1;
      return Vertex(
        numerator.m_x / denominator, numerator.m_y / denominator);
    }

    Vertex evaluate_tangent(float t) const {
      auto& p = m_points;
      if((t == 0 && p[0] == p[1]) || (t == 1 && p[1] == p[2])) {
        return p[2] - p[0];
      }
      auto p20 = p[2] - p[0];
      auto p10 = p[1] - p[0];
      auto c = m_weight * p10;
      auto a = m_weight * p20 - p20;
      auto b = p20 - c - c;
      return (a * t + b) * t + c;
    }
  };

  Vertex evaluate_quadratic(const Vertex* points, float t) {
    auto c = points[0];
    auto b = twice(points[1] - c);
    auto a = points[2] - twice(points[1]) + c;
    return (a * t + b) * t + c;
  }

  Vertex evaluate_quadratic_tangent(const Vertex* points, float t) {
    if((t == 0 && points[0] == points[1]) ||
        (t == 1 && points[1] == points[2])) {
      return points[2] - points[0];
    }
    auto b = points[1] - points[0];
    auto a = points[2] - points[1] - b;
    auto tangent = a * t + b;
    return tangent + tangent;
  }

  Vertex evaluate_cubic(const Vertex* points, float t) {
    auto a = points[3] + 3.0f * (points[1] - points[2]) - points[0];
    auto b = 3.0f * (points[2] - twice(points[1]) + points[0]);
    auto c = 3.0f * (points[1] - points[0]);
    auto d = points[0];
    return ((a * t + b) * t + c) * t + d;
  }

  Vertex evaluate_cubic_derivative(const Vertex* points, float t) {
    auto a = points[3] + 3 * (points[1] - points[2]) - points[0];
    auto b = twice(points[2] - twice(points[1]) + points[0]);
    auto c = points[1] - points[0];
    return (a * t + b) * t + c;
  }

  Vertex evaluate_cubic_tangent(const Vertex* points, float t) {
    if((t == 0 && points[0] == points[1]) ||
        (t == 1 && points[2] == points[3])) {
      auto tangent = Vertex();
      if(t == 0) {
        tangent = points[2] - points[0];
      } else {
        tangent = points[3] - points[1];
      }
      if(tangent.m_x == 0 && tangent.m_y == 0) {
        tangent = points[3] - points[0];
      }
      return tangent;
    }
    return evaluate_cubic_derivative(points, t);
  }

  Vertex mix(Vertex start, Vertex end, float t) {
    return (end - start) * t + start;
  }

  std::array<Vertex, 7> chop_cubic(const Vertex* points, float t) {
    if(t == 1) {
      return std::to_array({points[0], points[1], points[2], points[3],
        points[3], points[3], points[3]});
    }
    auto ab = mix(points[0], points[1], t);
    auto bc = mix(points[1], points[2], t);
    auto cd = mix(points[2], points[3], t);
    auto abc = mix(ab, bc, t);
    auto bcd = mix(bc, cd, t);
    return std::to_array(
      {points[0], ab, abc, mix(abc, bcd, t), bcd, cd, points[3]});
  }

  int divide_unit(float numerator, float denominator, float* ratio) {
    if(numerator < 0) {
      numerator = -numerator;
      denominator = -denominator;
    }
    if(denominator == 0 || numerator == 0 || numerator >= denominator) {
      return 0;
    }
    auto value = numerator / denominator;
    if(std::isnan(value) || value == 0) {
      return 0;
    }
    *ratio = value;
    return 1;
  }

  int find_unit_quadratic_roots(float a, float b, float c, float* roots) {
    if(a == 0) {
      return divide_unit(-c, b, roots);
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
    auto count = divide_unit(q, a, roots);
    count += divide_unit(c, q, roots + count);
    if(count == 2) {
      if(roots[0] > roots[1]) {
        std::swap(roots[0], roots[1]);
      } else if(roots[0] == roots[1]) {
        count = 1;
      }
    }
    return count;
  }

  float find_quadratic_maximum_curvature(const Vertex* points) {
    auto ax = points[1].m_x - points[0].m_x;
    auto ay = points[1].m_y - points[0].m_y;
    auto bx = points[0].m_x - points[1].m_x - points[1].m_x + points[2].m_x;
    auto by = points[0].m_y - points[1].m_y - points[1].m_y + points[2].m_y;
    auto numerator = -(ax * bx + ay * by);
    auto denominator = bx * bx + by * by;
    if(denominator < 0) {
      numerator = -numerator;
      denominator = -denominator;
    }
    if(numerator <= 0) {
      return 0;
    }
    if(numerator >= denominator) {
      return 1;
    }
    return numerator / denominator;
  }

  int collapse_duplicates(float* values, int count) {
    auto array = values;
    for(auto n = count; n > 1; --n) {
      if(array[0] == array[1]) {
        for(auto i = 1; i < n; ++i) {
          array[i - 1] = array[i];
        }
        count -= 1;
      } else {
        array += 1;
      }
    }
    return count;
  }

  int solve_cubic(const float* coefficients, float* values) {
    if(is_nearly_zero(coefficients[0])) {
      return find_unit_quadratic_roots(
        coefficients[1], coefficients[2], coefficients[3], values);
    }
    auto inverse = 1.0f / coefficients[0];
    auto a = coefficients[1] * inverse;
    auto b = coefficients[2] * inverse;
    auto c = coefficients[3] * inverse;
    auto q = (a * a - b * 3) / 9;
    auto r = (2 * a * a * a - 9 * a * b + 27 * c) / 54;
    auto q3 = q * q * q;
    auto discriminant = r * r - q3;
    auto third = a / 3;
    if(discriminant < 0) {
      auto theta =
        std::acos(std::clamp(r / std::sqrt(q3), -1.0f, 1.0f));
      auto root = -2 * std::sqrt(q);
      values[0] = std::clamp(root * std::cos(theta / 3) - third, 0.0f, 1.0f);
      values[1] = std::clamp(
        root * std::cos((theta + 2 * PI) / 3) - third, 0.0f, 1.0f);
      values[2] = std::clamp(
        root * std::cos((theta - 2 * PI) / 3) - third, 0.0f, 1.0f);
      for(auto i = 2; i > 0; --i) {
        for(auto j = i; j > 0; --j) {
          if(values[j] < values[j - 1]) {
            std::swap(values[j], values[j - 1]);
          }
        }
      }
      return collapse_duplicates(values, 3);
    }
    auto value = std::abs(r) + std::sqrt(discriminant);
    value = std::pow(value, 0.3333333f);
    if(r > 0) {
      value = -value;
    }
    if(value != 0) {
      value += q / value;
    }
    values[0] = std::clamp(value - third, 0.0f, 1.0f);
    return 1;
  }

  std::array<float, 4> get_curvature_coefficients(
      float p0, float p1, float p2, float p3) {
    auto a = p1 - p0;
    auto b = p2 - 2 * p1 + p0;
    auto c = p3 + 3 * (p1 - p2) - p0;
    return std::to_array({c * c, 3 * b * c, 2 * b * b + c * a, a * b});
  }

  int find_cubic_maximum_curvature(const Vertex* points, float* values) {
    auto x = get_curvature_coefficients(
      points[0].m_x, points[1].m_x, points[2].m_x, points[3].m_x);
    auto y = get_curvature_coefficients(
      points[0].m_y, points[1].m_y, points[2].m_y, points[3].m_y);
    for(auto i = 0; i != 4; ++i) {
      x[i] += y[i];
    }
    return solve_cubic(x.data(), values);
  }

  int find_cubic_inflections(const Vertex* points, float* values) {
    auto ax = points[1].m_x - points[0].m_x;
    auto ay = points[1].m_y - points[0].m_y;
    auto bx = points[2].m_x - 2 * points[1].m_x + points[0].m_x;
    auto by = points[2].m_y - 2 * points[1].m_y + points[0].m_y;
    auto cx = points[3].m_x + 3 * (points[1].m_x - points[2].m_x) -
      points[0].m_x;
    auto cy = points[3].m_y + 3 * (points[1].m_y - points[2].m_y) -
      points[0].m_y;
    return find_unit_quadratic_roots(
      bx * cy - by * cx, ax * cy - ay * cx, ax * by - ay * bx, values);
  }

  bool is_same_side(const Vertex* points, int test, int line) {
    auto origin = points[line];
    auto direction = points[line + 1] - origin;
    auto first = cross(direction, points[test] - origin);
    auto second = cross(direction, points[test + 1] - origin);
    return first * second >= 0;
  }

  float find_cubic_cusp(const Vertex* points) {
    if(points[0] == points[1] || points[2] == points[3]) {
      return -1;
    }
    if(is_same_side(points, 0, 2) || is_same_side(points, 2, 0)) {
      return -1;
    }
    auto values = std::array<float, 3>();
    auto count = find_cubic_maximum_curvature(points, values.data());
    for(auto i = 0; i < count; ++i) {
      auto t = values[i];
      if(0 >= t || t >= 1) {
        continue;
      }
      auto derivative = evaluate_cubic_derivative(points, t);
      auto magnitude = dot(derivative, derivative);
      auto precision = (get_distance_squared(points[1], points[0]) +
        get_distance_squared(points[2], points[1]) +
        get_distance_squared(points[3], points[2])) * 1e-8f;
      if(magnitude < precision) {
        return t;
      }
    }
    return -1;
  }

  int build_unit_arc(Vertex start, Vertex stop, bool is_clockwise,
      float radius, Vertex pivot, std::array<Conic, 5>& conics) {
    auto x = dot(start, stop);
    auto y = cross(start, stop);
    auto absolute_y = std::abs(y);
    if(absolute_y <= NEARLY_ZERO && x > 0 &&
        ((y >= 0 && is_clockwise) || (y <= 0 && !is_clockwise))) {
      return 0;
    }
    if(!is_clockwise) {
      y = -y;
    }
    auto quadrant = 0;
    if(y == 0) {
      quadrant = 2;
    } else if(x == 0) {
      quadrant = y > 0 ? 1 : 3;
    } else {
      if(y < 0) {
        quadrant += 2;
      }
      if((x < 0) != (y < 0)) {
        quadrant += 1;
      }
    }
    static const auto QUADRANTS = std::to_array({Vertex(1, 0), Vertex(1, 1),
      Vertex(0, 1), Vertex(-1, 1), Vertex(-1, 0), Vertex(-1, -1),
      Vertex(0, -1), Vertex(1, -1)});
    auto count = quadrant;
    for(auto i = 0; i < count; ++i) {
      conics[i] = Conic(std::to_array({QUADRANTS[i * 2], QUADRANTS[i * 2 + 1],
        QUADRANTS[(i * 2 + 2) % 8]}), ROOT_2_OVER_2);
    }
    auto end = Vertex(x, y);
    auto last = QUADRANTS[quadrant * 2];
    auto cosine = dot(last, end);
    if(cosine < 1) {
      auto control = Vertex(last.m_x + x, last.m_y + y);
      auto half_cosine = std::sqrt((1 + cosine) / 2);
      set_length(control, 1.0f / half_cosine);
      if(can_normalize(last - control)) {
        conics[count] =
          Conic(std::to_array({last, control, end}), half_cosine);
        ++count;
      }
    }
    auto sine = start.m_y;
    auto cosine_start = start.m_x;
    auto a = cosine_start;
    auto b = -sine;
    auto c = sine;
    auto d = cosine_start;
    if(!is_clockwise) {
      b = -b;
      d = -d;
    }
    a *= radius;
    b *= radius;
    c *= radius;
    d *= radius;
    for(auto i = 0; i < count; ++i) {
      for(auto& point : conics[i].m_points) {
        point = Vertex(a * point.m_x + b * point.m_y + pivot.m_x,
          c * point.m_x + d * point.m_y + pivot.m_y);
      }
    }
    return count;
  }

  bool is_clockwise(Vertex before, Vertex after) {
    return before.m_x * after.m_y > before.m_y * after.m_x;
  }

  enum class Angle {
    NEARLY_180,
    SHARP,
    SHALLOW,
    NEARLY_LINE
  };

  Angle to_angle(float cosine) {
    if(cosine >= 0) {
      if(is_nearly_zero(1 - cosine)) {
        return Angle::NEARLY_LINE;
      }
      return Angle::SHALLOW;
    }
    if(is_nearly_zero(1 + cosine)) {
      return Angle::NEARLY_180;
    }
    return Angle::SHARP;
  }

  void join_inner(Builder& inner, Vertex pivot, Vertex after) {
    inner.line_to(pivot);
    inner.line_to(pivot - after);
  }

  void join_bevel(Builder* outer, Builder* inner, Vertex before_normal,
      Vertex pivot, Vertex after_normal, float radius) {
    auto after = after_normal * radius;
    if(!is_clockwise(before_normal, after_normal)) {
      std::swap(outer, inner);
      after = -after;
    }
    outer->line_to(pivot + after);
    join_inner(*inner, pivot, after);
  }

  void join_round(Builder* outer, Builder* inner, Vertex before_normal,
      Vertex pivot, Vertex after_normal, float radius) {
    auto cosine = dot(before_normal, after_normal);
    if(to_angle(cosine) == Angle::NEARLY_LINE) {
      return;
    }
    auto before = before_normal;
    auto after = after_normal;
    auto is_clockwise_turn = true;
    if(!is_clockwise(before, after)) {
      std::swap(outer, inner);
      before = -before;
      after = -after;
      is_clockwise_turn = false;
    }
    auto conics = std::array<Conic, 5>();
    auto count =
      build_unit_arc(before, after, is_clockwise_turn, radius, pivot, conics);
    if(count > 0) {
      for(auto i = 0; i < count; ++i) {
        outer->conic_to(conics[i].m_points[1], conics[i].m_points[2],
          conics[i].m_weight);
      }
      after = after * radius;
      join_inner(*inner, pivot, after);
    }
  }

  void join_miter(Builder* outer, Builder* inner, Vertex before_normal,
      Vertex pivot, Vertex after_normal, float radius,
      float inverse_miter_limit, bool is_previous_line, bool is_line) {
    auto cosine = dot(before_normal, after_normal);
    auto angle = to_angle(cosine);
    auto before = before_normal;
    auto after = after_normal;
    if(angle == Angle::NEARLY_LINE) {
      return;
    }
    auto is_blunt = false;
    if(angle == Angle::NEARLY_180) {
      is_line = false;
      is_blunt = true;
    } else {
      auto is_counterclockwise = !is_clockwise(before, after);
      if(is_counterclockwise) {
        std::swap(outer, inner);
        before = -before;
        after = -after;
      }
      auto middle = Vertex(0, 0);
      if(cosine == 0 && inverse_miter_limit <= ROOT_2_OVER_2) {
        middle = (before + after) * radius;
      } else {
        auto half_sine = std::sqrt((1 + cosine) * 0.5f);
        if(half_sine < inverse_miter_limit) {
          is_line = false;
          is_blunt = true;
        } else {
          if(angle == Angle::SHARP) {
            middle = Vertex(
              after.m_y - before.m_y, before.m_x - after.m_x);
            if(is_counterclockwise) {
              middle = -middle;
            }
          } else {
            middle = Vertex(before.m_x + after.m_x, before.m_y + after.m_y);
          }
          set_length(middle, radius / half_sine);
        }
      }
      if(!is_blunt) {
        if(is_previous_line) {
          outer->set_last(pivot + middle);
        } else {
          outer->line_to(pivot + middle);
        }
      }
    }
    after = after * radius;
    if(!is_line) {
      outer->line_to(pivot + after);
    }
    join_inner(*inner, pivot, after);
  }

  void cap(StrokeCap shape, Builder& path, Vertex pivot, Vertex normal,
      Vertex stop, Builder* other) {
    if(shape == StrokeCap::BUTT) {
      path.line_to(stop);
    } else if(shape == StrokeCap::ROUND) {
      auto parallel = rotate_clockwise(normal);
      auto center = pivot + parallel;
      path.conic_to(center + normal, center, ROOT_2_OVER_2);
      path.conic_to(center - normal, stop, ROOT_2_OVER_2);
    } else {
      auto parallel = rotate_clockwise(normal);
      if(other) {
        path.set_last(pivot + normal + parallel);
        path.line_to(pivot - normal + parallel);
      } else {
        path.line_to(pivot + normal + parallel);
        path.line_to(pivot - normal + parallel);
        path.line_to(stop);
      }
    }
  }

  float get_distance_to_line(Vertex point, Vertex start, Vertex end) {
    auto delta = end - start;
    auto offset = point - start;
    auto numerator = dot(delta, offset);
    auto denominator = dot(delta, delta);
    auto t = numerator / denominator;
    if(t >= 0 && t <= 1) {
      auto hit = Vertex(start.m_x * (1 - t) + end.m_x * t,
        start.m_y * (1 - t) + end.m_y * t);
      return get_distance_squared(hit, point);
    }
    return get_distance_squared(point, start);
  }

  bool is_cubic_in_line(const Vertex* cubic) {
    auto maximum = -1.0f;
    auto first = 0;
    auto second = 0;
    for(auto i = 0; i < 3; ++i) {
      for(auto j = i + 1; j < 4; ++j) {
        auto difference = cubic[j] - cubic[i];
        auto extent =
          std::max(std::abs(difference.m_x), std::abs(difference.m_y));
        if(maximum < extent) {
          first = i;
          second = j;
          maximum = extent;
        }
      }
    }
    auto middle = (1 + (2 >> second)) >> first;
    auto other = first ^ second ^ middle;
    auto slop = maximum * maximum * 0.00001f;
    return get_distance_to_line(cubic[middle], cubic[first], cubic[second]) <=
      slop && get_distance_to_line(
        cubic[other], cubic[first], cubic[second]) <= slop;
  }

  bool is_quadratic_in_line(const Vertex* quadratic) {
    auto maximum = -1.0f;
    auto first = 0;
    auto second = 0;
    for(auto i = 0; i < 2; ++i) {
      for(auto j = i + 1; j < 3; ++j) {
        auto difference = quadratic[j] - quadratic[i];
        auto extent =
          std::max(std::abs(difference.m_x), std::abs(difference.m_y));
        if(maximum < extent) {
          first = i;
          second = j;
          maximum = extent;
        }
      }
    }
    auto middle = first ^ second ^ 3;
    auto slop = maximum * maximum * 0.000005f;
    return get_distance_to_line(
      quadratic[middle], quadratic[first], quadratic[second]) <= slop;
  }

  bool is_sharp_angle(const std::array<Vertex, 3>& quadratic) {
    auto smaller = quadratic[1] - quadratic[0];
    auto larger = quadratic[1] - quadratic[2];
    auto smaller_length = dot(smaller, smaller);
    auto larger_length = dot(larger, larger);
    if(smaller_length > larger_length) {
      std::swap(smaller, larger);
      larger_length = smaller_length;
    }
    if(!set_length(smaller, larger_length)) {
      return false;
    }
    return dot(smaller, larger) > 0;
  }

  bool is_within_distance(Vertex near, Vertex far, float limit) {
    return get_distance_squared(near, far) <= limit * limit;
  }

  int intersect_quadratic_ray(
      const Vertex* line, const Vertex* quadratic, float* roots) {
    auto direction = line[1] - line[0];
    auto r = std::array<float, 3>();
    for(auto n = 0; n < 3; ++n) {
      r[n] = (quadratic[n].m_y - line[0].m_y) * direction.m_x -
        (quadratic[n].m_x - line[0].m_x) * direction.m_y;
    }
    auto a = r[2];
    auto b = r[1];
    auto c = r[0];
    a += c - 2 * b;
    b -= c;
    return find_unit_quadratic_roots(a, 2 * b, c, roots);
  }

  struct QuadConstruct {
    std::array<Vertex, 3> m_quadratic;
    Vertex m_tangent_start;
    Vertex m_tangent_end;
    float m_start_t;
    float m_middle_t;
    float m_end_t;
    bool m_is_start_set;
    bool m_is_end_set;
    bool m_is_opposite;

    bool initialize(float start, float end) {
      m_start_t = start;
      m_middle_t = (start + end) * 0.5f;
      m_end_t = end;
      m_is_start_set = false;
      m_is_end_set = false;
      return m_start_t < m_middle_t && m_middle_t < m_end_t;
    }

    bool initialize_with_start(const QuadConstruct& parent) {
      if(!initialize(parent.m_start_t, parent.m_middle_t)) {
        return false;
      }
      m_quadratic[0] = parent.m_quadratic[0];
      m_tangent_start = parent.m_tangent_start;
      m_is_start_set = true;
      return true;
    }

    bool initialize_with_end(const QuadConstruct& parent) {
      if(!initialize(parent.m_middle_t, parent.m_end_t)) {
        return false;
      }
      m_quadratic[2] = parent.m_quadratic[2];
      m_tangent_end = parent.m_tangent_end;
      m_is_end_set = true;
      return true;
    }
  };

  enum class Result {
    SPLIT,
    DEGENERATE,
    QUADRATIC
  };

  enum class Reduction {
    POINT,
    LINE,
    QUADRATIC,
    DEGENERATE,
    DEGENERATE_2,
    DEGENERATE_3
  };

  Reduction check_conic_linear(const Conic& conic, Vertex& reduction) {
    auto& points = conic.m_points;
    auto is_first_degenerate = is_degenerate(points[1] - points[0]);
    auto is_second_degenerate = is_degenerate(points[2] - points[1]);
    if(is_first_degenerate && is_second_degenerate) {
      return Reduction::POINT;
    }
    if(is_first_degenerate || is_second_degenerate) {
      return Reduction::LINE;
    }
    if(!is_quadratic_in_line(points.data())) {
      return Reduction::QUADRATIC;
    }
    auto t = find_quadratic_maximum_curvature(points.data());
    if(t == 0) {
      return Reduction::LINE;
    }
    reduction = conic.evaluate(t);
    return Reduction::DEGENERATE;
  }

  Reduction check_quadratic_linear(const Vertex* points, Vertex& reduction) {
    auto is_first_degenerate = is_degenerate(points[1] - points[0]);
    auto is_second_degenerate = is_degenerate(points[2] - points[1]);
    if(is_first_degenerate && is_second_degenerate) {
      return Reduction::POINT;
    }
    if(is_first_degenerate || is_second_degenerate) {
      return Reduction::LINE;
    }
    if(!is_quadratic_in_line(points)) {
      return Reduction::QUADRATIC;
    }
    auto t = find_quadratic_maximum_curvature(points);
    if(t == 0 || t == 1) {
      return Reduction::LINE;
    }
    reduction = evaluate_quadratic(points, t);
    return Reduction::DEGENERATE;
  }

  Reduction check_cubic_linear(const Vertex* cubic,
      std::array<Vertex, 3>& reduction, const Vertex*& tangent) {
    auto is_ab_degenerate = is_degenerate(cubic[1] - cubic[0]);
    auto is_bc_degenerate = is_degenerate(cubic[2] - cubic[1]);
    auto is_cd_degenerate = is_degenerate(cubic[3] - cubic[2]);
    if(is_ab_degenerate && is_bc_degenerate && is_cd_degenerate) {
      return Reduction::POINT;
    }
    if(is_ab_degenerate + is_bc_degenerate + is_cd_degenerate == 2) {
      return Reduction::LINE;
    }
    if(!is_cubic_in_line(cubic)) {
      tangent = is_ab_degenerate ? &cubic[2] : &cubic[1];
      return Reduction::QUADRATIC;
    }
    auto values = std::array<float, 3>();
    auto count = find_cubic_maximum_curvature(cubic, values.data());
    auto reduced = 0;
    for(auto i = 0; i < count; ++i) {
      auto t = values[i];
      if(0 >= t || t >= 1) {
        continue;
      }
      reduction[reduced] = evaluate_cubic(cubic, t);
      if(!(reduction[reduced] == cubic[0]) &&
          !(reduction[reduced] == cubic[3])) {
        ++reduced;
      }
    }
    if(reduced == 0) {
      return Reduction::LINE;
    }
    return static_cast<Reduction>(
      static_cast<int>(Reduction::QUADRATIC) + reduced);
  }

  struct Step {
    Verb m_verb;
    std::array<Vertex, 4> m_points;
    float m_weight;
  };

  class Stroker {
    public:
      Stroker(float radius, float miter_limit, StrokeCap cap,
        StrokeJoin join, float resolution_scale);

      bool has_only_move() const;
      Vertex get_move_point() const;
      bool is_current_contour_empty() const;
      void move_to(Vertex point);
      void line_to(Vertex point, const std::vector<Step>* steps = nullptr,
        std::size_t next = 0);
      void quadratic_to(Vertex control, Vertex end);
      void conic_to(Vertex control, Vertex end, float weight);
      void cubic_to(Vertex first, Vertex second, Vertex end);
      void close(bool is_line);
      CurveOutline done(bool is_line);

    private:
      enum class Side : int {
        OUTER = 1,
        INNER = -1
      };
      float m_radius;
      float m_inverse_miter_limit;
      float m_resolution_scale;
      float m_inverse_resolution_scale;
      float m_inverse_resolution_scale_squared;
      Vertex m_first_normal;
      Vertex m_previous_normal;
      Vertex m_first_unit_normal;
      Vertex m_previous_unit_normal;
      Vertex m_first_point;
      Vertex m_previous_point;
      Vertex m_first_outer_point;
      int m_first_outer_index;
      int m_segment_count;
      bool m_is_previous_line;
      StrokeCap m_cap;
      StrokeJoin m_join;
      Builder m_inner;
      Builder m_outer;
      Builder m_cusper;
      Side m_side;
      int m_recursion_depth;
      bool m_has_found_tangents;
      bool m_is_join_completed;

      void join(Builder* outer, Builder* inner, Vertex before, Vertex pivot,
        Vertex after, bool is_previous_line, bool is_line);
      void finish_contour(bool is_closed, bool is_line);
      bool pre_join(Vertex point, Vertex& normal, Vertex& unit_normal,
        bool is_line);
      void post_join(Vertex point, Vertex normal, Vertex unit_normal);
      void add_line(Vertex point, Vertex normal);
      void set_ray(Vertex point, Vertex& derivative, Vertex& on,
        Vertex* tangent) const;
      void conic_ray(const Conic& conic, float t, Vertex& point, Vertex& on,
        Vertex* tangent) const;
      void quadratic_ray(const Vertex* quadratic, float t, Vertex& point,
        Vertex& on, Vertex* tangent) const;
      void cubic_ray(const Vertex* cubic, float t, Vertex& point, Vertex& on,
        Vertex* tangent) const;
      void conic_ends(const Conic& conic, QuadConstruct& construct) const;
      void cubic_ends(const Vertex* cubic, QuadConstruct& construct) const;
      Result intersect_ray(QuadConstruct& construct, bool is_control) const;
      bool is_in_bounds(const std::array<Vertex, 3>& quadratic,
        Vertex point) const;
      Result is_close_enough(const std::array<Vertex, 3>& stroke,
        const std::array<Vertex, 2>& ray, QuadConstruct& construct) const;
      Result compare_conic(const Conic& conic, QuadConstruct& construct) const;
      Result compare_quadratic(
        const Vertex* quadratic, QuadConstruct& construct) const;
      Result compare_cubic(const Vertex* cubic, QuadConstruct& construct);
      void add_degenerate_line(const QuadConstruct& construct);
      bool is_cubic_middle_on_line(
        const Vertex* cubic, const QuadConstruct& construct) const;
      bool stroke_conic(const Conic& conic, QuadConstruct& construct);
      bool stroke_quadratic(
        const Vertex* quadratic, QuadConstruct& construct);
      bool stroke_cubic(const Vertex* cubic, QuadConstruct& construct);
      void initialize(Side side, QuadConstruct& construct, float start,
        float end);
      Builder& get_side_path();
  };

  Stroker::Stroker(float radius, float miter_limit, StrokeCap cap,
      StrokeJoin join, float resolution_scale)
      : m_radius(radius),
        m_inverse_miter_limit(0),
        m_resolution_scale(resolution_scale),
        m_first_outer_index(0),
        m_segment_count(-1),
        m_is_previous_line(false),
        m_cap(cap),
        m_join(join),
        m_side(Side::OUTER),
        m_recursion_depth(0),
        m_has_found_tangents(false),
        m_is_join_completed(false) {
    if(m_join == StrokeJoin::MITER) {
      if(miter_limit <= 1) {
        m_join = StrokeJoin::BEVEL;
      } else {
        m_inverse_miter_limit = 1.0f / miter_limit;
      }
    }
    m_inverse_resolution_scale = 1.0f / (resolution_scale * 4);
    m_inverse_resolution_scale_squared =
      m_inverse_resolution_scale * m_inverse_resolution_scale;
  }

  bool Stroker::has_only_move() const {
    return m_segment_count == 0;
  }

  Vertex Stroker::get_move_point() const {
    return m_first_point;
  }

  bool Stroker::is_current_contour_empty() const {
    return m_inner.is_zero_length_since(0) &&
      m_outer.is_zero_length_since(m_first_outer_index);
  }

  void Stroker::join(Builder* outer, Builder* inner, Vertex before,
      Vertex pivot, Vertex after, bool is_previous_line, bool is_line) {
    if(m_join == StrokeJoin::MITER) {
      join_miter(outer, inner, before, pivot, after, m_radius,
        m_inverse_miter_limit, is_previous_line, is_line);
    } else if(m_join == StrokeJoin::ROUND) {
      join_round(outer, inner, before, pivot, after, m_radius);
    } else {
      join_bevel(outer, inner, before, pivot, after, m_radius);
    }
  }

  bool Stroker::pre_join(
      Vertex point, Vertex& normal, Vertex& unit_normal, bool is_line) {
    auto previous = m_previous_point;
    if(!set_length(unit_normal,
        (point.m_x - previous.m_x) * m_resolution_scale,
        (point.m_y - previous.m_y) * m_resolution_scale, 1)) {
      if(m_cap == StrokeCap::BUTT) {
        return false;
      }
      normal = Vertex(m_radius, 0);
      unit_normal = Vertex(1, 0);
    } else {
      unit_normal = rotate_counterclockwise(unit_normal);
      normal = unit_normal * m_radius;
    }
    if(m_segment_count == 0) {
      m_first_normal = normal;
      m_first_unit_normal = unit_normal;
      m_first_outer_point =
        Vertex(previous.m_x + normal.m_x, previous.m_y + normal.m_y);
      m_outer.move_to(m_first_outer_point);
      m_inner.move_to(
        Vertex(previous.m_x - normal.m_x, previous.m_y - normal.m_y));
    } else {
      join(&m_outer, &m_inner, m_previous_unit_normal, m_previous_point,
        unit_normal, m_is_previous_line, is_line);
    }
    m_is_previous_line = is_line;
    return true;
  }

  void Stroker::post_join(Vertex point, Vertex normal, Vertex unit_normal) {
    m_is_join_completed = true;
    m_previous_point = point;
    m_previous_unit_normal = unit_normal;
    m_previous_normal = normal;
    m_segment_count += 1;
  }

  void Stroker::finish_contour(bool is_closed, bool is_line) {
    if(m_segment_count > 0) {
      if(is_closed) {
        join(&m_outer, &m_inner, m_previous_unit_normal, m_previous_point,
          m_first_unit_normal, m_is_previous_line, is_line);
        m_outer.close();
        auto point = m_inner.get_last();
        m_outer.move_to(point);
        m_outer.reverse_path_to(m_inner);
        m_outer.close();
      } else {
        auto point = m_inner.get_last();
        cap(m_cap, m_outer, m_previous_point, m_previous_normal, point,
          is_line ? &m_inner : nullptr);
        m_outer.reverse_path_to(m_inner);
        cap(m_cap, m_outer, m_first_point, -m_first_normal,
          m_first_outer_point, m_is_previous_line ? &m_inner : nullptr);
        m_outer.close();
      }
      if(!m_cusper.m_outline.m_verbs.empty()) {
        m_outer.add_path(m_cusper);
        m_cusper.rewind();
      }
    }
    m_inner.rewind();
    m_segment_count = -1;
    m_first_outer_index = m_outer.get_count();
  }

  void Stroker::move_to(Vertex point) {
    if(m_segment_count > 0) {
      finish_contour(false, false);
    }
    m_segment_count = 0;
    m_first_point = point;
    m_previous_point = point;
    m_is_join_completed = false;
  }

  void Stroker::add_line(Vertex point, Vertex normal) {
    m_outer.line_to(
      Vertex(point.m_x + normal.m_x, point.m_y + normal.m_y));
    m_inner.line_to(
      Vertex(point.m_x - normal.m_x, point.m_y - normal.m_y));
  }

  bool has_valid_tangent(const std::vector<Step>& steps, std::size_t next) {
    for(auto i = next; i < steps.size(); ++i) {
      auto& step = steps[i];
      auto& points = step.m_points;
      if(step.m_verb == Verb::MOVE || step.m_verb == Verb::CLOSE) {
        return false;
      } else if(step.m_verb == Verb::LINE) {
        if(points[0] == points[1]) {
          continue;
        }
        return true;
      } else if(step.m_verb == Verb::QUADRATIC ||
          step.m_verb == Verb::CONIC) {
        if(points[0] == points[1] && points[0] == points[2]) {
          continue;
        }
        return true;
      } else {
        if(points[0] == points[1] && points[0] == points[2] &&
            points[0] == points[3]) {
          continue;
        }
        return true;
      }
    }
    return false;
  }

  void Stroker::line_to(
      Vertex point, const std::vector<Step>* steps, std::size_t next) {
    auto tolerance = NEARLY_ZERO * m_inverse_resolution_scale;
    auto is_teeny = is_nearly_zero(m_previous_point.m_x - point.m_x,
      tolerance) && is_nearly_zero(m_previous_point.m_y - point.m_y,
        tolerance);
    if(m_cap == StrokeCap::BUTT && is_teeny) {
      return;
    }
    if(is_teeny && (m_is_join_completed ||
        (steps && has_valid_tangent(*steps, next)))) {
      return;
    }
    auto normal = Vertex();
    auto unit_normal = Vertex();
    if(!pre_join(point, normal, unit_normal, true)) {
      return;
    }
    add_line(point, normal);
    post_join(point, normal, unit_normal);
  }

  void Stroker::initialize(
      Side side, QuadConstruct& construct, float start, float end) {
    m_side = side;
    m_has_found_tangents = false;
    construct.initialize(start, end);
  }

  Builder& Stroker::get_side_path() {
    if(m_side == Side::OUTER) {
      return m_outer;
    }
    return m_inner;
  }

  void Stroker::set_ray(
      Vertex point, Vertex& derivative, Vertex& on, Vertex* tangent) const {
    if(!set_length(derivative, m_radius)) {
      derivative = Vertex(m_radius, 0);
    }
    auto flip = static_cast<float>(static_cast<int>(m_side));
    on.m_x = point.m_x + flip * derivative.m_y;
    on.m_y = point.m_y - flip * derivative.m_x;
    if(tangent) {
      tangent->m_x = on.m_x + derivative.m_x;
      tangent->m_y = on.m_y + derivative.m_y;
    }
  }

  void Stroker::conic_ray(const Conic& conic, float t, Vertex& point,
      Vertex& on, Vertex* tangent) const {
    point = conic.evaluate(t);
    auto derivative = conic.evaluate_tangent(t);
    if(derivative.m_x == 0 && derivative.m_y == 0) {
      derivative = conic.m_points[2] - conic.m_points[0];
    }
    set_ray(point, derivative, on, tangent);
  }

  void Stroker::quadratic_ray(const Vertex* quadratic, float t, Vertex& point,
      Vertex& on, Vertex* tangent) const {
    point = evaluate_quadratic(quadratic, t);
    auto derivative = evaluate_quadratic_tangent(quadratic, t);
    if(derivative.m_x == 0 && derivative.m_y == 0) {
      derivative = quadratic[2] - quadratic[0];
    }
    set_ray(point, derivative, on, tangent);
  }

  void Stroker::cubic_ray(const Vertex* cubic, float t, Vertex& point,
      Vertex& on, Vertex* tangent) const {
    point = evaluate_cubic(cubic, t);
    auto derivative = evaluate_cubic_tangent(cubic, t);
    if(derivative.m_x == 0 && derivative.m_y == 0) {
      auto points = cubic;
      auto chopped = std::array<Vertex, 7>();
      if(is_nearly_zero(t)) {
        derivative = cubic[2] - cubic[0];
      } else if(is_nearly_zero(1 - t)) {
        derivative = cubic[3] - cubic[1];
      } else {
        chopped = chop_cubic(cubic, t);
        derivative = chopped[3] - chopped[2];
        if(derivative.m_x == 0 && derivative.m_y == 0) {
          derivative = chopped[3] - chopped[1];
          points = chopped.data();
        }
      }
      if(derivative.m_x == 0 && derivative.m_y == 0) {
        derivative = points[3] - points[0];
      }
    }
    set_ray(point, derivative, on, tangent);
  }

  void Stroker::conic_ends(
      const Conic& conic, QuadConstruct& construct) const {
    if(!construct.m_is_start_set) {
      auto point = Vertex();
      conic_ray(conic, construct.m_start_t, point, construct.m_quadratic[0],
        &construct.m_tangent_start);
      construct.m_is_start_set = true;
    }
    if(!construct.m_is_end_set) {
      auto point = Vertex();
      conic_ray(conic, construct.m_end_t, point, construct.m_quadratic[2],
        &construct.m_tangent_end);
      construct.m_is_end_set = true;
    }
  }

  void Stroker::cubic_ends(
      const Vertex* cubic, QuadConstruct& construct) const {
    if(!construct.m_is_start_set) {
      auto point = Vertex();
      cubic_ray(cubic, construct.m_start_t, point, construct.m_quadratic[0],
        &construct.m_tangent_start);
      construct.m_is_start_set = true;
    }
    if(!construct.m_is_end_set) {
      auto point = Vertex();
      cubic_ray(cubic, construct.m_end_t, point, construct.m_quadratic[2],
        &construct.m_tangent_end);
      construct.m_is_end_set = true;
    }
  }

  Result Stroker::intersect_ray(
      QuadConstruct& construct, bool is_control) const {
    auto& start = construct.m_quadratic[0];
    auto& end = construct.m_quadratic[2];
    auto a = construct.m_tangent_start - start;
    auto b = construct.m_tangent_end - end;
    auto denominator = cross(a, b);
    if(denominator == 0 || !std::isfinite(denominator)) {
      construct.m_is_opposite = dot(a, b) < 0;
      return Result::DEGENERATE;
    }
    construct.m_is_opposite = false;
    auto offset = start - end;
    auto numerator_a = cross(b, offset);
    auto numerator_b = cross(a, offset);
    if((numerator_a >= 0) == (numerator_b >= 0)) {
      auto first = get_distance_to_line(start, end, construct.m_tangent_end);
      auto second =
        get_distance_to_line(end, start, construct.m_tangent_start);
      if(std::max(first, second) <= m_inverse_resolution_scale_squared) {
        return Result::DEGENERATE;
      }
      return Result::SPLIT;
    }
    numerator_a /= denominator;
    auto is_valid = numerator_a > numerator_a - 1;
    if(is_valid) {
      if(is_control) {
        auto& control = construct.m_quadratic[1];
        control.m_x = start.m_x * (1 - numerator_a) +
          construct.m_tangent_start.m_x * numerator_a;
        control.m_y = start.m_y * (1 - numerator_a) +
          construct.m_tangent_start.m_y * numerator_a;
      }
      return Result::QUADRATIC;
    }
    construct.m_is_opposite = dot(a, b) < 0;
    return Result::DEGENERATE;
  }

  bool Stroker::is_in_bounds(
      const std::array<Vertex, 3>& quadratic, Vertex point) const {
    auto tolerance = m_inverse_resolution_scale;
    auto left =
      std::min(std::min(quadratic[0].m_x, quadratic[1].m_x), quadratic[2].m_x);
    if(point.m_x + tolerance < left) {
      return false;
    }
    auto right =
      std::max(std::max(quadratic[0].m_x, quadratic[1].m_x), quadratic[2].m_x);
    if(point.m_x - tolerance > right) {
      return false;
    }
    auto top =
      std::min(std::min(quadratic[0].m_y, quadratic[1].m_y), quadratic[2].m_y);
    if(point.m_y + tolerance < top) {
      return false;
    }
    auto bottom =
      std::max(std::max(quadratic[0].m_y, quadratic[1].m_y), quadratic[2].m_y);
    if(point.m_y - tolerance > bottom) {
      return false;
    }
    return true;
  }

  Result Stroker::is_close_enough(const std::array<Vertex, 3>& stroke,
      const std::array<Vertex, 2>& ray, QuadConstruct& construct) const {
    auto middle = evaluate_quadratic(stroke.data(), 0.5f);
    if(is_within_distance(ray[0], middle, m_inverse_resolution_scale)) {
      if(is_sharp_angle(construct.m_quadratic)) {
        return Result::SPLIT;
      }
      return Result::QUADRATIC;
    }
    if(!is_in_bounds(stroke, ray[0])) {
      return Result::SPLIT;
    }
    auto roots = std::array<float, 2>();
    auto count =
      intersect_quadratic_ray(ray.data(), stroke.data(), roots.data());
    if(count != 1) {
      return Result::SPLIT;
    }
    auto point = evaluate_quadratic(stroke.data(), roots[0]);
    auto error = m_inverse_resolution_scale *
      (1 - std::abs(roots[0] - 0.5f) * 2);
    if(is_within_distance(ray[0], point, error)) {
      if(is_sharp_angle(construct.m_quadratic)) {
        return Result::SPLIT;
      }
      return Result::QUADRATIC;
    }
    return Result::SPLIT;
  }

  Result Stroker::compare_cubic(
      const Vertex* cubic, QuadConstruct& construct) {
    cubic_ends(cubic, construct);
    auto result = intersect_ray(construct, true);
    if(result != Result::QUADRATIC) {
      return result;
    }
    auto ray = std::array<Vertex, 2>();
    cubic_ray(cubic, construct.m_middle_t, ray[1], ray[0], nullptr);
    return is_close_enough(construct.m_quadratic, ray, construct);
  }

  Result Stroker::compare_conic(
      const Conic& conic, QuadConstruct& construct) const {
    conic_ends(conic, construct);
    auto result = intersect_ray(construct, true);
    if(result != Result::QUADRATIC) {
      return result;
    }
    auto ray = std::array<Vertex, 2>();
    conic_ray(conic, construct.m_middle_t, ray[1], ray[0], nullptr);
    return is_close_enough(construct.m_quadratic, ray, construct);
  }

  Result Stroker::compare_quadratic(
      const Vertex* quadratic, QuadConstruct& construct) const {
    if(!construct.m_is_start_set) {
      auto point = Vertex();
      quadratic_ray(quadratic, construct.m_start_t, point,
        construct.m_quadratic[0], &construct.m_tangent_start);
      construct.m_is_start_set = true;
    }
    if(!construct.m_is_end_set) {
      auto point = Vertex();
      quadratic_ray(quadratic, construct.m_end_t, point,
        construct.m_quadratic[2], &construct.m_tangent_end);
      construct.m_is_end_set = true;
    }
    auto result = intersect_ray(construct, true);
    if(result != Result::QUADRATIC) {
      return result;
    }
    auto ray = std::array<Vertex, 2>();
    quadratic_ray(quadratic, construct.m_middle_t, ray[1], ray[0], nullptr);
    return is_close_enough(construct.m_quadratic, ray, construct);
  }

  void Stroker::add_degenerate_line(const QuadConstruct& construct) {
    get_side_path().line_to(construct.m_quadratic[2]);
  }

  bool Stroker::is_cubic_middle_on_line(
      const Vertex* cubic, const QuadConstruct& construct) const {
    auto point = Vertex();
    auto middle = Vertex();
    cubic_ray(cubic, construct.m_middle_t, point, middle, nullptr);
    auto distance = get_distance_to_line(
      middle, construct.m_quadratic[0], construct.m_quadratic[2]);
    return distance < m_inverse_resolution_scale_squared;
  }

  bool Stroker::stroke_cubic(const Vertex* cubic, QuadConstruct& construct) {
    if(!m_has_found_tangents) {
      cubic_ends(cubic, construct);
      auto result = intersect_ray(construct, false);
      if(result != Result::QUADRATIC) {
        if((result == Result::DEGENERATE || is_within_distance(
            construct.m_quadratic[0], construct.m_quadratic[2],
            m_inverse_resolution_scale)) &&
            is_cubic_middle_on_line(cubic, construct)) {
          add_degenerate_line(construct);
          return true;
        }
      } else {
        m_has_found_tangents = true;
      }
    }
    if(m_has_found_tangents) {
      auto result = compare_cubic(cubic, construct);
      if(result == Result::QUADRATIC) {
        auto& stroke = construct.m_quadratic;
        get_side_path().quadratic_to(stroke[1], stroke[2]);
        return true;
      }
      if(result == Result::DEGENERATE) {
        if(!construct.m_is_opposite) {
          add_degenerate_line(construct);
          return true;
        }
      }
    }
    if(!std::isfinite(construct.m_quadratic[2].m_x) ||
        !std::isfinite(construct.m_quadratic[2].m_y)) {
      return false;
    }
    ++m_recursion_depth;
    if(m_recursion_depth >
        (m_has_found_tangents ? CUBIC_LIMIT : TANGENT_LIMIT)) {
      return false;
    }
    auto half = QuadConstruct();
    if(!half.initialize_with_start(construct)) {
      add_degenerate_line(construct);
      --m_recursion_depth;
      return true;
    }
    if(!stroke_cubic(cubic, half)) {
      return false;
    }
    if(!half.initialize_with_end(construct)) {
      add_degenerate_line(construct);
      --m_recursion_depth;
      return true;
    }
    if(!stroke_cubic(cubic, half)) {
      return false;
    }
    --m_recursion_depth;
    return true;
  }

  bool Stroker::stroke_conic(const Conic& conic, QuadConstruct& construct) {
    auto result = compare_conic(conic, construct);
    if(result == Result::QUADRATIC) {
      auto& stroke = construct.m_quadratic;
      get_side_path().quadratic_to(stroke[1], stroke[2]);
      return true;
    }
    if(result == Result::DEGENERATE) {
      add_degenerate_line(construct);
      return true;
    }
    if(++m_recursion_depth > CONIC_LIMIT) {
      return false;
    }
    auto half = QuadConstruct();
    half.initialize_with_start(construct);
    if(!stroke_conic(conic, half)) {
      return false;
    }
    half.initialize_with_end(construct);
    if(!stroke_conic(conic, half)) {
      return false;
    }
    --m_recursion_depth;
    return true;
  }

  bool Stroker::stroke_quadratic(
      const Vertex* quadratic, QuadConstruct& construct) {
    auto result = compare_quadratic(quadratic, construct);
    if(result == Result::QUADRATIC) {
      auto& stroke = construct.m_quadratic;
      get_side_path().quadratic_to(stroke[1], stroke[2]);
      return true;
    }
    if(result == Result::DEGENERATE) {
      add_degenerate_line(construct);
      return true;
    }
    if(++m_recursion_depth > QUADRATIC_LIMIT) {
      return false;
    }
    auto half = QuadConstruct();
    half.initialize_with_start(construct);
    if(!stroke_quadratic(quadratic, half)) {
      return false;
    }
    half.initialize_with_end(construct);
    if(!stroke_quadratic(quadratic, half)) {
      return false;
    }
    --m_recursion_depth;
    return true;
  }

  void Stroker::conic_to(Vertex control, Vertex end, float weight) {
    auto conic =
      Conic(std::to_array({m_previous_point, control, end}), weight);
    auto reduction = Vertex();
    auto type = check_conic_linear(conic, reduction);
    if(type == Reduction::POINT || type == Reduction::LINE) {
      line_to(end);
      return;
    }
    if(type == Reduction::DEGENERATE) {
      line_to(reduction);
      auto saved = m_join;
      m_join = StrokeJoin::ROUND;
      line_to(end);
      m_join = saved;
      return;
    }
    auto normal_ab = Vertex();
    auto unit_ab = Vertex();
    if(!pre_join(control, normal_ab, unit_ab, false)) {
      line_to(end);
      return;
    }
    auto construct = QuadConstruct();
    initialize(Side::OUTER, construct, 0, 1);
    stroke_conic(conic, construct);
    initialize(Side::INNER, construct, 0, 1);
    stroke_conic(conic, construct);
    auto normal_bc = normal_ab;
    auto unit_bc = unit_ab;
    if(set_length(unit_bc, (end.m_x - control.m_x) * m_resolution_scale,
        (end.m_y - control.m_y) * m_resolution_scale, 1)) {
      unit_bc = rotate_counterclockwise(unit_bc);
      normal_bc = unit_bc * m_radius;
    } else {
      normal_bc = normal_ab;
      unit_bc = unit_ab;
    }
    post_join(end, normal_bc, unit_bc);
  }

  void Stroker::quadratic_to(Vertex control, Vertex end) {
    auto quadratic = std::to_array({m_previous_point, control, end});
    auto reduction = Vertex();
    auto type = check_quadratic_linear(quadratic.data(), reduction);
    if(type == Reduction::POINT || type == Reduction::LINE) {
      line_to(end);
      return;
    }
    if(type == Reduction::DEGENERATE) {
      line_to(reduction);
      auto saved = m_join;
      m_join = StrokeJoin::ROUND;
      line_to(end);
      m_join = saved;
      return;
    }
    auto normal_ab = Vertex();
    auto unit_ab = Vertex();
    if(!pre_join(control, normal_ab, unit_ab, false)) {
      line_to(end);
      return;
    }
    auto construct = QuadConstruct();
    initialize(Side::OUTER, construct, 0, 1);
    stroke_quadratic(quadratic.data(), construct);
    initialize(Side::INNER, construct, 0, 1);
    stroke_quadratic(quadratic.data(), construct);
    auto normal_bc = normal_ab;
    auto unit_bc = unit_ab;
    if(set_length(unit_bc, (end.m_x - control.m_x) * m_resolution_scale,
        (end.m_y - control.m_y) * m_resolution_scale, 1)) {
      unit_bc = rotate_counterclockwise(unit_bc);
      normal_bc = unit_bc * m_radius;
    } else {
      normal_bc = normal_ab;
      unit_bc = unit_ab;
    }
    post_join(end, normal_bc, unit_bc);
  }

  void Stroker::cubic_to(Vertex first, Vertex second, Vertex end) {
    auto cubic = std::to_array({m_previous_point, first, second, end});
    auto reduction = std::array<Vertex, 3>();
    auto tangent = static_cast<const Vertex*>(nullptr);
    auto type = check_cubic_linear(cubic.data(), reduction, tangent);
    if(type == Reduction::POINT || type == Reduction::LINE) {
      line_to(end);
      return;
    }
    if(type != Reduction::QUADRATIC) {
      line_to(reduction[0]);
      auto saved = m_join;
      m_join = StrokeJoin::ROUND;
      if(type == Reduction::DEGENERATE_2 || type == Reduction::DEGENERATE_3) {
        line_to(reduction[1]);
      }
      if(type == Reduction::DEGENERATE_3) {
        line_to(reduction[2]);
      }
      line_to(end);
      m_join = saved;
      return;
    }
    auto normal_ab = Vertex();
    auto unit_ab = Vertex();
    if(!pre_join(*tangent, normal_ab, unit_ab, false)) {
      line_to(end);
      return;
    }
    auto values = std::array<float, 2>();
    auto count = find_cubic_inflections(cubic.data(), values.data());
    auto last = 0.0f;
    for(auto i = 0; i <= count; ++i) {
      auto next = i < count ? values[i] : 1.0f;
      auto construct = QuadConstruct();
      initialize(Side::OUTER, construct, last, next);
      stroke_cubic(cubic.data(), construct);
      initialize(Side::INNER, construct, last, next);
      stroke_cubic(cubic.data(), construct);
      last = next;
    }
    auto cusp = find_cubic_cusp(cubic.data());
    if(cusp > 0) {
      m_cusper.add_circle(evaluate_cubic(cubic.data(), cusp), m_radius);
    }
    auto ab = cubic[1] - cubic[0];
    auto cd = cubic[3] - cubic[2];
    auto is_ab_degenerate = is_degenerate(ab);
    auto is_cd_degenerate = is_degenerate(cd);
    auto normal_cd = normal_ab;
    auto unit_cd = unit_ab;
    if(!(is_ab_degenerate && is_cd_degenerate)) {
      if(is_ab_degenerate) {
        ab = cubic[2] - cubic[0];
        is_ab_degenerate = is_degenerate(ab);
      }
      if(is_cd_degenerate) {
        cd = cubic[3] - cubic[1];
        is_cd_degenerate = is_degenerate(cd);
      }
      if(!is_ab_degenerate && !is_cd_degenerate &&
          set_length(unit_cd, cd.m_x, cd.m_y, 1)) {
        unit_cd = rotate_counterclockwise(unit_cd);
        normal_cd = unit_cd * m_radius;
      } else {
        normal_cd = normal_ab;
        unit_cd = unit_ab;
      }
    }
    post_join(end, normal_cd, unit_cd);
  }

  void Stroker::close(bool is_line) {
    finish_contour(true, is_line);
  }

  CurveOutline Stroker::done(bool is_line) {
    finish_contour(false, is_line);
    return std::move(m_outer.m_outline);
  }

  struct Rectangle {
    float m_left;
    float m_top;
    float m_right;
    float m_bottom;
    bool m_is_clockwise;
  };

  int to_direction(float x, float y) {
    return (x != 0 ? 1 : 0) | ((x > 0 || y > 0) ? 2 : 0);
  }

  std::optional<Rectangle> find_rectangle(
      const std::vector<Verb>& verbs, const std::vector<Vertex>& points) {
    auto corners = 0;
    auto line_start = Vertex(0, 0);
    auto first = std::optional<Vertex>();
    auto last = std::optional<Vertex>();
    auto first_corner = Vertex(0, 0);
    auto third_corner = Vertex(0, 0);
    auto directions = std::array<int, 5>({-1, -1, -1, -1, -1});
    auto is_closed_or_moved = false;
    auto is_auto_closed = false;
    auto index = std::size_t(0);
    for(auto verb : verbs) {
      if(verb == Verb::CLOSE || verb == Verb::LINE) {
        if(verb == Verb::CLOSE) {
          is_auto_closed = true;
        } else {
          last = points[index];
        }
        if(!first) {
          return std::nullopt;
        }
        auto line_end = verb == Verb::CLOSE ? *first : points[index++];
        auto delta = line_end - line_start;
        if(delta.m_x != 0 && delta.m_y != 0) {
          return std::nullopt;
        }
        if(!std::isfinite(delta.m_x) || !std::isfinite(delta.m_y)) {
          return std::nullopt;
        }
        if(line_start == line_end) {
          continue;
        }
        auto direction = to_direction(delta.m_x, delta.m_y);
        if(corners == 0) {
          directions[0] = direction;
          corners = 1;
          is_closed_or_moved = false;
          line_start = line_end;
          continue;
        }
        if(is_closed_or_moved) {
          return std::nullopt;
        }
        if(is_auto_closed && direction == directions[0]) {
          continue;
        }
        is_closed_or_moved = is_auto_closed;
        if(directions[corners - 1] == direction) {
          if(corners == 3 && verb == Verb::LINE) {
            third_corner = line_end;
          }
          line_start = line_end;
          continue;
        }
        directions[corners++] = direction;
        if(corners == 2) {
          first_corner = line_start;
        } else if(corners == 3) {
          if((directions[0] ^ directions[2]) != 2) {
            return std::nullopt;
          }
          third_corner = line_end;
        } else if(corners == 4) {
          if((directions[1] ^ directions[3]) != 2) {
            return std::nullopt;
          }
        } else {
          return std::nullopt;
        }
        line_start = line_end;
      } else if(verb == Verb::MOVE) {
        if(corners == 0) {
          first = points[index];
        } else if(first && last) {
          auto close = *first - *last;
          if(close.m_x != 0 && close.m_y != 0) {
            return std::nullopt;
          }
        }
        line_start = points[index++];
        is_closed_or_moved = true;
      } else {
        return std::nullopt;
      }
    }
    if(corners < 3 || corners > 4 || !first || !last) {
      return std::nullopt;
    }
    auto close = *first - *last;
    if(close.m_x != 0 && close.m_y != 0) {
      return std::nullopt;
    }
    if(!is_auto_closed) {
      return std::nullopt;
    }
    return Rectangle(std::min(first_corner.m_x, third_corner.m_x),
      std::min(first_corner.m_y, third_corner.m_y),
      std::max(first_corner.m_x, third_corner.m_x),
      std::max(first_corner.m_y, third_corner.m_y),
      directions[0] == ((directions[1] + 1) & 3));
  }

  CurveOutline stroke_rectangle(const Rectangle& rectangle, float width,
      StrokeJoin join, float miter_limit) {
    auto builder = Builder();
    auto radius = width * 0.5f;
    auto left = rectangle.m_left - radius;
    auto top = rectangle.m_top - radius;
    auto right = rectangle.m_right + radius;
    auto bottom = rectangle.m_bottom + radius;
    if(join == StrokeJoin::MITER && miter_limit < ROOT_2) {
      join = StrokeJoin::BEVEL;
    }
    if(join == StrokeJoin::MITER) {
      builder.add_rectangle(left, top, right, bottom,
        rectangle.m_is_clockwise);
    } else if(join == StrokeJoin::BEVEL) {
      auto points = std::array<Vertex, 8>();
      auto& r = rectangle;
      if(rectangle.m_is_clockwise) {
        points = std::to_array({Vertex(r.m_left, top), Vertex(r.m_right, top),
          Vertex(right, r.m_top), Vertex(right, r.m_bottom),
          Vertex(r.m_right, bottom), Vertex(r.m_left, bottom),
          Vertex(left, r.m_bottom), Vertex(left, r.m_top)});
      } else {
        points = std::to_array({Vertex(left, r.m_top),
          Vertex(left, r.m_bottom), Vertex(r.m_left, bottom),
          Vertex(r.m_right, bottom), Vertex(right, r.m_bottom),
          Vertex(right, r.m_top), Vertex(r.m_right, top),
          Vertex(r.m_left, top)});
      }
      builder.add_polygon(points);
    } else {
      builder.add_round_rectangle(left, top, right, bottom, radius,
        rectangle.m_is_clockwise);
    }
    auto rectangle_width = rectangle.m_right - rectangle.m_left;
    auto rectangle_height = rectangle.m_bottom - rectangle.m_top;
    if(width < std::min(rectangle_width, rectangle_height)) {
      builder.add_rectangle(rectangle.m_left + radius,
        rectangle.m_top + radius, rectangle.m_right - radius,
        rectangle.m_bottom - radius, !rectangle.m_is_clockwise);
    }
    return std::move(builder.m_outline);
  }

  Vertex to_vertex(Point point) {
    return Vertex(static_cast<float>(point.m_x), static_cast<float>(point.m_y));
  }

  std::vector<Step> to_steps(const Path& line, std::vector<Verb>& verbs,
      std::vector<Vertex>& points) {
    auto steps = std::vector<Step>();
    auto move = Vertex(0, 0);
    auto last = Vertex(0, 0);
    auto source = Point();
    auto is_open = false;
    auto close = [&] {
      if(!is_open) {
        return;
      }
      if(!(last == move) && !std::isnan(last.m_x) && !std::isnan(last.m_y) &&
          !std::isnan(move.m_x) && !std::isnan(move.m_y)) {
        steps.push_back(Step(Verb::LINE, std::to_array({last, move,
          Vertex(0, 0), Vertex(0, 0)}), 0));
        last = move;
      }
      steps.push_back(Step(Verb::CLOSE, std::array<Vertex, 4>(), 0));
      is_open = false;
    };
    for(auto& segment : line.get_segments()) {
      auto& p = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        move = to_vertex(p[0]);
        last = move;
        source = p[0];
        is_open = true;
        verbs.push_back(Verb::MOVE);
        points.push_back(move);
        steps.push_back(Step(Verb::MOVE, std::to_array({move, Vertex(0, 0),
          Vertex(0, 0), Vertex(0, 0)}), 0));
      } else if(segment.m_type == SegmentType::LINE) {
        auto end = to_vertex(p[0]);
        verbs.push_back(Verb::LINE);
        points.push_back(end);
        steps.push_back(Step(Verb::LINE, std::to_array({last, end,
          Vertex(0, 0), Vertex(0, 0)}), 0));
        last = end;
        source = p[0];
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        auto control = to_vertex(p[0]);
        auto end = to_vertex(p[1]);
        verbs.push_back(Verb::QUADRATIC);
        points.push_back(control);
        points.push_back(end);
        steps.push_back(Step(Verb::QUADRATIC, std::to_array({last, control,
          end, Vertex(0, 0)}), 0));
        last = end;
        source = p[1];
      } else if(segment.m_type == SegmentType::CUBIC) {
        auto end = to_vertex(p[2]);
        if(segment.m_is_arc) {
          auto control = to_vertex(source + (p[0] - source) / ARC_CONTROL);
          verbs.push_back(Verb::CONIC);
          points.push_back(control);
          points.push_back(end);
          steps.push_back(Step(Verb::CONIC, std::to_array({last, control,
            end, Vertex(0, 0)}), ROOT_2_OVER_2));
        } else {
          auto first = to_vertex(p[0]);
          auto second = to_vertex(p[1]);
          verbs.push_back(Verb::CUBIC);
          points.push_back(first);
          points.push_back(second);
          points.push_back(end);
          steps.push_back(Step(Verb::CUBIC, std::to_array({last, first,
            second, end}), 0));
        }
        last = end;
        source = p[2];
      } else {
        verbs.push_back(Verb::CLOSE);
        close();
        source = p[0];
      }
    }
    return steps;
  }

  float get_resolution_scale(const Matrix& matrix) {
    auto length = [] (double x, double y) {
      auto a = static_cast<float>(x);
      auto b = static_cast<float>(y);
      return std::sqrt(a * a + b * b);
    };
    auto horizontal = length(matrix.m_a, matrix.m_b);
    auto vertical = length(matrix.m_c, matrix.m_d);
    if(std::isfinite(horizontal) && std::isfinite(vertical)) {
      auto scale = std::max(horizontal, vertical);
      if(scale > 0) {
        return scale;
      }
    }
    return 1;
  }

  void map(CurveOutline& outline, const Matrix& matrix) {
    for(auto& point : outline.m_points) {
      auto mapped = matrix.map(Point(point.m_x, point.m_y));
      point = to_vertex(mapped);
    }
  }
}

CurveOutline Rellu::stroke_outline(const Path& line, const Matrix& matrix,
    double width, StrokeCap cap, StrokeJoin join, double miter_limit) {
  auto stroke_width = static_cast<float>(width);
  auto radius = stroke_width * 0.5f;
  if(!(radius > 0)) {
    return CurveOutline();
  }
  auto verbs = std::vector<Verb>();
  auto points = std::vector<Vertex>();
  auto steps = to_steps(line, verbs, points);
  auto limit = static_cast<float>(miter_limit);
  if(auto rectangle = find_rectangle(verbs, points)) {
    auto outline = stroke_rectangle(*rectangle, stroke_width, join, limit);
    map(outline, matrix);
    return outline;
  }
  auto stroker =
    Stroker(radius, limit, cap, join, get_resolution_scale(matrix));
  auto last = Verb::MOVE;
  for(auto i = std::size_t(0); i != steps.size(); ++i) {
    auto& step = steps[i];
    auto& p = step.m_points;
    if(step.m_verb == Verb::MOVE) {
      stroker.move_to(p[0]);
    } else if(step.m_verb == Verb::LINE) {
      stroker.line_to(p[1], &steps, i + 1);
      last = Verb::LINE;
    } else if(step.m_verb == Verb::QUADRATIC) {
      stroker.quadratic_to(p[1], p[2]);
      last = Verb::QUADRATIC;
    } else if(step.m_verb == Verb::CONIC) {
      stroker.conic_to(p[1], p[2], step.m_weight);
      last = Verb::CONIC;
    } else if(step.m_verb == Verb::CUBIC) {
      stroker.cubic_to(p[1], p[2], p[3]);
      last = Verb::CUBIC;
    } else {
      if(cap != StrokeCap::BUTT) {
        if(stroker.has_only_move()) {
          stroker.line_to(stroker.get_move_point());
          last = Verb::LINE;
          continue;
        }
        if(stroker.is_current_contour_empty()) {
          last = Verb::LINE;
          continue;
        }
      }
      stroker.close(last == Verb::LINE);
    }
  }
  auto outline = stroker.done(last == Verb::LINE);
  map(outline, matrix);
  return outline;
}

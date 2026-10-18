#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <system_error>
#include "Rellu/Geometry/SvgPath.hpp"

using namespace Rellu;

namespace {
  bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
  }

  bool is_digit(char c) {
    return c >= '0' && c <= '9';
  }

  bool is_command(char c) {
    switch(c) {
      case 'M': case 'm': case 'L': case 'l': case 'H': case 'h': case 'V':
      case 'v': case 'C': case 'c': case 'S': case 's': case 'Q': case 'q':
      case 'T': case 't': case 'A': case 'a': case 'Z': case 'z':
        return true;
      default:
        return false;
    }
  }

  bool is_relative(char command) {
    return command >= 'a' && command <= 'z';
  }

  char to_absolute(char command) {
    if(is_relative(command)) {
      return static_cast<char>(command - 'a' + 'A');
    }
    return command;
  }

  void add_arc(Path& path, Point start, double x_radius, double y_radius,
      double rotation, bool is_large, bool is_sweep, Point end) {
    if(start == end) {
      return;
    }
    x_radius = std::abs(x_radius);
    y_radius = std::abs(y_radius);
    if(x_radius == 0 || y_radius == 0) {
      path.line_to(end);
      return;
    }
    auto angle = rotation * std::numbers::pi / 180;
    auto cosine = std::cos(angle);
    auto sine = std::sin(angle);
    auto half = (start - end) / 2;
    auto x = cosine * half.m_x + sine * half.m_y;
    auto y = -sine * half.m_x + cosine * half.m_y;
    auto lambda = (x * x) / (x_radius * x_radius) +
      (y * y) / (y_radius * y_radius);
    if(lambda > 1) {
      auto scale = std::sqrt(lambda);
      x_radius *= scale;
      y_radius *= scale;
    }
    auto numerator = x_radius * x_radius * y_radius * y_radius -
      x_radius * x_radius * y * y - y_radius * y_radius * x * x;
    auto denominator =
      x_radius * x_radius * y * y + y_radius * y_radius * x * x;
    auto coefficient = std::sqrt(std::max(0.0, numerator / denominator));
    if(is_large == is_sweep) {
      coefficient = -coefficient;
    }
    auto center_x = coefficient * x_radius * y / y_radius;
    auto center_y = -coefficient * y_radius * x / x_radius;
    auto middle = (start + end) / 2;
    auto center = Point(cosine * center_x - sine * center_y + middle.m_x,
      sine * center_x + cosine * center_y + middle.m_y);
    auto get_angle = [] (Point from, Point to) {
      return std::atan2(cross(from, to), dot(from, to));
    };
    auto from = Point((x - center_x) / x_radius, (y - center_y) / y_radius);
    auto to = Point((-x - center_x) / x_radius, (-y - center_y) / y_radius);
    auto initial = get_angle(Point(1, 0), from);
    auto sweep = get_angle(from, to);
    if(!is_sweep && sweep > 0) {
      sweep -= 2 * std::numbers::pi;
    } else if(is_sweep && sweep < 0) {
      sweep += 2 * std::numbers::pi;
    }
    auto count = std::max(1,
      static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 2) -
        1e-9)));
    auto step = sweep / count;
    auto handle = 4 * std::tan(step / 4) / 3;
    auto map = [&] (Point point) {
      auto scaled = Point(x_radius * point.m_x, y_radius * point.m_y);
      return center + Point(cosine * scaled.m_x - sine * scaled.m_y,
        sine * scaled.m_x + cosine * scaled.m_y);
    };
    for(auto i = 0; i != count; ++i) {
      auto first = initial + i * step;
      auto second = first + step;
      auto departure = Point(std::cos(first), std::sin(first));
      auto arrival = Point(std::cos(second), std::sin(second));
      auto destination = [&] {
        if(i == count - 1) {
          return end;
        }
        return map(arrival);
      }();
      path.cubic_to(
        map(departure + handle * Point(-departure.m_y, departure.m_x)),
        map(arrival - handle * Point(-arrival.m_y, arrival.m_x)), destination);
    }
  }

  class Parser {
    public:
      explicit Parser(std::string_view source) noexcept;

      std::optional<Path> parse();

    private:
      std::string_view m_source;
      std::size_t m_position;
      Path m_path;
      Point m_current;
      Point m_start;
      Point m_control;
      char m_previous;

      bool is_at_end() const;
      char peek() const;
      void skip_whitespace();
      void skip_separator();
      bool is_at_number() const;
      std::optional<double> parse_number();
      std::optional<double> parse_argument(bool is_first);
      std::optional<Point> parse_point(bool is_first, char command);
      std::optional<bool> parse_flag();
      bool apply(char command, bool is_first);
  };

  Parser::Parser(std::string_view source) noexcept
    : m_source(source),
      m_position(0),
      m_current(),
      m_start(),
      m_control(),
      m_previous('\0') {}

  std::optional<Path> Parser::parse() {
    skip_whitespace();
    if(is_at_end()) {
      return m_path;
    }
    if(peek() != 'M' && peek() != 'm') {
      return std::nullopt;
    }
    auto command = '\0';
    while(true) {
      skip_whitespace();
      if(is_at_end()) {
        break;
      }
      auto is_first = false;
      if(is_command(peek())) {
        command = peek();
        ++m_position;
        is_first = true;
      } else {
        if(to_absolute(command) == 'Z') {
          return std::nullopt;
        }
        if(peek() == ',') {
          ++m_position;
          skip_whitespace();
          if(!is_at_number()) {
            return std::nullopt;
          }
        }
        if(command == 'M') {
          command = 'L';
        } else if(command == 'm') {
          command = 'l';
        }
      }
      if(!apply(command, is_first)) {
        return std::nullopt;
      }
    }
    return m_path;
  }

  bool Parser::is_at_end() const {
    return m_position == m_source.size();
  }

  char Parser::peek() const {
    return m_source[m_position];
  }

  void Parser::skip_whitespace() {
    while(!is_at_end() && is_whitespace(peek())) {
      ++m_position;
    }
  }

  void Parser::skip_separator() {
    skip_whitespace();
    if(!is_at_end() && peek() == ',') {
      ++m_position;
      skip_whitespace();
    }
  }

  bool Parser::is_at_number() const {
    if(is_at_end()) {
      return false;
    }
    auto c = peek();
    return is_digit(c) || c == '.' || c == '-' || c == '+';
  }

  std::optional<double> Parser::parse_number() {
    auto start = m_position;
    auto position = m_position;
    auto is_negative = false;
    if(position != m_source.size() &&
        (m_source[position] == '+' || m_source[position] == '-')) {
      is_negative = m_source[position] == '-';
      ++position;
    }
    auto mantissa = position;
    auto digits = 0;
    while(position != m_source.size() && is_digit(m_source[position])) {
      ++position;
      ++digits;
    }
    if(position != m_source.size() && m_source[position] == '.') {
      ++position;
      while(position != m_source.size() && is_digit(m_source[position])) {
        ++position;
        ++digits;
      }
    }
    if(digits == 0) {
      return std::nullopt;
    }
    auto has_negative_exponent = false;
    if(position != m_source.size() &&
        (m_source[position] == 'e' || m_source[position] == 'E')) {
      auto exponent = position + 1;
      auto is_exponent_negative = false;
      if(exponent != m_source.size() &&
          (m_source[exponent] == '+' || m_source[exponent] == '-')) {
        is_exponent_negative = m_source[exponent] == '-';
        ++exponent;
      }
      if(exponent != m_source.size() && is_digit(m_source[exponent])) {
        while(exponent != m_source.size() && is_digit(m_source[exponent])) {
          ++exponent;
        }
        position = exponent;
        has_negative_exponent = is_exponent_negative;
      }
    }
    auto value = 0.0;
    auto result = std::from_chars(m_source.data() + mantissa,
      m_source.data() + position, value);
    if(result.ec == std::errc::result_out_of_range && has_negative_exponent) {
      value = 0;
    } else if(result.ec != std::errc() ||
        result.ptr != m_source.data() + position) {
      m_position = start;
      return std::nullopt;
    }
    m_position = position;
    if(is_negative) {
      return -value;
    }
    return value;
  }

  std::optional<double> Parser::parse_argument(bool is_first) {
    if(is_first) {
      skip_whitespace();
    } else {
      skip_separator();
    }
    return parse_number();
  }

  std::optional<Point> Parser::parse_point(bool is_first, char command) {
    auto x = parse_argument(is_first);
    if(!x) {
      return std::nullopt;
    }
    auto y = parse_argument(false);
    if(!y) {
      return std::nullopt;
    }
    if(is_relative(command)) {
      return m_current + Point(*x, *y);
    }
    return Point(*x, *y);
  }

  std::optional<bool> Parser::parse_flag() {
    skip_separator();
    if(is_at_end()) {
      return std::nullopt;
    }
    auto c = peek();
    if(c != '0' && c != '1') {
      return std::nullopt;
    }
    ++m_position;
    return c == '1';
  }

  bool Parser::apply(char command, bool is_first) {
    auto type = to_absolute(command);
    if(type == 'M') {
      auto point = parse_point(is_first, command);
      if(!point) {
        return false;
      }
      m_path.move_to(*point);
      m_start = *point;
      m_current = *point;
    } else if(type == 'L') {
      auto point = parse_point(is_first, command);
      if(!point) {
        return false;
      }
      m_path.line_to(*point);
      m_current = *point;
    } else if(type == 'H' || type == 'V') {
      auto value = parse_argument(is_first);
      if(!value) {
        return false;
      }
      auto point = m_current;
      if(type == 'H') {
        point.m_x = *value + (is_relative(command) ? m_current.m_x : 0);
      } else {
        point.m_y = *value + (is_relative(command) ? m_current.m_y : 0);
      }
      m_path.line_to(point);
      m_current = point;
    } else if(type == 'C' || type == 'S') {
      auto first = [&] () -> std::optional<Point> {
        if(type == 'C') {
          return parse_point(is_first, command);
        } else if(m_previous == 'C' || m_previous == 'S') {
          return 2 * m_current - m_control;
        }
        return m_current;
      }();
      if(!first) {
        return false;
      }
      auto second = parse_point(is_first && type == 'S', command);
      if(!second) {
        return false;
      }
      auto end = parse_point(false, command);
      if(!end) {
        return false;
      }
      m_path.cubic_to(*first, *second, *end);
      m_control = *second;
      m_current = *end;
    } else if(type == 'Q' || type == 'T') {
      auto control = [&] () -> std::optional<Point> {
        if(type == 'Q') {
          return parse_point(is_first, command);
        } else if(m_previous == 'Q' || m_previous == 'T') {
          return 2 * m_current - m_control;
        }
        return m_current;
      }();
      if(!control) {
        return false;
      }
      auto end = parse_point(is_first && type == 'T', command);
      if(!end) {
        return false;
      }
      m_path.quadratic_to(*control, *end);
      m_control = *control;
      m_current = *end;
    } else if(type == 'A') {
      auto x_radius = parse_argument(is_first);
      if(!x_radius) {
        return false;
      }
      auto y_radius = parse_argument(false);
      if(!y_radius) {
        return false;
      }
      auto rotation = parse_argument(false);
      if(!rotation) {
        return false;
      }
      auto is_large = parse_flag();
      if(!is_large) {
        return false;
      }
      auto is_sweep = parse_flag();
      if(!is_sweep) {
        return false;
      }
      auto end = parse_point(false, command);
      if(!end) {
        return false;
      }
      add_arc(m_path, m_current, *x_radius, *y_radius, *rotation, *is_large,
        *is_sweep, *end);
      m_current = *end;
    } else {
      m_path.close();
      m_current = m_start;
    }
    m_previous = type;
    return true;
  }
}

std::optional<Path> Rellu::parse_svg_path(std::string_view source) {
  return Parser(source).parse();
}

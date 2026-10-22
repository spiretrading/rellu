#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>
#include <QByteArray>
#include "PdfFile.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_SAVINGS = std::size_t(21);
  constexpr auto HEADER = std::string_view("%PDF-1.4\n%\xD3\xEB\xE9\xE1\n");

  std::string to_decimal(int value, int digits) {
    auto factor = 1;
    for(auto i = 0; i != digits; ++i) {
      factor *= 10;
    }
    if(value >= factor || value <= 0) {
      return value > 0 ? "1" : "0";
    }
    auto result = std::string(static_cast<std::size_t>(digits) + 1, '.');
    for(auto i = digits; i > 0; --i) {
      result[static_cast<std::size_t>(i)] = static_cast<char>('0' + value % 10);
      value /= 10;
    }
    auto end = static_cast<std::size_t>(digits);
    while(end > 1 && result[end] == '0') {
      --end;
    }
    result.resize(end + 1);
    return result;
  }
}

std::string Rellu::to_pdf_number(double input) {
  auto value = static_cast<float>(input);
  if(value == std::numeric_limits<float>::infinity()) {
    value = std::numeric_limits<float>::max();
  } else if(value == -std::numeric_limits<float>::infinity()) {
    value = -std::numeric_limits<float>::max();
  }
  if(!std::isfinite(value) || value == 0) {
    return "0";
  }
  auto result = std::string();
  if(value < 0) {
    result.push_back('-');
    value = -value;
  }
  auto binary_exponent = 0;
  std::frexp(value, &binary_exponent);
  constexpr auto LOG2 = 0.3010299956639812;
  auto decimal_exponent =
    static_cast<int>(std::floor(LOG2 * binary_exponent));
  auto shift = decimal_exponent - 8;
  auto power = std::pow(10.0, -shift);
  auto digits = static_cast<std::int64_t>(value * power + 0.5);
  if(digits > 167772159) {
    shift = decimal_exponent - 7;
    digits = static_cast<std::int64_t>(value * (power * 0.1) + 0.5);
  }
  while(digits % 10 == 0) {
    digits /= 10;
    ++shift;
  }
  auto buffer = std::string();
  do {
    buffer.push_back(static_cast<char>('0' + digits % 10));
    digits /= 10;
  } while(digits != 0);
  auto index = static_cast<int>(buffer.size());
  if(shift >= 0) {
    while(index != 0) {
      --index;
      result.push_back(buffer[static_cast<std::size_t>(index)]);
    }
    result.append(static_cast<std::size_t>(shift), '0');
  } else {
    auto before = index + shift;
    if(before > 0) {
      while(before-- > 0) {
        --index;
        result.push_back(buffer[static_cast<std::size_t>(index)]);
      }
      result.push_back('.');
    } else {
      result.push_back('.');
      result.append(static_cast<std::size_t>(-before), '0');
    }
    while(index > 0) {
      --index;
      result.push_back(buffer[static_cast<std::size_t>(index)]);
    }
  }
  return result;
}

std::string Rellu::to_pdf_color(double value) {
  return to_decimal(
    static_cast<int>(std::lround(static_cast<float>(value) * 10000.0f)), 4);
}

std::string Rellu::to_pdf_byte_color(double value) {
  auto byte = static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
  if(byte == 0 || byte == 255) {
    return byte ? "1" : "0";
  }
  auto fixed = std::int64_t(65536) * 1000 / 255 * byte;
  return to_decimal(static_cast<int>((fixed + 32768) >> 16), 3);
}

std::string Rellu::to_pdf_reference(int id) {
  return std::format("{} 0 R", id);
}

std::string Rellu::to_pdf_name(std::string_view name) {
  auto result = std::string("/");
  for(auto character : name) {
    auto code = static_cast<unsigned char>(character);
    if(code < '!' || code > '~' ||
        std::string_view("#/%()<>[]{}").find(character) !=
          std::string_view::npos) {
      result += std::format("#{:02X}", code);
    } else {
      result.push_back(character);
    }
  }
  return result;
}

std::string Rellu::to_pdf_hex(std::uint16_t value) {
  return std::format("{:04X}", value);
}

PdfDictionary::PdfDictionary(std::string_view type) {
  add("Type", to_pdf_name(type));
}

void PdfDictionary::add(std::string_view key, std::string value) {
  m_entries.emplace_back(std::string(key), std::move(value));
}

bool PdfDictionary::is_empty() const {
  return m_entries.empty();
}

std::string PdfDictionary::to_string() const {
  auto result = std::string("<<");
  for(auto i = std::size_t(0); i != m_entries.size(); ++i) {
    if(i != 0) {
      result.push_back('\n');
    }
    result += to_pdf_name(m_entries[i].first);
    result.push_back(' ');
    result += m_entries[i].second;
  }
  result += ">>";
  return result;
}

std::string Rellu::to_pdf_array(const std::vector<std::string>& values) {
  auto result = std::string("[");
  for(auto i = std::size_t(0); i != values.size(); ++i) {
    if(i != 0) {
      result.push_back(' ');
    }
    result += values[i];
  }
  result.push_back(']');
  return result;
}

std::string Rellu::deflate(std::string_view data) {
  auto compressed = qCompress(reinterpret_cast<const uchar*>(data.data()),
    static_cast<qsizetype>(data.size()), -1);
  return std::string(compressed.constData() + 4,
    static_cast<std::size_t>(compressed.size() - 4));
}

PdfFile::PdfFile()
  : m_data(HEADER),
    m_offsets(1, 0) {}

int PdfFile::reserve() {
  m_offsets.push_back(0);
  return static_cast<int>(m_offsets.size() - 1);
}

void PdfFile::write(int id, std::string_view object) {
  m_offsets[static_cast<std::size_t>(id)] = m_data.size();
  m_data += std::format("{} 0 obj\n", id);
  m_data += object;
  m_data += "\nendobj\n";
}

void PdfFile::write_stream(
    int id, PdfDictionary dictionary, std::string data) {
  if(data.size() > MINIMUM_SAVINGS) {
    auto compressed = deflate(data);
    if(data.size() > compressed.size() + MINIMUM_SAVINGS) {
      data = std::move(compressed);
      dictionary.add("Filter", "/FlateDecode");
    }
  }
  write_raw_stream(id, std::move(dictionary), data);
}

void PdfFile::write_raw_stream(
    int id, PdfDictionary dictionary, std::string_view data) {
  dictionary.add("Length", std::to_string(data.size()));
  auto object = dictionary.to_string();
  object += " stream\n";
  object += data;
  object += "\nendstream";
  write(id, object);
}

QByteArray PdfFile::finish(int root, int info) {
  auto start = m_data.size();
  m_data += std::format("xref\n0 {}\n0000000000 65535 f \n", m_offsets.size());
  for(auto i = std::size_t(1); i != m_offsets.size(); ++i) {
    m_data += std::format("{:010} 00000 n \n", m_offsets[i]);
  }
  auto trailer = PdfDictionary();
  trailer.add("Size", std::to_string(m_offsets.size()));
  trailer.add("Root", to_pdf_reference(root));
  trailer.add("Info", to_pdf_reference(info));
  m_data += "trailer\n";
  m_data += trailer.to_string();
  m_data += std::format("\nstartxref\n{}\n%%EOF", start);
  return QByteArray(m_data.data(), static_cast<qsizetype>(m_data.size()));
}

namespace {
  struct FloatPoint {
    float m_x;
    float m_y;
  };

  FloatPoint to_float(Point point) {
    return FloatPoint(
      static_cast<float>(point.m_x), static_cast<float>(point.m_y));
  }

  struct Conic {
    std::array<FloatPoint, 3> m_points;
    float m_weight;
  };

  std::array<Conic, 2> chop(const Conic& conic) {
    auto scale = 1 / (1.0f + conic.m_weight);
    auto weight = std::sqrt(0.5f + conic.m_weight * 0.5f);
    auto& p0 = conic.m_points[0];
    auto& p1 = conic.m_points[1];
    auto& p2 = conic.m_points[2];
    auto wx = conic.m_weight * p1.m_x;
    auto wy = conic.m_weight * p1.m_y;
    auto middle = FloatPoint((p0.m_x + (wx + wx) + p2.m_x) * scale * 0.5f,
      (p0.m_y + (wy + wy) + p2.m_y) * scale * 0.5f);
    auto first = Conic(std::array<FloatPoint, 3>({p0,
      FloatPoint((p0.m_x + wx) * scale, (p0.m_y + wy) * scale), middle}),
      weight);
    auto second = Conic(std::array<FloatPoint, 3>({middle,
      FloatPoint((wx + p2.m_x) * scale, (wy + p2.m_y) * scale), p2}),
      weight);
    return std::array<Conic, 2>({first, second});
  }

  bool is_between(float a, float b, float c) {
    return (a - b) * (c - b) <= 0;
  }

  void subdivide(const Conic& conic, int level,
      std::vector<FloatPoint>& points) {
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
      if(!is_between(
          start, halves[0].m_points[1].m_y, halves[0].m_points[2].m_y)) {
        halves[0].m_points[1].m_y = start;
      }
      if(!is_between(
          halves[1].m_points[0].m_y, halves[1].m_points[1].m_y, end)) {
        halves[1].m_points[1].m_y = end;
      }
    }
    subdivide(halves[0], level - 1, points);
    subdivide(halves[1], level - 1, points);
  }

  std::vector<FloatPoint> to_quadratics(const Conic& conic, float tolerance) {
    auto a = conic.m_weight - 1;
    auto k = a / (4 * (2 + a));
    auto x = k * (conic.m_points[0].m_x - 2 * conic.m_points[1].m_x +
      conic.m_points[2].m_x);
    auto y = k * (conic.m_points[0].m_y - 2 * conic.m_points[1].m_y +
      conic.m_points[2].m_y);
    auto error = std::sqrt(x * x + y * y);
    auto level = 0;
    for(; level < 5; ++level) {
      if(error <= tolerance) {
        break;
      }
      error *= 0.25f;
    }
    auto points = std::vector<FloatPoint>();
    points.push_back(conic.m_points[0]);
    subdivide(conic, level, points);
    return points;
  }

  void append_point(std::string& out, double x, double y) {
    out += to_pdf_number(x);
    out.push_back(' ');
    out += to_pdf_number(y);
    out.push_back(' ');
  }

  void append_quadratic(std::string& out, FloatPoint start,
      FloatPoint control, FloatPoint end) {
    auto scale = static_cast<float>(2.0 / 3.0);
    append_point(out, start.m_x + (control.m_x - start.m_x) * scale,
      start.m_y + (control.m_y - start.m_y) * scale);
    append_point(out, end.m_x + (control.m_x - end.m_x) * scale,
      end.m_y + (control.m_y - end.m_y) * scale);
    append_point(out, end.m_x, end.m_y);
    out += "c\n";
  }

  std::optional<Conic> to_conic(Point start, const Segment& segment) {
    if(!segment.m_is_arc || segment.m_type != SegmentType::CUBIC) {
      return std::nullopt;
    }
    auto end = segment.m_points[2];
    auto corner = start + (segment.m_points[0] - start) / ARC_CONTROL;
    auto incoming = corner - start;
    auto outgoing = end - corner;
    auto dot = incoming.m_x * outgoing.m_x + incoming.m_y * outgoing.m_y;
    auto lengths = std::hypot(incoming.m_x, incoming.m_y) *
      std::hypot(outgoing.m_x, outgoing.m_y);
    if(!(lengths > 0) || std::abs(dot) > 1e-6 * lengths) {
      return std::nullopt;
    }
    return Conic(std::array<FloatPoint, 3>(
      {to_float(start), to_float(corner), to_float(end)}),
      static_cast<float>(std::numbers::sqrt2 / 2));
  }

  std::vector<Segment> rotate_ellipse(
      const std::vector<Segment>& segments, bool is_clip) {
    if(segments.size() != 6 || segments[0].m_type != SegmentType::MOVE ||
        segments[5].m_type != SegmentType::CLOSE ||
        !std::all_of(segments.begin() + 1, segments.begin() + 5,
          [] (const auto& segment) {
            return segment.m_is_arc;
          })) {
      return segments;
    }
    auto first = is_clip ? std::size_t(3) : std::size_t(4);
    auto start = segments[first - 1].m_points[2];
    auto rotated = std::vector<Segment>();
    rotated.push_back(Segment(SegmentType::MOVE,
      std::array<Point, 3>({start, Point(0, 0), Point(0, 0)})));
    for(auto i = std::size_t(0); i != 4; ++i) {
      rotated.push_back(segments[(first - 1 + i) % 4 + 1]);
    }
    rotated.push_back(Segment(SegmentType::CLOSE,
      std::array<Point, 3>({start, Point(0, 0), Point(0, 0)})));
    return rotated;
  }
}

std::optional<Rect> Rellu::find_pdf_rect(const Path& path) {
  auto& segments = path.get_segments();
  if(segments.size() < 4 || segments[0].m_type != SegmentType::MOVE) {
    return std::nullopt;
  }
  auto points = std::vector<Point>();
  points.push_back(segments[0].m_points[0]);
  auto is_closed = false;
  for(auto i = std::size_t(1); i != segments.size(); ++i) {
    auto& segment = segments[i];
    if(segment.m_type == SegmentType::LINE && !is_closed) {
      points.push_back(segment.m_points[0]);
    } else if(segment.m_type == SegmentType::CLOSE && !is_closed) {
      is_closed = true;
    } else {
      return std::nullopt;
    }
  }
  if(points.size() == 5 && points[4] == points[0]) {
    points.pop_back();
    is_closed = true;
  }
  if(!is_closed || points.size() != 4) {
    return std::nullopt;
  }
  auto area = 0.0;
  auto is_horizontal = points[0].m_y == points[1].m_y;
  for(auto i = std::size_t(0); i != 4; ++i) {
    auto& current = points[i];
    auto& next = points[(i + 1) % 4];
    auto is_edge_horizontal = (i % 2 == 0) == is_horizontal;
    if((is_edge_horizontal && current.m_y != next.m_y) ||
        (!is_edge_horizontal && current.m_x != next.m_x) ||
        current == next) {
      return std::nullopt;
    }
    area += current.m_x * next.m_y - next.m_x * current.m_y;
  }
  if(!(area > 0) && path.get_fill_rule() != FillRule::EVEN_ODD) {
    return std::nullopt;
  }
  auto left = std::min({points[0].m_x, points[1].m_x, points[2].m_x});
  auto top = std::min({points[0].m_y, points[1].m_y, points[2].m_y});
  auto right = std::max({points[0].m_x, points[1].m_x, points[2].m_x});
  auto bottom = std::max({points[0].m_y, points[1].m_y, points[2].m_y});
  return Rect(left, top, right - left, bottom - top);
}

std::string Rellu::to_pdf_path(
    const Path& path, double tolerance, bool is_clip) {
  if(path.is_empty()) {
    return "0 0 0 0 re\n";
  }
  if(auto rect = find_pdf_rect(path)) {
    return to_pdf_number(rect->m_x) + ' ' + to_pdf_number(rect->m_y) + ' ' +
      to_pdf_number(rect->m_width) + ' ' + to_pdf_number(rect->m_height) +
      " re\n";
  }
  auto out = std::string();
  auto current = Point(0, 0);
  auto start = Point(0, 0);
  for(auto& segment : rotate_ellipse(path.get_segments(), is_clip)) {
    if(segment.m_type == SegmentType::MOVE) {
      append_point(out, segment.m_points[0].m_x, segment.m_points[0].m_y);
      out += "m\n";
      current = segment.m_points[0];
      start = current;
    } else if(segment.m_type == SegmentType::LINE) {
      append_point(out, segment.m_points[0].m_x, segment.m_points[0].m_y);
      out += "l\n";
      current = segment.m_points[0];
    } else if(segment.m_type == SegmentType::QUADRATIC) {
      append_quadratic(out, to_float(current), to_float(segment.m_points[0]),
        to_float(segment.m_points[1]));
      current = segment.m_points[1];
    } else if(segment.m_type == SegmentType::CUBIC) {
      if(auto conic = to_conic(current, segment)) {
        auto points = to_quadratics(*conic, static_cast<float>(tolerance));
        for(auto i = std::size_t(0); i + 2 < points.size(); i += 2) {
          append_quadratic(out, points[i], points[i + 1], points[i + 2]);
        }
      } else {
        for(auto& point : segment.m_points) {
          append_point(out, point.m_x, point.m_y);
        }
        out += "c\n";
      }
      current = segment.m_points[2];
    } else {
      out += "h\n";
      current = start;
    }
  }
  return out;
}

std::string_view Rellu::get_pdf_fill(const Path& path) {
  if(path.get_fill_rule() == FillRule::EVEN_ODD) {
    return "f*\n";
  }
  return "f\n";
}

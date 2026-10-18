#include <cmath>
#include <optional>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/SvgPath.hpp"

using namespace Rellu;

namespace {
  bool is_near(Point left, Point right, double tolerance = 1e-9) {
    return distance(left, right) < tolerance;
  }

  bool is_near(const Rect& left, const Rect& right, double tolerance = 1e-9) {
    return is_near(left.get_top_left(), right.get_top_left(), tolerance) &&
      is_near(left.get_bottom_right(), right.get_bottom_right(), tolerance);
  }

  bool is_near(const Path& left, const Path& right) {
    auto& left_segments = left.get_segments();
    auto& right_segments = right.get_segments();
    if(left_segments.size() != right_segments.size()) {
      return false;
    }
    for(auto i = std::size_t(0); i != left_segments.size(); ++i) {
      if(left_segments[i].m_type != right_segments[i].m_type) {
        return false;
      }
      for(auto j = std::size_t(0); j != 3; ++j) {
        if(!is_near(left_segments[i].m_points[j],
            right_segments[i].m_points[j])) {
          return false;
        }
      }
    }
    return true;
  }

  std::vector<SegmentType> get_types(const Path& path) {
    auto types = std::vector<SegmentType>();
    for(auto& segment : path.get_segments()) {
      types.push_back(segment.m_type);
    }
    return types;
  }

  Path make_polyline(const std::vector<Point>& points, bool is_closed) {
    auto path = Path();
    path.move_to(points.front());
    for(auto i = std::size_t(1); i != points.size(); ++i) {
      path.line_to(points[i]);
    }
    if(is_closed) {
      path.close();
    }
    return path;
  }
}

TEST_SUITE("SvgPath") {
  TEST_CASE("corpus_closed_curve") {
    auto path = parse_svg_path("M 143 -95 L 143 -86 L 134 -86 C 134 "
      "-90.97055816650391 138.0294036865234 -95 143 -95 Z");
    REQUIRE(path);
    auto expected = Path();
    expected.move_to(Point(143, -95));
    expected.line_to(Point(143, -86));
    expected.line_to(Point(134, -86));
    expected.cubic_to(Point(134, -90.97055816650391),
      Point(138.0294036865234, -95), Point(143, -95));
    expected.close();
    REQUIRE(*path == expected);
  }

  TEST_CASE("corpus_subpaths") {
    auto path = parse_svg_path("M 1 2 L 7 2 L 7 7 L 1 7 L 1 2 Z M 0 8 L 8 8 "
      "L 8 1 L 0 1 L 0 8 Z");
    REQUIRE(path);
    auto expected = make_polyline({Point(1, 2), Point(7, 2), Point(7, 7),
      Point(1, 7), Point(1, 2)}, true);
    expected.move_to(Point(0, 8));
    expected.line_to(Point(8, 8));
    expected.line_to(Point(8, 1));
    expected.line_to(Point(0, 1));
    expected.line_to(Point(0, 8));
    expected.close();
    REQUIRE(*path == expected);
    path->set_fill_rule(FillRule::EVEN_ODD);
    REQUIRE(contains(*path, Point(0.5, 4)));
    REQUIRE(!contains(*path, Point(4, 4)));
  }

  TEST_CASE("corpus_exponent") {
    auto path = parse_svg_path("M -5.030391454405835e-15 0 L 146 0 L 146 1 L "
      "-5.030391454405835e-15 1 L -5.030391454405835e-15 0 Z");
    REQUIRE(path);
    REQUIRE(*path == make_polyline({Point(-5.030391454405835e-15, 0),
      Point(146, 0), Point(146, 1), Point(-5.030391454405835e-15, 1),
      Point(-5.030391454405835e-15, 0)}, true));
    auto small = parse_svg_path(
      "M 1.2166748046875 0 L 3.853311859396607e-31 6.605133056640625");
    REQUIRE(small);
    REQUIRE(small->get_segments()[1].m_points[0] ==
      Point(3.853311859396607e-31, 6.605133056640625));
  }

  TEST_CASE("corpus_open_curve") {
    auto path = parse_svg_path("M 4643 5807 C 4855.90869140625 5807 "
      "2487.149169921875 6337.65673828125 2754.96630859375 6337.65673828125");
    REQUIRE(path);
    auto expected = Path();
    expected.move_to(Point(4643, 5807));
    expected.cubic_to(Point(4855.90869140625, 5807),
      Point(2487.149169921875, 6337.65673828125),
      Point(2754.96630859375, 6337.65673828125));
    REQUIRE(*path == expected);
  }

  TEST_CASE("corpus_move") {
    auto path = parse_svg_path("M 1439 0");
    REQUIRE(path);
    auto expected = Path();
    expected.move_to(Point(1439, 0));
    REQUIRE(*path == expected);
  }

  TEST_CASE("corpus_curved_subpaths") {
    auto path = parse_svg_path("M 10 0 C 4.5 0 0 4.5 0 10 C 0 15.5 4.5 20 10 "
      "20 C 15.5 20 20 15.5 20 10 C 20 4.500000476837158 15.5 0 10 0 Z M 10 "
      "17.5 C 5.875 17.5 2.5 14.125 2.5 10 C 2.5 5.875 5.875 2.5 10 2.5 L 10 "
      "10 L 17.25 12 C 16.25 15.25 13.375 17.5 10 17.5 Z");
    REQUIRE(path);
    REQUIRE(get_types(*path) == std::vector{SegmentType::MOVE,
      SegmentType::CUBIC, SegmentType::CUBIC, SegmentType::CUBIC,
      SegmentType::CUBIC, SegmentType::CLOSE, SegmentType::MOVE,
      SegmentType::CUBIC, SegmentType::CUBIC, SegmentType::LINE,
      SegmentType::LINE, SegmentType::CUBIC, SegmentType::CLOSE});
    REQUIRE(is_near(get_bounds(*path), Rect(0, 0, 20, 20), 0.2));
    path->set_fill_rule(FillRule::EVEN_ODD);
    REQUIRE(contains(*path, Point(1, 10)));
    REQUIRE(!contains(*path, Point(6, 10)));
    REQUIRE(contains(*path, Point(14, 6)));
  }

  TEST_CASE("empty") {
    auto path = parse_svg_path("");
    REQUIRE(path);
    REQUIRE(path->is_empty());
    auto blank = parse_svg_path(" \t\r\n");
    REQUIRE(blank);
    REQUIRE(blank->is_empty());
  }

  TEST_CASE("relative_lines") {
    auto path = parse_svg_path("m 10 10 l 5 0 l 0 5 z");
    REQUIRE(path);
    REQUIRE(*path ==
      make_polyline({Point(10, 10), Point(15, 10), Point(15, 15)}, true));
  }

  TEST_CASE("horizontal_vertical") {
    auto path = parse_svg_path("M 1 1 H 10 V 5 h -3 v 2");
    REQUIRE(path);
    REQUIRE(*path == make_polyline({Point(1, 1), Point(10, 1), Point(10, 5),
      Point(7, 5), Point(7, 7)}, false));
  }

  TEST_CASE("implicit_repeat") {
    auto path = parse_svg_path("M 0 0 10 0 10 10 L 0 10 0 5");
    REQUIRE(path);
    REQUIRE(*path == make_polyline({Point(0, 0), Point(10, 0), Point(10, 10),
      Point(0, 10), Point(0, 5)}, false));
    auto relative = parse_svg_path("m 1 1 2 2 l 1 0 0 1");
    REQUIRE(relative);
    REQUIRE(*relative == make_polyline(
      {Point(1, 1), Point(3, 3), Point(4, 3), Point(4, 4)}, false));
    auto curves = parse_svg_path("M 0 0 C 1 1 2 2 3 3 4 4 5 5 6 6");
    REQUIRE(curves);
    REQUIRE(get_types(*curves) ==
      std::vector{SegmentType::MOVE, SegmentType::CUBIC, SegmentType::CUBIC});
    REQUIRE(curves->get_segments()[2].m_points[2] == Point(6, 6));
  }

  TEST_CASE("separators") {
    auto expected = make_polyline({Point(1, 2), Point(3, 4), Point(5, 6)},
      false);
    auto commas = parse_svg_path("M1,2L3,4,5,6");
    REQUIRE(commas);
    REQUIRE(*commas == expected);
    auto spaced = parse_svg_path("  M 1 , 2\tL\n3 4 ,5 6  ");
    REQUIRE(spaced);
    REQUIRE(*spaced == expected);
  }

  TEST_CASE("compact_numbers") {
    auto path = parse_svg_path("M1.5.5L-2-3l.5-.5L+1 0.");
    REQUIRE(path);
    REQUIRE(*path == make_polyline({Point(1.5, 0.5), Point(-2, -3),
      Point(-1.5, -3.5), Point(1, 0)}, false));
  }

  TEST_CASE("exponents") {
    auto path = parse_svg_path("M1e2 1E-2L1e+2-2.5e1M 1e-400 -1e-400");
    REQUIRE(path);
    auto& segments = path->get_segments();
    REQUIRE(segments.size() == 3);
    REQUIRE(segments[0].m_points[0] == Point(100, 0.01));
    REQUIRE(segments[1].m_points[0] == Point(100, -25));
    REQUIRE(segments[2].m_points[0] == Point(0, 0));
  }

  TEST_CASE("smooth_cubic") {
    auto path = parse_svg_path("M 0 0 C 0 10 10 10 10 0 S 20 -10 20 0 s 5 5 "
      "10 0");
    REQUIRE(path);
    auto expected = Path();
    expected.move_to(Point(0, 0));
    expected.cubic_to(Point(0, 10), Point(10, 10), Point(10, 0));
    expected.cubic_to(Point(10, -10), Point(20, -10), Point(20, 0));
    expected.cubic_to(Point(20, 10), Point(25, 5), Point(30, 0));
    REQUIRE(*path == expected);
    auto unpaired = parse_svg_path("M 1 1 L 2 2 S 5 5 10 0");
    REQUIRE(unpaired);
    REQUIRE(unpaired->get_segments()[2].m_points[0] == Point(2, 2));
  }

  TEST_CASE("smooth_quadratic") {
    auto path = parse_svg_path("M 0 0 Q 5 10 10 0 T 20 0 t 10 0");
    REQUIRE(path);
    auto expected = Path();
    expected.move_to(Point(0, 0));
    expected.quadratic_to(Point(5, 10), Point(10, 0));
    expected.quadratic_to(Point(15, -10), Point(20, 0));
    expected.quadratic_to(Point(25, 10), Point(30, 0));
    REQUIRE(*path == expected);
    auto unpaired = parse_svg_path("M 1 1 C 2 2 3 3 4 4 T 10 0");
    REQUIRE(unpaired);
    REQUIRE(unpaired->get_segments()[2].m_points[0] == Point(4, 4));
  }

  TEST_CASE("relative_curves") {
    auto path = parse_svg_path("m 10 10 c 1 1 2 2 3 3 q 1 0 2 0");
    REQUIRE(path);
    auto expected = Path();
    expected.move_to(Point(10, 10));
    expected.cubic_to(Point(11, 11), Point(12, 12), Point(13, 13));
    expected.quadratic_to(Point(14, 13), Point(15, 13));
    REQUIRE(*path == expected);
  }

  TEST_CASE("relative_after_close") {
    auto path = parse_svg_path("M 10 10 L 20 10 Z l 0 5 m 1 1 h 1");
    REQUIRE(path);
    auto expected = make_polyline({Point(10, 10), Point(20, 10)}, true);
    expected.line_to(Point(10, 15));
    expected.move_to(Point(11, 16));
    expected.line_to(Point(12, 16));
    REQUIRE(*path == expected);
  }

  TEST_CASE("semicircle_arc") {
    auto path = parse_svg_path("M 0 0 A 5 5 0 0 1 10 0");
    REQUIRE(path);
    REQUIRE(get_types(*path) ==
      std::vector{SegmentType::MOVE, SegmentType::CUBIC, SegmentType::CUBIC});
    auto& segments = path->get_segments();
    REQUIRE(is_near(segments[1].m_points[2], Point(5, -5)));
    REQUIRE(segments[2].m_points[2] == Point(10, 0));
    REQUIRE(is_near(get_bounds(*path), Rect(0, -5, 10, 5)));
    auto reversed = parse_svg_path("M 0 0 A 5 5 0 0 0 10 0");
    REQUIRE(reversed);
    REQUIRE(is_near(get_bounds(*reversed), Rect(0, 0, 10, 5)));
  }

  TEST_CASE("large_arc") {
    auto large = parse_svg_path("M 0 0 A 10 10 0 1 0 10 0");
    REQUIRE(large);
    REQUIRE(large->get_segments().size() == 5);
    REQUIRE(large->get_segments().back().m_points[2] == Point(10, 0));
    auto height = 10 + 5 * std::sqrt(3.0);
    REQUIRE(is_near(get_bounds(*large), Rect(-5, 0, 20, height), 1e-3));
    auto small = parse_svg_path("M 0 0 A 10 10 0 0 0 10 0");
    REQUIRE(small);
    REQUIRE(small->get_segments().size() == 2);
    REQUIRE(is_near(
      get_bounds(*small), Rect(0, 0, 10, 10 - 5 * std::sqrt(3.0)), 1e-3));
  }

  TEST_CASE("arc_points") {
    auto path = parse_svg_path("M 0 0 A 10 10 0 1 0 10 0");
    REQUIRE(path);
    auto center = Point(5, 5 * std::sqrt(3.0));
    auto start = Point(0, 0);
    for(auto& segment : path->get_segments()) {
      if(segment.m_type != SegmentType::CUBIC) {
        continue;
      }
      auto& points = segment.m_points;
      for(auto t : {0.25, 0.5, 0.75}) {
        auto s = 1 - t;
        auto point = s * s * s * start + 3 * s * s * t * points[0] +
          3 * s * t * t * points[1] + t * t * t * points[2];
        REQUIRE(distance(point, center) == doctest::Approx(10).epsilon(1e-3));
      }
      start = points[2];
    }
  }

  TEST_CASE("arc_radius_scaling") {
    auto scaled = parse_svg_path("M 0 0 A 1 1 0 0 1 10 0");
    auto exact = parse_svg_path("M 0 0 A 5 5 0 0 1 10 0");
    REQUIRE(scaled);
    REQUIRE(exact);
    REQUIRE(is_near(*scaled, *exact));
    auto negative = parse_svg_path("M 0 0 A -5 -5 0 0 1 10 0");
    REQUIRE(negative);
    REQUIRE(is_near(*negative, *exact));
  }

  TEST_CASE("rotated_arc") {
    auto path = parse_svg_path("M 0 0 A 10 5 90 0 1 0 20");
    REQUIRE(path);
    REQUIRE(is_near(get_bounds(*path), Rect(0, 0, 5, 20), 1e-6));
  }

  TEST_CASE("degenerate_arc") {
    auto line = parse_svg_path("M 0 0 A 0 5 0 0 1 10 0");
    REQUIRE(line);
    REQUIRE(*line == make_polyline({Point(0, 0), Point(10, 0)}, false));
    auto point = parse_svg_path("M 3 4 A 5 5 0 0 1 3 4");
    REQUIRE(point);
    REQUIRE(get_types(*point) == std::vector{SegmentType::MOVE});
  }

  TEST_CASE("packed_arc_flags") {
    auto packed = parse_svg_path("M0 0a5 5 0 0110 0");
    auto spaced = parse_svg_path("M 0 0 a 5 5 0 0 1 10 0");
    REQUIRE(packed);
    REQUIRE(spaced);
    REQUIRE(*packed == *spaced);
  }

  TEST_CASE("malformed") {
    REQUIRE(!parse_svg_path("L 0 0"));
    REQUIRE(!parse_svg_path("0 0"));
    REQUIRE(!parse_svg_path("M"));
    REQUIRE(!parse_svg_path("M 0"));
    REQUIRE(!parse_svg_path("M,0 0"));
    REQUIRE(!parse_svg_path("M 0 0 Z 1 1"));
    REQUIRE(!parse_svg_path("M 0 0 L x 1"));
    REQUIRE(!parse_svg_path("M 0 0 L 1e 1"));
    REQUIRE(!parse_svg_path("M 0 0 L 1 2,"));
    REQUIRE(!parse_svg_path("M 0 0 L 1,,2"));
    REQUIRE(!parse_svg_path("M 0 0 L 1 2 , L 3 4"));
    REQUIRE(!parse_svg_path("M 0 0 C 1 1 2 2"));
    REQUIRE(!parse_svg_path("M 0 0 A 5 5 0 2 1 10 0"));
    REQUIRE(!parse_svg_path("M 0 0 #"));
    REQUIRE(!parse_svg_path("M - 0"));
    REQUIRE(!parse_svg_path("M . 0"));
    REQUIRE(!parse_svg_path("M 0 0 L inf 0"));
    REQUIRE(!parse_svg_path("M 1e400 0"));
  }
}

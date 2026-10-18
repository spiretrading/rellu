#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/SvgPath.hpp"

using namespace Rellu;

namespace {
  struct Operand {
    Matrix m_transform;
    std::string_view m_path;
  };

  struct Compound {
    BooleanOperation m_operation;
    std::vector<Operand> m_operands;
    std::string_view m_path;
  };

  bool is_near(const Rect& left, const Rect& right, double tolerance) {
    return distance(left.get_top_left(), right.get_top_left()) < tolerance &&
      distance(left.get_bottom_right(), right.get_bottom_right()) <
        tolerance;
  }

  double get_area(const Polyline& polyline) {
    auto& points = polyline.m_points;
    auto area = 0.0;
    for(auto i = std::size_t(0); i != points.size(); ++i) {
      area += cross(points[i], points[(i + 1) % points.size()]);
    }
    return area / 2;
  }

  double get_area(const Path& path) {
    auto area = 0.0;
    for(auto& polyline : flatten(path, 0.01)) {
      area += get_area(polyline);
    }
    return area;
  }

  Path make_ring(FillRule rule) {
    auto path = make_rectangle(Rect(0, 0, 10, 10));
    path.move_to(Point(3, 3));
    path.line_to(Point(7, 3));
    path.line_to(Point(7, 7));
    path.line_to(Point(3, 7));
    path.close();
    path.set_fill_rule(rule);
    return path;
  }
}

TEST_SUITE("BooleanOperation") {
  TEST_CASE("empty_operands") {
    auto square = make_rectangle(Rect(0, 0, 10, 10));
    REQUIRE(get_area(combine(square, Path(), BooleanOperation::ADD, 0.1)) ==
      doctest::Approx(100));
    REQUIRE(get_area(combine(Path(), square, BooleanOperation::ADD, 0.1)) ==
      doctest::Approx(100));
    REQUIRE(get_area(
      combine(square, Path(), BooleanOperation::SUBTRACT, 0.1)) ==
        doctest::Approx(100));
    REQUIRE(get_area(
      combine(square, Path(), BooleanOperation::EXCLUDE, 0.1)) ==
        doctest::Approx(100));
    REQUIRE(get_area(
      combine(Path(), square, BooleanOperation::EXCLUDE, 0.1)) ==
        doctest::Approx(100));
    REQUIRE(
      combine(Path(), square, BooleanOperation::SUBTRACT, 0.1).is_empty());
    REQUIRE(
      combine(square, Path(), BooleanOperation::INTERSECT, 0.1).is_empty());
    REQUIRE(combine(Path(), Path(), BooleanOperation::ADD, 0.1).is_empty());
  }

  TEST_CASE("self_operands") {
    auto circle = make_circle(Point(5, 5), 5);
    auto area =
      get_area(combine(circle, Path(), BooleanOperation::ADD, 0.01));
    REQUIRE(area == doctest::Approx(25 * std::numbers::pi).epsilon(0.01));
    REQUIRE(get_area(combine(circle, circle, BooleanOperation::ADD, 0.01)) ==
      doctest::Approx(area));
    REQUIRE(get_area(
      combine(circle, circle, BooleanOperation::INTERSECT, 0.01)) ==
        doctest::Approx(area));
    REQUIRE(
      combine(circle, circle, BooleanOperation::SUBTRACT, 0.01).is_empty());
    REQUIRE(
      combine(circle, circle, BooleanOperation::EXCLUDE, 0.01).is_empty());
  }

  TEST_CASE("rectangles") {
    auto left = make_rectangle(Rect(0, 0, 10, 10));
    auto right = make_rectangle(Rect(5, 5, 10, 10));
    REQUIRE(get_area(combine(left, right, BooleanOperation::ADD, 0.1)) ==
      doctest::Approx(175));
    REQUIRE(get_area(combine(left, right, BooleanOperation::SUBTRACT, 0.1)) ==
      doctest::Approx(75));
    REQUIRE(get_area(
      combine(left, right, BooleanOperation::INTERSECT, 0.1)) ==
        doctest::Approx(25));
    REQUIRE(get_area(combine(left, right, BooleanOperation::EXCLUDE, 0.1)) ==
      doctest::Approx(150));
    auto intersection =
      combine(left, right, BooleanOperation::INTERSECT, 0.1);
    REQUIRE(get_bounds(intersection) == Rect(5, 5, 5, 5));
    REQUIRE(flatten(
      combine(left, right, BooleanOperation::EXCLUDE, 0.1), 0.1).size() == 2);
  }

  TEST_CASE("circles") {
    auto radius = 10.0;
    auto left = make_circle(Point(0, 0), radius);
    auto right = make_circle(Point(radius, 0), radius);
    auto circle = std::numbers::pi * radius * radius;
    auto lens = 2 * radius * radius * std::numbers::pi / 3 -
      radius * radius * std::sqrt(3.0) / 2;
    auto tolerance = 0.001;
    REQUIRE(get_area(
      combine(left, right, BooleanOperation::ADD, tolerance)) ==
        doctest::Approx(2 * circle - lens).epsilon(0.001));
    REQUIRE(get_area(
      combine(left, right, BooleanOperation::SUBTRACT, tolerance)) ==
        doctest::Approx(circle - lens).epsilon(0.001));
    REQUIRE(get_area(
      combine(left, right, BooleanOperation::INTERSECT, tolerance)) ==
        doctest::Approx(lens).epsilon(0.001));
    REQUIRE(get_area(
      combine(left, right, BooleanOperation::EXCLUDE, tolerance)) ==
        doctest::Approx(2 * circle - 2 * lens).epsilon(0.001));
    auto intersection =
      combine(left, right, BooleanOperation::INTERSECT, tolerance);
    REQUIRE(contains(intersection, Point(radius / 2, 0)));
    REQUIRE(!contains(intersection, Point(-radius / 2, 0)));
  }

  TEST_CASE("holes") {
    auto result = combine(make_rectangle(Rect(0, 0, 10, 10)),
      make_rectangle(Rect(3, 3, 4, 4)), BooleanOperation::SUBTRACT, 0.1);
    REQUIRE(get_area(result) == doctest::Approx(84));
    REQUIRE(get_bounds(result) == Rect(0, 0, 10, 10));
    REQUIRE(contains(result, Point(1, 1)));
    REQUIRE(!contains(result, Point(5, 5)));
    auto polylines = flatten(result, 0.1);
    REQUIRE(polylines.size() == 2);
    auto outer = std::max(get_area(polylines[0]), get_area(polylines[1]));
    auto hole = std::min(get_area(polylines[0]), get_area(polylines[1]));
    REQUIRE(outer == doctest::Approx(100));
    REQUIRE(hole == doctest::Approx(-16));
  }

  TEST_CASE("result_form") {
    auto ring = make_ring(FillRule::EVEN_ODD);
    auto result = combine(ring, make_circle(Point(10, 10), 3),
      BooleanOperation::ADD, 0.1);
    REQUIRE(result.get_fill_rule() == FillRule::NON_ZERO);
    auto& segments = result.get_segments();
    REQUIRE(!segments.empty());
    REQUIRE(segments.front().m_type == SegmentType::MOVE);
    REQUIRE(segments.back().m_type == SegmentType::CLOSE);
    for(auto i = std::size_t(1); i != segments.size(); ++i) {
      auto type = segments[i].m_type;
      REQUIRE((type == SegmentType::LINE || type == SegmentType::CLOSE ||
        type == SegmentType::MOVE));
      if(type == SegmentType::MOVE) {
        REQUIRE(segments[i - 1].m_type == SegmentType::CLOSE);
      }
    }
    for(auto& polyline : flatten(result, 0.1)) {
      REQUIRE(polyline.m_is_closed);
    }
  }

  TEST_CASE("even_odd") {
    auto ring = make_ring(FillRule::EVEN_ODD);
    auto result = combine(ring, Path(), BooleanOperation::ADD, 0.1);
    REQUIRE(get_area(result) == doctest::Approx(84));
    REQUIRE(!contains(result, Point(5, 5)));
    auto filled = make_ring(FillRule::NON_ZERO);
    REQUIRE(get_area(combine(filled, Path(), BooleanOperation::ADD, 0.1)) ==
      doctest::Approx(100));
    auto overlap = make_rectangle(Rect(0, 0, 10, 10));
    overlap.move_to(Point(5, 0));
    overlap.line_to(Point(15, 0));
    overlap.line_to(Point(15, 10));
    overlap.line_to(Point(5, 10));
    overlap.close();
    overlap.set_fill_rule(FillRule::EVEN_ODD);
    REQUIRE(get_area(combine(overlap, Path(), BooleanOperation::ADD, 0.1)) ==
      doctest::Approx(100));
    overlap.set_fill_rule(FillRule::NON_ZERO);
    REQUIRE(get_area(combine(overlap, Path(), BooleanOperation::ADD, 0.1)) ==
      doctest::Approx(150));
  }

  TEST_CASE("mixed_fill_rules") {
    auto ring = make_ring(FillRule::EVEN_ODD);
    auto square = make_rectangle(Rect(0, 0, 10, 10));
    REQUIRE(get_area(combine(square, ring, BooleanOperation::SUBTRACT, 0.1)) ==
      doctest::Approx(16));
    REQUIRE(combine(ring, square, BooleanOperation::SUBTRACT, 0.1).is_empty());
    REQUIRE(get_area(combine(ring, make_rectangle(Rect(4, 4, 2, 2)),
      BooleanOperation::ADD, 0.1)) == doctest::Approx(88));
    REQUIRE(get_area(combine(make_rectangle(Rect(4, 4, 2, 2)), ring,
      BooleanOperation::ADD, 0.1)) == doctest::Approx(88));
    REQUIRE(get_area(
      combine(ring, square, BooleanOperation::EXCLUDE, 0.1)) ==
        doctest::Approx(16));
  }

  TEST_CASE("open_subpaths") {
    auto path = Path();
    path.move_to(Point(0, 0));
    path.line_to(Point(10, 0));
    path.line_to(Point(10, 10));
    path.move_to(Point(20, 0));
    path.cubic_to(Point(30, 0), Point(30, 0), Point(30, 10));
    auto result = combine(path, Path(), BooleanOperation::ADD, 0.01);
    REQUIRE(flatten(result, 0.01).size() == 2);
    REQUIRE(contains(result, Point(8, 2)));
    REQUIRE(contains(result, Point(27, 3)));
    REQUIRE(get_area(result) > 50);
    REQUIRE(get_area(combine(path, make_rectangle(Rect(0, 0, 10, 10)),
      BooleanOperation::INTERSECT, 0.01)) == doctest::Approx(50));
  }

  TEST_CASE("degenerate_subpaths") {
    auto path = make_line(Point(0, 0), Point(10, 10));
    path.move_to(Point(5, 5));
    path.close();
    path.move_to(Point(0, 0));
    path.line_to(Point(5, 0));
    path.line_to(Point(10, 0));
    path.close();
    path.move_to(Point(1, 1));
    path.line_to(Point(1, 1));
    path.line_to(Point(1, 1));
    path.close();
    REQUIRE(combine(path, Path(), BooleanOperation::ADD, 0.1).is_empty());
    REQUIRE(combine(path, path, BooleanOperation::EXCLUDE, 0.1).is_empty());
    auto square = make_rectangle(Rect(0, 0, 10, 10));
    REQUIRE(get_area(combine(square, path, BooleanOperation::SUBTRACT, 0.1)) ==
      doctest::Approx(100));
    auto bowtie = Path();
    bowtie.move_to(Point(0, 0));
    bowtie.line_to(Point(10, 10));
    bowtie.line_to(Point(10, 0));
    bowtie.line_to(Point(0, 10));
    bowtie.close();
    auto result = combine(bowtie, Path(), BooleanOperation::ADD, 0.1);
    REQUIRE(get_area(result) == doctest::Approx(50));
    for(auto& polyline : flatten(result, 0.1)) {
      REQUIRE(get_area(polyline) == doctest::Approx(25));
    }
  }

  TEST_CASE("unrepresentable_coordinates") {
    auto square = make_rectangle(Rect(0, 0, 10, 10));
    auto path = square;
    path.move_to(Point(std::numeric_limits<double>::quiet_NaN(), 0));
    path.line_to(Point(5, 5));
    path.line_to(Point(0, 5));
    path.move_to(Point(0, 0));
    path.cubic_to(Point(std::numeric_limits<double>::infinity(), 0),
      Point(5, 5), Point(0, 5));
    path.move_to(Point(0, 0));
    path.line_to(Point(1e21, 0));
    path.line_to(Point(0, 5));
    auto expected = combine(square, Path(), BooleanOperation::ADD, 0.1);
    REQUIRE(combine(path, Path(), BooleanOperation::ADD, 0.1) == expected);
    REQUIRE(combine(Path(), path, BooleanOperation::ADD, 0.1) == expected);
    REQUIRE(
      combine(square, path, BooleanOperation::SUBTRACT, 0.1).is_empty());
    auto operands = std::array{path, square, path};
    REQUIRE(combine(operands, BooleanOperation::INTERSECT, 0.1) == expected);
  }

  TEST_CASE("large_coordinates") {
    auto origin = std::ldexp(1.0, 53);
    auto size = std::ldexp(1.0, 24);
    auto left = make_rectangle(Rect(origin, 0, size, size));
    auto right =
      make_rectangle(Rect(origin + size / 2, size / 2, size, size));
    auto sum = combine(left, right, BooleanOperation::ADD, 0.1);
    REQUIRE(get_area(sum) == doctest::Approx(7 * std::ldexp(1.0, 46)));
    REQUIRE(get_bounds(sum) == Rect(origin, 0, 1.5 * size, 1.5 * size));
    auto operands = std::array{left, right};
    REQUIRE(get_area(combine(operands, BooleanOperation::INTERSECT, 0.1)) ==
      doctest::Approx(std::ldexp(1.0, 46)));
  }

  TEST_CASE("tolerance") {
    auto radius = 100.0;
    auto approximation_error = 0.04;
    auto circle = make_circle(Point(0, 0), radius);
    auto previous = std::size_t(0);
    for(auto tolerance : {1.0, 0.1, 0.01}) {
      auto result = combine(circle, Path(), BooleanOperation::ADD, tolerance);
      auto polylines = flatten(result, tolerance);
      REQUIRE(polylines.size() == 1);
      auto& points = polylines.front().m_points;
      REQUIRE(points.size() > previous);
      previous = points.size();
      for(auto i = std::size_t(0); i != points.size(); ++i) {
        REQUIRE(length(points[i]) < radius + approximation_error);
        auto middle =
          interpolate(points[i], points[(i + 1) % points.size()], 0.5);
        REQUIRE(length(middle) > radius - tolerance - approximation_error);
      }
      REQUIRE(get_area(result) ==
        doctest::Approx(std::numbers::pi * radius * radius).epsilon(
          2 * tolerance / radius + 0.001));
    }
  }

  TEST_CASE("small_features") {
    auto square = make_rectangle(Rect(0.5, 0.5, 0.001, 0.001));
    auto result = combine(square, Path(), BooleanOperation::ADD, 0.00001);
    REQUIRE(std::abs(get_area(result) - 0.000001) < 0.00000001);
    auto bounds = get_bounds(result);
    REQUIRE(is_near(bounds, Rect(0.5, 0.5, 0.001, 0.001), 0.00001));
  }

  TEST_CASE("sequence") {
    auto operands = std::array{make_rectangle(Rect(0, 0, 10, 10)),
      make_rectangle(Rect(2, 0, 2, 10)), make_rectangle(Rect(6, 0, 2, 10))};
    auto subtraction = combine(operands, BooleanOperation::SUBTRACT, 0.1);
    REQUIRE(get_area(subtraction) == doctest::Approx(60));
    REQUIRE(flatten(subtraction, 0.1).size() == 3);
    REQUIRE(get_area(combine(operands, BooleanOperation::EXCLUDE, 0.1)) ==
      doctest::Approx(60));
    REQUIRE(get_area(combine(operands, BooleanOperation::ADD, 0.1)) ==
      doctest::Approx(100));
    REQUIRE(combine(operands, BooleanOperation::INTERSECT, 0.1).is_empty());
    auto pair = std::span(operands).first(2);
    for(auto operation : {BooleanOperation::ADD, BooleanOperation::SUBTRACT,
        BooleanOperation::INTERSECT, BooleanOperation::EXCLUDE}) {
      REQUIRE(combine(pair, operation, 0.1) ==
        combine(operands[0], operands[1], operation, 0.1));
    }
    auto ring = make_ring(FillRule::EVEN_ODD);
    REQUIRE(get_area(combine(std::span(&ring, 1), BooleanOperation::SUBTRACT,
      0.1)) == doctest::Approx(84));
    REQUIRE(combine(
      std::span<const Path>(), BooleanOperation::ADD, 0.1).is_empty());
  }

  TEST_CASE("xd_compounds") {
    auto compounds = std::vector{
      Compound(BooleanOperation::ADD, std::vector{
        Operand(Matrix(1, 0, 0, 1, 0, 0),
          "M 2 0 H 129 A 2 2 0 0 1 131 2 V 30 H 0 V 2 A 2 2 0 0 1 2 0 Z"),
        Operand(Matrix(1, 0, 0, 1, 0, 28),
          "M 0 0 H 232 A 2 2 0 0 1 234 2 V 179 A 2 2 0 0 1 232 181 H 2 A 2 2 "
          "0 0 1 0 179 V 0 Z")},
        "M 1.999800086021423 208.9998016357422 C 0.8955000042915344 "
        "208.9998016357422 0 208.1043090820312 0 207 L 0 29.99970054626465 L "
        "0 1.999800086021423 C 0 0.8955000042915344 0.8955000042915344 0 "
        "1.999800086021423 0 L 128.9997100830078 0 C 130.1049041748047 0 "
        "131.0003967285156 0.8955000042915344 131.0003967285156 "
        "1.999800086021423 L 131.0003967285156 27.99990081787109 L "
        "232.0001983642578 27.99990081787109 C 233.1045074462891 "
        "27.99990081787109 234 28.89540100097656 234 29.99970054626465 L 234 "
        "207 C 234 208.1043090820312 233.1045074462891 208.9998016357422 "
        "232.0001983642578 208.9998016357422 L 1.999800086021423 "
        "208.9998016357422 Z"),
      Compound(BooleanOperation::SUBTRACT, std::vector{
        Operand(Matrix(1, 0, 0, 1, -3677, -21402.513671875),
          "M 5.138369560241699 2.5691847801208496 A 2.5691847801208496 "
          "2.5691847801208496 0 1 1 0 2.5691847801208496 A 2.5691847801208496 "
          "2.5691847801208496 0 1 1 5.138369560241699 2.5691847801208496 Z"),
        Operand(Matrix(1, 0, 0, 1, -3678.4306640625, -21403.943359375),
          "M 3.999755859375 3.9990234375 L 3.999755859375 0.943359375 L "
          "6.356689453125 1.37890625 L 6.923583984375 3.9990234375 L "
          "3.999755859375 3.9990234375 Z")},
        "M -3674.43359375 -21397.376953125 C -3675.849365234375 "
        "-21397.376953125 -3677.001220703125 -21398.529296875 "
        "-3677.001220703125 -21399.943359375 C -3677.001220703125 "
        "-21401.359375 -3675.849365234375 -21402.51171875 -3674.43359375 "
        "-21402.51171875 L -3674.43359375 -21399.943359375 L "
        "-3671.860595703125 -21399.943359375 C -3671.860595703125 "
        "-21398.52734375 -3673.015380859375 -21397.376953125 -3674.43359375 "
        "-21397.376953125 Z"),
      Compound(BooleanOperation::ADD, std::vector{
        Operand(Matrix(1, 0, 0, 1, 16, 11),
          "M 0 0 H 124 V 64 H 0 V 0 Z"),
        Operand(Matrix(0, 1, -1, 0, 162, 30),
          "M 12.999999046325684 0 L 26 24 L 0 24 Z")},
        "M 16.00020027160645 74.99970245361328 L 16.00020027160645 "
        "10.99980068206787 L 140.0003967285156 10.99980068206787 L "
        "140.0003967285156 31.08344078063965 L 162 43.00020217895508 L "
        "140.0003967285156 54.91613388061523 L 140.0003967285156 "
        "74.99970245361328 L 16.00020027160645 74.99970245361328 Z"),
      Compound(BooleanOperation::EXCLUDE, std::vector{
        Operand(Matrix(1, 0, 0, 1, 18.14741325378418, 45.40198516845703),
          "M 0 18.01951789855957 L 0 0 L 54.60517120361328 0 L "
          "54.60517120361328 18.01951789855957 L 0 18.01951789855957 Z"),
        Operand(Matrix(1, 0, 0, 1, 0, 27.24118995666504),
          "M 90.90000152587891 54.62443161010742 L 0 54.62443161010742 L 0 0 "
          "L 90.90000152587891 0 L 90.90000152587891 9.222447395324707 L "
          "9.217241287231445 9.222447395324707 L 9.217241287231445 "
          "45.40198516845703 L 90.90000152587891 45.40198516845703 L "
          "90.90000152587891 54.62443161010742 Z")},
        "M 90.90000152587891 81.86579895019531 L 7.629394644936838e-07 "
        "81.86579895019531 L 7.629394644936838e-07 27.24120140075684 L "
        "90.90000152587891 27.24120140075684 L 90.90000152587891 "
        "36.4635009765625 L 9.216900825500488 36.4635009765625 L "
        "9.216900825500488 72.64350128173828 L 90.90000152587891 "
        "72.64350128173828 L 90.90000152587891 81.86479949951172 L "
        "90.90000152587891 81.86579895019531 Z M 72.75240325927734 "
        "63.42119979858398 L 18.1476001739502 63.42119979858398 L "
        "18.1476001739502 45.40230178833008 L 72.75240325927734 "
        "45.40230178833008 L 72.75240325927734 63.42020034790039 L "
        "72.75240325927734 63.42119979858398 Z"),
      Compound(BooleanOperation::SUBTRACT, std::vector{
        Operand(
          Matrix(1, 0, 0, 1, -2.7490179377309687e-07, -8.662148331950448e-08),
          "M 0 0 H 11.000421524047852 V 11.000422477722168 H 0 V 0 Z"),
        Operand(Matrix(1, 0, 0, 1, 1.9656808376312256, 1.968702793121338),
          "M 3.53125 7.060546875 C 1.583984375 7.060546875 0.00048828125 "
          "5.474609375 0.00048828125 3.53125 C 0.00048828125 1.583984375 "
          "1.583984375 -0.001953125 3.53125 -0.001953125 L 3.53125 3.53125 L "
          "7.0693359375 3.53125 C 7.0693359375 5.478515625 5.4814453125 "
          "7.060546875 3.53125 7.060546875 Z")},
        "M 11 11 L 10.99862384796143 11 L 0 11 L 0 0 L 11 0 L 11 "
        "10.99862480163574 L 11 11 Z M 5.496286869049072 1.969059467315674 C "
        "3.549325942993164 1.969059467315674 1.965346455574036 "
        "3.553039312362671 1.965346455574036 5.5 C 1.965346455574036 "
        "7.446287155151367 3.549325942993164 9.029703140258789 "
        "5.496286869049072 9.029703140258789 C 7.447345733642578 "
        "9.029703140258789 9.034653663635254 7.446287155151367 "
        "9.034653663635254 5.5 L 5.496286869049072 5.5 L 5.496286869049072 "
        "1.969059467315674 Z"),
      Compound(BooleanOperation::SUBTRACT, std::vector{
        Operand(
          Matrix(1, 0, 0, 1, -6.40435686705132e-08, -9.822412039284245e-08),
          "M 0 0 H 7.545454502105713 V 7.545454502105713 H 0 V 0 Z"),
        Operand(Matrix(1, 0, 0, 1, -2749.076171875, 1276.06396484375),
          "M 2755.47607421875 -1273.706176757812 L 2750.340576171875 "
          "-1273.706176757812 L 2750.340576171875 -1274.649291992188 L "
          "2755.47607421875 -1274.649291992188 L 2755.47607421875 "
          "-1273.706176757812 Z"),
        Operand(Matrix(1, 0, 0, 1, -2749.076171875, 1277.950439453125),
          "M 2755.47607421875 -1273.706176757812 L 2750.340576171875 "
          "-1273.706176757812 L 2750.340576171875 -1274.649291992188 L "
          "2755.47607421875 -1274.649291992188 L 2755.47607421875 "
          "-1273.706176757812 Z"),
        Operand(Matrix(1, 0, 0, 1, -2749.076171875, 1279.8367919921875),
          "M 2755.47607421875 -1273.706176757812 L 2750.340576171875 "
          "-1273.706176757812 L 2750.340576171875 -1274.649291992188 L "
          "2755.47607421875 -1274.649291992188 L 2755.47607421875 "
          "-1273.706176757812 Z")},
        "M 7.545549392700195 7.545549392700195 L 7.54460620880127 "
        "7.545549392700195 L 0 7.545549392700195 L 0 0 L 7.545549392700195 0 "
        "L 7.545549392700195 7.54460620880127 L 7.545549392700195 "
        "7.545549392700195 Z M 1.264806747436523 5.187406063079834 L "
        "1.264806747436523 6.1304931640625 L 6.399583339691162 "
        "6.1304931640625 L 6.399583339691162 5.187406063079834 L "
        "1.264806747436523 5.187406063079834 Z M 1.264806747436523 "
        "3.301230907440186 L 1.264806747436523 4.244318008422852 L "
        "6.399583339691162 4.244318008422852 L 6.399583339691162 "
        "3.301230907440186 L 1.264806747436523 3.301230907440186 Z M "
        "1.264806747436523 1.415055513381958 L 1.264806747436523 "
        "2.358143329620361 L 6.399583339691162 2.358143329620361 L "
        "6.399583339691162 1.415055513381958 L 1.264806747436523 "
        "1.415055513381958 Z")
    };
    auto tolerance = 0.01;
    for(auto& compound : compounds) {
      auto operands = std::vector<Path>();
      for(auto& operand : compound.m_operands) {
        operands.push_back(
          transform(*parse_svg_path(operand.m_path), operand.m_transform));
      }
      auto result = combine(operands, compound.m_operation, tolerance);
      auto expected = combine(*parse_svg_path(compound.m_path), Path(),
        BooleanOperation::ADD, tolerance);
      auto area = get_area(expected);
      REQUIRE(get_area(result) == doctest::Approx(area).epsilon(0.005));
      REQUIRE(get_area(combine(result, expected, BooleanOperation::EXCLUDE,
        tolerance)) < 0.005 * area);
      REQUIRE(is_near(get_bounds(result), get_bounds(expected), 0.01));
      REQUIRE(flatten(result, tolerance).size() ==
        flatten(expected, tolerance).size());
    }
  }
}

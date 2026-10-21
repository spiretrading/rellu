#include <cmath>
#include <limits>
#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  Transform3d make_rotation_x(double degrees, Point center, Point offset) {
    auto angle = degrees * std::numbers::pi / 180;
    auto cosine = std::cos(angle);
    auto sine = std::sin(angle);
    auto transform = Transform3d();
    transform.m_matrix = std::array<double, 16>({1, 0, 0, offset.m_x,
      0, cosine, -sine, offset.m_y + center.m_y * (1 - cosine),
      0, sine, cosine, -sine * center.m_y, 0, 0, 0, 1});
    transform.m_rotation_x = degrees;
    transform.m_perspective_center = center;
    return transform;
  }

  Matrix get_planar(const Transform3d& transform) {
    auto& matrix = transform.m_matrix;
    return Matrix(
      matrix[0], matrix[4], matrix[1], matrix[5], matrix[3], matrix[7]);
  }

  Point project(const Transform3d& transform, Point point) {
    auto& matrix = transform.m_matrix;
    auto planar = get_planar(transform);
    auto mapped = planar.map(point);
    auto center = planar.map(transform.m_perspective_center);
    auto z = matrix[8] * point.m_x + matrix[9] * point.m_y + matrix[11];
    auto scale = PERSPECTIVE_DISTANCE / (PERSPECTIVE_DISTANCE - z);
    return center + (mapped - center) * scale;
  }
}

TEST_SUITE("NodeHeader") {
  TEST_CASE("equality") {
    auto header = NodeHeader();
    auto other = header;
    REQUIRE(header == other);
    other.m_transform_3d.emplace();
    REQUIRE(header != other);
    header.m_transform_3d.emplace();
    REQUIRE(header == other);
    other.m_transform_3d->m_matrix[11] = 4;
    REQUIRE(header != other);
    header.m_transform_3d->m_matrix[11] =
      std::numeric_limits<double>::quiet_NaN();
    other.m_transform_3d->m_matrix[11] =
      std::numeric_limits<double>::quiet_NaN();
    REQUIRE(header == other);
    other.m_transform_3d->m_rotation_y = 5;
    REQUIRE(header != other);
    other.m_transform_3d->m_rotation_y = 0;
    other.m_transform_3d->m_perspective_center = Point(1, 2);
    REQUIRE(header != other);
  }

  TEST_CASE("projection") {
    auto transform = make_rotation_x(-10, Point(119, 30.5), Point(8, 7));
    auto projection = to_projection(transform, get_planar(transform));
    for(auto point : {Point(0, 0), Point(238, 0), Point(0, 61),
        Point(238, 61), Point(119, 30.5), Point(40, 12)}) {
      REQUIRE(distance(projection.map(point), project(transform, point)) <
        1e-9);
    }
    auto center = projection.map(Point(119, 30.5));
    REQUIRE(distance(center, Point(127, 37.5)) < 1e-9);
    auto top = projection.map(Point(238, 0)).m_x -
      projection.map(Point(0, 0)).m_x;
    auto bottom = projection.map(Point(238, 61)).m_x -
      projection.map(Point(0, 61)).m_x;
    REQUIRE(top > 238);
    REQUIRE(bottom < 238);
  }

  TEST_CASE("derive_rotation") {
    REQUIRE(derive_rotation(Matrix()) == 0);
    REQUIRE(derive_rotation(Matrix(0, -1, 1, 0, 3, 22)) == 270);
    REQUIRE(derive_rotation(Matrix(-1, 0, 0, -1, 0, 0)) == 180);
    REQUIRE(derive_rotation(Matrix(0.8660253882408142, -0.5, 0.5,
      0.8660253882408142, 0, 0)) == doctest::Approx(330));
  }

  TEST_CASE("fit_rotation") {
    auto turned = Matrix(0, -1, 1, 0, 3, 22);
    REQUIRE(fit_rotation(-90, turned) == -90);
    REQUIRE(fit_rotation(270, turned) == 270);
    REQUIRE(fit_rotation(0, turned) == 270);
    REQUIRE(fit_rotation(-30, turned) == 270);
    REQUIRE(fit_rotation(360, Matrix()) == 360);
    REQUIRE(fit_rotation(-360, Matrix()) == -360);
    REQUIRE(fit_rotation(180.00001525878906, Matrix(-1, 0, 0, -1, 6, 208)) ==
      180.00001525878906);
    REQUIRE(fit_rotation(90, Matrix()) == 0);
  }

  TEST_CASE("finalize_rotation") {
    REQUIRE(finalize_rotation(360, Matrix()) == 0);
    REQUIRE(finalize_rotation(-360, Matrix()) == 0);
    REQUIRE(finalize_rotation(-90, Matrix(0, -1, 1, 0, 0, 0)) == -90);
    REQUIRE(finalize_rotation(-90, Matrix(0, 1, -1, 0, 0, 0)) == -270);
    REQUIRE(finalize_rotation(90, Matrix(0, -1, 1, 0, 0, 0)) == 270);
    auto turn = [] (double degrees) {
      return make_rotation(degrees * std::numbers::pi / 180);
    };
    REQUIRE(finalize_rotation(30, turn(150)) == doctest::Approx(150));
    REQUIRE(finalize_rotation(-30, turn(210)) == doctest::Approx(-150));
    REQUIRE(finalize_rotation(30, turn(-30)) == doctest::Approx(330));
    REQUIRE(finalize_rotation(270, turn(-90)) == 270);
  }

  TEST_CASE("transaction_rotation") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto header = NodeHeader();
    header.m_rotation = -90;
    header.m_transform = Matrix(0, -1, 1, 0, 0, 0);
    auto id = transaction.append(NodeId(), header, Group());
    REQUIRE(document.get(id).m_header.m_rotation == -90);
    header = document.get(id).m_header;
    header.m_transform.m_tx = 5;
    transaction.replace_header(header);
    REQUIRE(document.get(id).m_header.m_rotation == -90);
    header.m_transform = Matrix(0, 1, -1, 0, 5, 0);
    transaction.replace_header(header);
    REQUIRE(document.get(id).m_header.m_rotation == 90);
    auto unrotated = NodeHeader();
    unrotated.m_rotation = 30;
    auto other = transaction.append(NodeId(), unrotated, Group());
    REQUIRE(document.get(other).m_header.m_rotation == 0);
    auto tilted = NodeHeader();
    tilted.m_rotation = 30;
    tilted.m_transform_3d.emplace();
    auto spatial = transaction.append(NodeId(), tilted, Group());
    REQUIRE(document.get(spatial).m_header.m_rotation == 30);
    transaction.commit();
  }

  TEST_CASE("planar_entries") {
    auto transform = make_rotation_x(-10, Point(119, 30.5), Point(8, 7));
    auto planar = make_translation(20, 0) * get_planar(transform);
    auto projection = to_projection(transform, planar);
    auto unmoved = to_projection(transform, get_planar(transform));
    REQUIRE(distance(projection.map(Point(0, 0)),
      unmoved.map(Point(0, 0)) + Point(20, 0)) < 1e-9);
  }
}

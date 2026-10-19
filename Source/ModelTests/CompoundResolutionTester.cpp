#include <cmath>
#include <doctest/doctest.h>
#include "Rellu/Model/CompoundResolution.hpp"
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  constexpr auto TOLERANCE = 0.01;

  NodeHeader make_header(std::string name, const Matrix& transform) {
    auto header = Rellu::Tests::make_header(std::move(name));
    header.m_transform = transform;
    return header;
  }

  Shape make_compound(CompoundOperation operation) {
    auto shape = Shape();
    shape.m_geometry = CompoundShape(operation, Shared(Path()));
    return shape;
  }

  bool is_near(const Rect& left, const Rect& right) {
    return std::abs(left.m_x - right.m_x) < 1e-6 &&
      std::abs(left.m_y - right.m_y) < 1e-6 &&
      std::abs(left.m_width - right.m_width) < 1e-6 &&
      std::abs(left.m_height - right.m_height) < 1e-6;
  }
}

TEST_SUITE("CompoundResolution") {
  TEST_CASE("group_operand") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto compound = transaction.append(NodeId(), make_header("Compound"),
      make_compound(CompoundOperation::SUBTRACT));
    transaction.append(compound, make_header("Base"),
      make_rectangle_shape(10, 10));
    auto group = transaction.append(compound,
      make_header("Group", make_translation(2, 2)), Group());
    transaction.append(group, make_header("Left"),
      make_rectangle_shape(1, 1));
    transaction.append(group,
      make_header("Right", make_translation(3, 0)),
      make_rectangle_shape(1, 1));
    transaction.commit();
    auto& nodes = document.get_nodes();
    auto outline =
      get_operand_outline(nodes, document.get(group), TOLERANCE);
    REQUIRE(outline);
    REQUIRE(is_near(get_bounds(*outline), Rect(2, 2, 4, 1)));
    auto result = combine_operands(nodes, document.get(compound), TOLERANCE);
    REQUIRE(result);
    REQUIRE(is_near(get_bounds(*result), Rect(0, 0, 10, 10)));
    REQUIRE(!contains(*result, Point(2.5, 2.5)));
    REQUIRE(!contains(*result, Point(5.5, 2.5)));
    REQUIRE(contains(*result, Point(4, 2.5)));
  }

  TEST_CASE("reference_operand") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto master = transaction.insert_root(make_header("Master"), Group());
    auto shape = transaction.append(master,
      make_header("Shape", make_translation(1, 2)),
      make_rectangle_shape(4, 4));
    auto compound = transaction.append(NodeId(), make_header("Compound"),
      make_compound(CompoundOperation::UNION));
    auto plain = transaction.append(
      compound, make_header("Plain"), Reference(shape));
    auto moved = transaction.append(compound, make_header("Moved"),
      Reference(shape, make_translation(10, 0)));
    transaction.commit();
    auto& nodes = document.get_nodes();
    REQUIRE(is_near(get_bounds(
      *get_operand_outline(nodes, document.get(plain), TOLERANCE)),
      Rect(1, 2, 4, 4)));
    REQUIRE(is_near(get_bounds(
      *get_operand_outline(nodes, document.get(moved), TOLERANCE)),
      Rect(10, 0, 4, 4)));
    auto result = combine_operands(nodes, document.get(compound), TOLERANCE);
    REQUIRE(result);
    REQUIRE(is_near(get_bounds(*result), Rect(1, 0, 13, 6)));
  }

  TEST_CASE("nested_compound") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto outer = transaction.append(NodeId(), make_header("Outer"),
      make_compound(CompoundOperation::UNION));
    auto inner_shape = make_compound(CompoundOperation::UNION);
    std::get<CompoundShape>(inner_shape.m_geometry).m_path =
      Shared(make_rectangle(Rect(0, 0, 2, 2)));
    auto inner = transaction.append(outer,
      make_header("Inner", make_translation(5, 5)), inner_shape);
    transaction.append(inner, make_header("Operand"),
      make_rectangle_shape(3, 3));
    transaction.commit();
    auto& nodes = document.get_nodes();
    REQUIRE(is_near(get_bounds(
      *combine_operands(nodes, document.get(outer), TOLERANCE)),
      Rect(5, 5, 2, 2)));
    auto nested = [&] (const Node& node) {
      return *combine_operands(nodes, node, TOLERANCE);
    };
    REQUIRE(is_near(get_bounds(
      *combine_operands(nodes, document.get(outer), TOLERANCE, nested)),
      Rect(5, 5, 3, 3)));
  }

  TEST_CASE("unsupported_operands") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto compound = transaction.append(NodeId(), make_header("Compound"),
      make_compound(CompoundOperation::UNION));
    auto empty = transaction.append(NodeId(), make_header("Empty"),
      make_compound(CompoundOperation::UNION));
    auto group = Group();
    group.m_mask = Mask();
    auto masked = transaction.append(compound, make_header("Masked"), group);
    auto text = transaction.append(NodeId(), make_header("Text"),
      make_text(u"Text"));
    auto plain = transaction.append(NodeId(), make_header("Plain"), Group());
    transaction.append(plain, make_header("Text"), make_text(u"Text"));
    transaction.commit();
    auto& nodes = document.get_nodes();
    REQUIRE(!get_operand_outline(nodes, document.get(masked), TOLERANCE));
    REQUIRE(!get_operand_outline(nodes, document.get(text), TOLERANCE));
    REQUIRE(!get_operand_outline(nodes, document.get(plain), TOLERANCE));
    REQUIRE(!combine_operands(nodes, document.get(compound), TOLERANCE));
    REQUIRE(!combine_operands(nodes, document.get(empty), TOLERANCE));
    REQUIRE(!combine_operands(nodes, document.get(text), TOLERANCE));
  }

  TEST_CASE("boolean_operations") {
    REQUIRE(to_boolean_operation(CompoundOperation::UNION) ==
      BooleanOperation::ADD);
    REQUIRE(to_boolean_operation(CompoundOperation::SUBTRACT) ==
      BooleanOperation::SUBTRACT);
    REQUIRE(to_boolean_operation(CompoundOperation::INTERSECT) ==
      BooleanOperation::INTERSECT);
    REQUIRE(to_boolean_operation(CompoundOperation::EXCLUDE) ==
      BooleanOperation::EXCLUDE);
  }
}

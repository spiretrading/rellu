#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/Compounds.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  const auto TOLERANCE = 0.01;
  const auto SQUARE = Rect(0, 0, 20, 20);

  class ShapeCommand : public Command {
    public:
      ShapeCommand(const NodeId& id, Shape shape)
        : m_id(id),
          m_shape(std::move(shape)) {}

      std::string get_label() const override {
        return "Shape";
      }

      void apply(const Document& document, Transaction& transaction) override {
        transaction.replace_payload(m_id, m_shape);
      }

    private:
      NodeId m_id;
      Shape m_shape;
  };

  NodeHeader make_header(const Matrix& transform = Matrix()) {
    auto header = NodeHeader();
    header.m_transform = transform;
    return header;
  }

  Shape make_rectangle_shape(const Rect& bounds) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(bounds);
    shape.m_style.m_fill = SolidFill(make_color(255, 0, 0));
    return shape;
  }

  Shape make_compound(CompoundOperation operation, const Path& result) {
    auto shape = Shape();
    shape.m_geometry = CompoundShape(operation, Shared(result));
    shape.m_style.m_fill = SolidFill(make_color(0, 0, 255));
    return shape;
  }

  const Path& get_result(const Document& document, const NodeId& id) {
    return *std::get<CompoundShape>(
      document.get(id).get_shape()->m_geometry).m_path;
  }

  Path make_union(const Rect& left, const Rect& right, Point offset) {
    auto operands = std::vector<Path>({make_rectangle(left), transform(
      make_rectangle(right), make_translation(offset.m_x, offset.m_y))});
    return combine(operands, BooleanOperation::ADD, TOLERANCE);
  }

  struct Sample {
    NodeId m_compound;
    NodeId m_left;
    NodeId m_right;
    Path m_stored;
  };

  Sample make_sample(Document& document) {
    auto sample = Sample();
    sample.m_stored = make_rectangle(Rect(0, 0, 1, 1));
    auto transaction = Transaction(document);
    sample.m_compound = transaction.append(NodeId(),
      make_header(make_translation(100, 100)),
      make_compound(CompoundOperation::UNION, sample.m_stored));
    sample.m_left = transaction.append(sample.m_compound, make_header(),
      make_rectangle_shape(Rect(0, 0, 20, 20)));
    sample.m_right = transaction.append(sample.m_compound,
      make_header(make_translation(10, 0)),
      make_rectangle_shape(Rect(0, 0, 20, 20)));
    transaction.commit();
    document.clear_history();
    return sample;
  }
}

TEST_SUITE("Compounds") {
  TEST_CASE("operand_move") {
    auto document = Document();
    auto sample = make_sample(document);
    auto history = History(document);
    REQUIRE(history.execute(std::make_unique<TranslateCommand>(
      std::vector{sample.m_right}, Point(15, 5))));
    auto expected = make_union(SQUARE, SQUARE, Point(25, 5));
    REQUIRE(get_result(document, sample.m_compound) == expected);
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.undo());
    REQUIRE(get_result(document, sample.m_compound) == sample.m_stored);
    REQUIRE(document.get(sample.m_right).m_header.m_transform ==
      make_translation(10, 0));
    REQUIRE(history.redo());
    REQUIRE(get_result(document, sample.m_compound) == expected);
    REQUIRE(document.get(sample.m_right).m_header.m_transform ==
      make_translation(25, 5));
  }

  TEST_CASE("operand_resize") {
    auto document = Document();
    auto sample = make_sample(document);
    auto history = History(document);
    REQUIRE(history.execute(std::make_unique<ShapeCommand>(
      sample.m_left, make_rectangle_shape(Rect(0, 0, 5, 40)))));
    auto expected = make_union(Rect(0, 0, 5, 40), SQUARE, Point(10, 0));
    REQUIRE(get_result(document, sample.m_compound) == expected);
    REQUIRE(history.execute(std::make_unique<TransformCommand>(
      std::vector{sample.m_right},
      make_translation(110, 100) * make_scale(2, 1) *
        make_translation(-110, -100), "Resize")));
    expected =
      make_union(Rect(0, 0, 5, 40), Rect(0, 0, 40, 20), Point(10, 0));
    REQUIRE(get_result(document, sample.m_compound) == expected);
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(get_result(document, sample.m_compound) == sample.m_stored);
  }

  TEST_CASE("merge") {
    auto document = Document();
    auto sample = make_sample(document);
    auto history = History(document);
    for(auto i = 1; i <= 3; ++i) {
      REQUIRE(history.merge(std::make_unique<TranslateCommand>(
        std::vector{sample.m_right}, Point(2, 0))));
      auto expected = make_union(SQUARE, SQUARE, Point(10 + 2 * i, 0));
      REQUIRE(get_result(document, sample.m_compound) == expected);
    }
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.undo());
    REQUIRE(get_result(document, sample.m_compound) == sample.m_stored);
    REQUIRE(!history.can_undo());
  }

  TEST_CASE("nested") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto stored = make_rectangle(Rect(0, 0, 1, 1));
    auto outer = transaction->append(NodeId(), make_header(),
      make_compound(CompoundOperation::SUBTRACT, stored));
    transaction->append(outer, make_header(),
      make_rectangle_shape(Rect(0, 0, 100, 100)));
    auto inner = transaction->append(outer,
      make_header(make_translation(10, 10)),
      make_compound(CompoundOperation::UNION, stored));
    transaction->append(inner, make_header(),
      make_rectangle_shape(Rect(0, 0, 20, 20)));
    auto moved = transaction->append(inner,
      make_header(make_translation(30, 0)),
      make_rectangle_shape(Rect(0, 0, 20, 20)));
    transaction->commit();
    document.clear_history();
    auto history = History(document);
    REQUIRE(history.execute(std::make_unique<TranslateCommand>(
      std::vector{moved}, Point(0, 40))));
    auto inner_result = make_union(SQUARE, SQUARE, Point(30, 40));
    REQUIRE(get_result(document, inner) == inner_result);
    auto paths = std::vector<Path>({make_rectangle(Rect(0, 0, 100, 100)),
      transform(inner_result, make_translation(10, 10))});
    REQUIRE(get_result(document, outer) ==
      combine(paths, BooleanOperation::SUBTRACT, TOLERANCE));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.undo());
    REQUIRE(get_result(document, inner) == stored);
    REQUIRE(get_result(document, outer) == stored);
    REQUIRE(history.redo());
    REQUIRE(get_result(document, inner) == inner_result);
  }

  TEST_CASE("group_operand") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto stored = make_rectangle(Rect(0, 0, 1, 1));
    auto compound = transaction->append(NodeId(), make_header(),
      make_compound(CompoundOperation::UNION, stored));
    transaction->append(compound, make_header(),
      make_rectangle_shape(SQUARE));
    auto group = transaction->append(compound,
      make_header(make_translation(10, 0)), Group());
    auto inner = transaction->append(group, make_header(),
      make_rectangle_shape(SQUARE));
    transaction->commit();
    document.clear_history();
    auto history = History(document);
    REQUIRE(history.execute(std::make_unique<TranslateCommand>(
      std::vector{inner}, Point(5, 5))));
    REQUIRE(get_result(document, compound) ==
      make_union(SQUARE, SQUARE, Point(15, 5)));
    REQUIRE(history.execute(std::make_unique<TranslateCommand>(
      std::vector{group}, Point(10, 0))));
    REQUIRE(get_result(document, compound) ==
      make_union(SQUARE, SQUARE, Point(25, 5)));
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(get_result(document, compound) == stored);
  }

  TEST_CASE("reference_operand") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto stored = make_rectangle(Rect(0, 0, 1, 1));
    auto master = transaction->insert_root(make_header(), Group());
    auto source = transaction->append(master,
      make_header(make_translation(10, 0)), make_rectangle_shape(SQUARE));
    auto compound = transaction->append(NodeId(), make_header(),
      make_compound(CompoundOperation::UNION, stored));
    transaction->append(compound, make_header(),
      make_rectangle_shape(SQUARE));
    transaction->append(compound, make_header(), Reference(source));
    transaction->commit();
    document.clear_history();
    auto history = History(document);
    REQUIRE(history.execute(std::make_unique<ShapeCommand>(
      source, make_rectangle_shape(Rect(0, 0, 20, 30)))));
    REQUIRE(get_result(document, compound) == make_union(
      SQUARE, Rect(0, 0, 20, 30), Point(10, 0)));
    REQUIRE(history.undo());
    REQUIRE(get_result(document, compound) == stored);
  }

  TEST_CASE("filter") {
    auto document = Document();
    auto sample = make_sample(document);
    auto history = History(document);
    history.set_compound_filter([&] (const Node& node) {
      return node.get_id() != sample.m_compound;
    });
    REQUIRE(history.execute(std::make_unique<TranslateCommand>(
      std::vector{sample.m_right}, Point(15, 5))));
    REQUIRE(get_result(document, sample.m_compound) == sample.m_stored);
  }

  TEST_CASE("operand_removal") {
    auto document = Document();
    auto sample = make_sample(document);
    auto history = History(document);
    REQUIRE(history.execute(
      std::make_unique<DeleteCommand>(std::vector{sample.m_left})));
    auto operands = std::vector<Path>(
      {transform(make_rectangle(SQUARE), make_translation(10, 0))});
    REQUIRE(get_result(document, sample.m_compound) ==
      combine(operands, BooleanOperation::ADD, TOLERANCE));
    REQUIRE(history.execute(
      std::make_unique<DeleteCommand>(std::vector{sample.m_right})));
    REQUIRE(get_result(document, sample.m_compound) ==
      combine(operands, BooleanOperation::ADD, TOLERANCE));
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(get_result(document, sample.m_compound) == sample.m_stored);
  }

  TEST_CASE("unrelated_edits") {
    auto document = Document();
    auto sample = make_sample(document);
    auto history = History(document);
    auto recolored = make_rectangle_shape(Rect(0, 0, 20, 20));
    recolored.m_style.m_fill = SolidFill(make_color(0, 255, 0));
    REQUIRE(history.execute(
      std::make_unique<ShapeCommand>(sample.m_left, recolored)));
    REQUIRE(history.execute(std::make_unique<TranslateCommand>(
      std::vector{sample.m_compound}, Point(15, 5))));
    REQUIRE(get_result(document, sample.m_compound) == sample.m_stored);
    auto changes = ChangeSet();
    changes.m_modified.push_back(
      NodeChange(sample.m_left, ChangeMask::STYLE | ChangeMask::NAME));
    REQUIRE(!refresh_compounds(document, changes, CompoundFilter()));
    changes.m_modified.push_back(
      NodeChange(sample.m_right, ChangeMask::TRANSFORM));
    REQUIRE(refresh_compounds(document, changes, CompoundFilter()));
    REQUIRE(get_result(document, sample.m_compound) ==
      make_union(SQUARE, SQUARE, Point(10, 0)));
  }
}

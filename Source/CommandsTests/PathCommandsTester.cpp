#include <cmath>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/PathCommands.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Board {
    NodeId m_artboard;
    NodeId m_bottom;
    NodeId m_top;
    NodeId m_ellipse;
    NodeId m_line;
    NodeId m_path;
    NodeId m_text;
    NodeId m_loose;
  };

  Board build_board(Document& document) {
    auto board = Board();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 400, 300);
    board.m_artboard =
      transaction.append(NodeId(), make_header("Artboard"), artboard);
    auto bottom = make_rectangle_shape(40, 40);
    bottom.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
    board.m_bottom = transaction.append(board.m_artboard,
      make_header("Bottom", make_translation(10, 10)), bottom);
    auto top = make_rectangle_shape(40, 40);
    top.m_style.m_fill = SolidFill(make_color(0, 255, 0));
    board.m_top = transaction.append(board.m_artboard,
      make_header("Top", make_translation(30, 30)), top);
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(10, 10), 10, 10);
    board.m_ellipse = transaction.append(board.m_artboard,
      make_header("Ellipse", make_translation(200, 10)), ellipse);
    auto line = Shape();
    line.m_geometry = LineShape(Point(0, 0), Point(50, 20));
    board.m_line = transaction.append(
      board.m_artboard, make_header("Line"), line);
    auto path = Shape();
    path.m_geometry = PathShape(
      Shared(make_line(Point(0, 0), Point(5, 5))), FillRule::NON_ZERO);
    board.m_path = transaction.append(
      board.m_artboard, make_header("Path"), path);
    board.m_text = transaction.append(
      board.m_artboard, make_header("Text"), make_text(u"Hi"));
    board.m_loose = transaction.append(NodeId(),
      make_header("Loose", make_translation(700, 700)),
      make_rectangle_shape(10, 10));
    transaction.commit();
    document.clear_history();
    return board;
  }

  const Shape& get_shape(const Document& document, const NodeId& id) {
    return *document.get(id).get_shape();
  }

  const CompoundShape& get_compound(
      const Document& document, const NodeId& id) {
    return std::get<CompoundShape>(get_shape(document, id).m_geometry);
  }

  CommandRegistry make_registry() {
    auto registry = CommandRegistry();
    add_path_commands(registry);
    return registry;
  }
}

TEST_SUITE("PathCommands") {
  TEST_CASE("default_style") {
    auto style = make_default_path_style();
    REQUIRE(std::holds_alternative<NoFill>(style.m_fill));
    REQUIRE(style.m_stroke);
    REQUIRE(style.m_stroke->m_color == make_color(112, 112, 112));
    REQUIRE(style.m_stroke->m_width == 1);
    REQUIRE(style.m_stroke->m_alignment == StrokeAlignment::CENTER);
    REQUIRE(style.m_effects.empty());
  }

  TEST_CASE("labels") {
    REQUIRE(get_operation_label(CompoundOperation::UNION) == "Add");
    REQUIRE(get_operation_label(CompoundOperation::SUBTRACT) == "Subtract");
    REQUIRE(get_operation_label(CompoundOperation::INTERSECT) ==
      "Intersect");
    REQUIRE(get_operation_label(CompoundOperation::EXCLUDE) ==
      "Exclude Overlap");
    REQUIRE(get_name_prefix(CompoundOperation::UNION) == "Union ");
    REQUIRE(get_name_prefix(CompoundOperation::SUBTRACT) == "Subtraction ");
    REQUIRE(get_name_prefix(CompoundOperation::INTERSECT) ==
      "Intersection ");
    REQUIRE(get_name_prefix(CompoundOperation::EXCLUDE) == "Exclusion ");
  }

  TEST_CASE("set_path") {
    auto document = Document();
    auto board = build_board(document);
    auto history = History(document);
    auto style = get_shape(document, board.m_bottom).m_style;
    auto outline = make_rectangle(Rect(0, 0, 40, 60));
    auto command = std::make_unique<SetPathCommand>("Edit Path",
      board.m_bottom, PathShape(Shared(outline), FillRule::EVEN_ODD));
    REQUIRE(command->get_node() == board.m_bottom);
    require_undoable(history, std::move(command));
    auto& shape = get_shape(document, board.m_bottom);
    REQUIRE(shape.m_geometry == ShapeGeometry(
      PathShape(Shared(outline), FillRule::EVEN_ODD)));
    REQUIRE(shape.m_style == style);
    REQUIRE(document.get(board.m_bottom).m_header.m_transform ==
      make_translation(10, 10));
    auto first = std::make_unique<SetPathCommand>("Edit Path",
      board.m_bottom, PathShape(Shared(make_rectangle(Rect(0, 0, 1, 1)))));
    REQUIRE(first->merge(SetPathCommand("Edit Path", board.m_bottom,
      PathShape(Shared(make_rectangle(Rect(0, 0, 2, 2)))))));
    REQUIRE(*first->get_geometry().m_path == make_rectangle(Rect(0, 0, 2, 2)));
    REQUIRE(!first->merge(SetPathCommand("Edit Path", board.m_top,
      PathShape(Shared(outline)))));
    REQUIRE_THROWS_AS(history.execute(std::make_unique<SetPathCommand>(
      "Edit Path", board.m_text, PathShape(Shared(outline)))),
      std::invalid_argument);
  }

  TEST_CASE("convert") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = make_registry();
    REQUIRE(is_convertible_to_path(document, board.m_bottom));
    REQUIRE(is_convertible_to_path(document, board.m_line));
    REQUIRE(!is_convertible_to_path(document, board.m_path));
    REQUIRE(!is_convertible_to_path(document, board.m_text));
    REQUIRE(!is_convertible_to_path(document, NodeId()));
    REQUIRE(!registry.make("object.path.convert", document,
      std::vector{board.m_path, board.m_text}));
    auto history = History(document);
    auto command = registry.make("object.path.convert", document,
      std::vector{board.m_bottom, board.m_ellipse, board.m_line,
        board.m_path});
    REQUIRE(command);
    REQUIRE(command->get_label() == "Convert to Path");
    auto rectangle = to_path(get_shape(document, board.m_bottom).m_geometry);
    auto ellipse = to_path(get_shape(document, board.m_ellipse).m_geometry);
    require_undoable(history, std::move(command));
    REQUIRE(get_shape(document, board.m_bottom).m_geometry ==
      ShapeGeometry(PathShape(Shared(rectangle))));
    auto& converted = *std::get<PathShape>(
      get_shape(document, board.m_ellipse).m_geometry).m_path;
    REQUIRE(converted.get_segments().size() == ellipse.get_segments().size());
    for(auto i = std::size_t(0); i != ellipse.get_segments().size(); ++i) {
      auto& segment = converted.get_segments()[i];
      REQUIRE(segment.m_type == ellipse.get_segments()[i].m_type);
      REQUIRE(segment.m_points == ellipse.get_segments()[i].m_points);
      REQUIRE(!segment.m_is_arc);
    }
    REQUIRE(get_shape(document, board.m_line).m_geometry == ShapeGeometry(
      PathShape(Shared(make_line(Point(0, 0), Point(50, 20))))));
    REQUIRE(document.get(board.m_bottom).m_header.m_name == "Bottom");
  }

  TEST_CASE("outline_stroke") {
    auto document = Document();
    auto board = build_board(document);
    {
      auto line = get_shape(document, board.m_line);
      line.m_style.m_stroke = Stroke(make_color(0, 0, 255), 4);
      auto transaction = Transaction(document);
      transaction.replace_payload(board.m_line, line);
      transaction.commit();
      document.clear_history();
    }
    auto registry = make_registry();
    REQUIRE(is_outline_strokable(document, board.m_bottom));
    REQUIRE(is_outline_strokable(document, board.m_line));
    REQUIRE(!is_outline_strokable(document, board.m_top));
    REQUIRE(!is_outline_strokable(document, board.m_text));
    REQUIRE(!registry.make("object.path.outline_stroke", document,
      std::vector{board.m_top, board.m_text}));
    auto history = History(document);
    auto command = registry.make("object.path.outline_stroke", document,
      std::vector{board.m_bottom, board.m_line, board.m_top});
    REQUIRE(command->get_label() == "Outline Stroke");
    auto recorded = require_undoable(history, std::move(command));
    auto selection = *recorded->get_selection();
    REQUIRE(selection.size() == 4);
    REQUIRE(selection[0] == board.m_bottom);
    REQUIRE(selection[2] == board.m_line);
    REQUIRE(selection[3] == board.m_top);
    auto& bottom = get_shape(document, board.m_bottom);
    REQUIRE(!bottom.m_style.m_stroke->m_is_enabled);
    REQUIRE(std::holds_alternative<RectangleShape>(bottom.m_geometry));
    auto& outline = document.get(selection[1]);
    REQUIRE(outline.m_header.m_name == "Bottom - Outline");
    REQUIRE(outline.m_header.m_transform == make_translation(10, 10));
    REQUIRE(get_index(document, selection[1]) ==
      get_index(document, board.m_bottom) + 1);
    auto& style = outline.get_shape()->m_style;
    REQUIRE(style.m_fill == Fill(SolidFill(make_color(0, 0, 0))));
    REQUIRE(!style.m_stroke->m_is_enabled);
    auto& path = *std::get<PathShape>(outline.get_shape()->m_geometry).m_path;
    REQUIRE(get_bounds(path) == Rect(-1, -1, 42, 42));
    REQUIRE(!contains(path, Point(20, 20)));
    REQUIRE(contains(path, Point(0, 20)));
    auto& line = document.get(board.m_line);
    REQUIRE(line.m_header.m_name == "Line");
    auto& line_shape = *line.get_shape();
    REQUIRE(line_shape.m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 255))));
    REQUIRE(get_bounds(*std::get<PathShape>(line_shape.m_geometry).m_path).
      m_height > 20);
  }

  TEST_CASE("outline_stroke_curves") {
    auto document = Document();
    auto board = build_board(document);
    {
      auto ellipse = get_shape(document, board.m_ellipse);
      ellipse.m_style.m_fill = SolidFill(make_color(255, 255, 255));
      ellipse.m_style.m_stroke =
        Stroke(make_color(0, 0, 0), 1, StrokeAlignment::INSIDE);
      auto transaction = Transaction(document);
      transaction.replace_payload(board.m_ellipse, ellipse);
      transaction.commit();
      document.clear_history();
    }
    auto history = History(document);
    auto recorded = require_undoable(history, make_registry().make(
      "object.path.outline_stroke", document, std::vector{board.m_ellipse}));
    auto selection = *recorded->get_selection();
    REQUIRE(selection.size() == 2);
    auto& path = *std::get<PathShape>(
      get_shape(document, selection[1]).m_geometry).m_path;
    auto moves = 0;
    auto cubics = 0;
    for(auto& segment : path.get_segments()) {
      moves += segment.m_type == SegmentType::MOVE;
      cubics += segment.m_type == SegmentType::CUBIC;
      REQUIRE(segment.m_type != SegmentType::LINE);
    }
    REQUIRE(moves == 2);
    REQUIRE(cubics >= 8);
    REQUIRE(cubics <= 80);
    for(auto& polyline : flatten(path, 0.001)) {
      for(auto& point : polyline.m_points) {
        auto radius = distance(point, Point(10, 10));
        REQUIRE((std::abs(radius - 10) < 0.03 || std::abs(radius - 9) < 0.03));
      }
    }
  }

  TEST_CASE("outline_stroke_text") {
    auto document = Document();
    auto board = build_board(document);
    auto set_text = [&] (bool is_filled) {
      auto text = *document.get(board.m_text).get_text();
      text.m_style.m_fill = is_filled ?
        Fill(SolidFill(make_color(0, 0, 0))) : Fill(NoFill());
      text.m_style.m_stroke = Stroke(make_color(255, 0, 0), 2);
      auto transaction = Transaction(document);
      transaction.replace_payload(board.m_text, text);
      transaction.commit();
      document.clear_history();
    };
    set_text(true);
    REQUIRE(is_outline_strokable(document, board.m_text));
    auto registry = CommandRegistry();
    add_path_commands(registry, [] (const auto&) {
      return make_rectangle(Rect(0, 0, 20, 10));
    });
    auto history = History(document);
    auto recorded = require_undoable(history, registry.make(
      "object.path.outline_stroke", document, std::vector{board.m_text}));
    auto selection = *recorded->get_selection();
    REQUIRE(selection.size() == 2);
    REQUIRE(selection[0] == board.m_text);
    REQUIRE(!document.get(board.m_text).get_text()->m_style.m_stroke->
      m_is_enabled);
    auto& outline = document.get(selection[1]);
    REQUIRE(outline.m_header.m_name == "Text - Outline");
    REQUIRE(outline.get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(255, 0, 0))));
    REQUIRE(get_bounds(*std::get<PathShape>(
      outline.get_shape()->m_geometry).m_path) == Rect(-1, -1, 22, 12));
    REQUIRE(history.undo());
    set_text(false);
    auto replacement = History(document);
    require_undoable(replacement, registry.make(
      "object.path.outline_stroke", document, std::vector{board.m_text}));
    auto& replaced = document.get(board.m_text);
    REQUIRE(replaced.get_shape());
    REQUIRE(replaced.m_header.m_name == "Text");
    REQUIRE(replaced.m_parent == board.m_artboard);
    REQUIRE(!make_registry().make("object.path.outline_stroke", document,
      std::vector{board.m_top}));
  }

  TEST_CASE("outline_stroke_compound") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = make_registry();
    auto history = History(document);
    auto command = history.execute(registry.make("object.path.add",
      document, std::vector{board.m_bottom, board.m_top}));
    auto id = static_cast<const BooleanCommand*>(command)->get_compound();
    {
      auto compound = get_shape(document, id);
      compound.m_style.m_fill = NoFill();
      compound.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
      auto transaction = Transaction(document);
      transaction.replace_payload(id, compound);
      transaction.commit();
    }
    REQUIRE(is_outline_strokable(document, id));
    REQUIRE(!is_outline_strokable(document, board.m_bottom));
    require_undoable(history, registry.make(
      "object.path.outline_stroke", document, std::vector{id}));
    REQUIRE(std::holds_alternative<PathShape>(get_shape(document, id).
      m_geometry));
    REQUIRE(document.get(id).m_children.empty());
    REQUIRE(!document.find(board.m_bottom));
    REQUIRE(get_bounds(*std::get<PathShape>(get_shape(document, id).
      m_geometry).m_path) == Rect(9, 9, 62, 62));
  }

  TEST_CASE("outline_stroke_name") {
    auto document = Document();
    auto board = build_board(document);
    {
      auto header = document.get(board.m_bottom).m_header;
      header.m_name.clear();
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.commit();
      document.clear_history();
    }
    set_language(Language::GERMAN);
    auto history = History(document);
    auto recorded = require_undoable(history, make_registry().make(
      "object.path.outline_stroke", document, std::vector{board.m_bottom}));
    set_language(Language::ENGLISH);
    auto selection = *recorded->get_selection();
    REQUIRE(selection.size() == 2);
    REQUIRE(document.get(selection[1]).m_header.m_name == "Rechteck - Kontur");
  }

  TEST_CASE("outline_stroke_group") {
    auto document = Document();
    auto board = build_board(document);
    {
      auto header = document.get(board.m_bottom).m_header;
      header.m_opacity = 0.5;
      auto shape = get_shape(document, board.m_bottom);
      shape.m_style.m_effects.push_back(
        DropShadow(Point(0, 3), 6, make_color(0, 0, 0), true));
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.replace_payload(board.m_bottom, shape);
      transaction.commit();
      document.clear_history();
    }
    auto registry = make_registry();
    auto history = History(document);
    auto recorded = require_undoable(history, registry.make(
      "object.path.outline_stroke", document, std::vector{board.m_bottom}));
    auto selection = *recorded->get_selection();
    REQUIRE(selection.size() == 1);
    auto& group = document.get(selection.front());
    REQUIRE(group.get_group());
    REQUIRE(group.m_header.m_name == "Bottom");
    REQUIRE(group.m_header.m_opacity == 0.5);
    REQUIRE(group.m_parent == board.m_artboard);
    REQUIRE(group.m_children.size() == 2);
    REQUIRE(group.m_children.front() == board.m_bottom);
    auto& bottom = document.get(board.m_bottom);
    REQUIRE(bottom.m_header.m_opacity == 1);
    REQUIRE(get_world_transform(document, board.m_bottom) ==
      make_translation(110, 110));
    REQUIRE(!std::get<DropShadow>(
      bottom.get_shape()->m_style.m_effects.front()).m_is_visible);
    auto& outline = document.get(group.m_children.back());
    REQUIRE(outline.m_header.m_opacity == 1);
    REQUIRE(!std::get<DropShadow>(
      outline.get_shape()->m_style.m_effects.front()).m_is_visible);
  }

  TEST_CASE("convert_compound") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = make_registry();
    auto history = History(document);
    auto compound = history.execute(registry.make("object.path.subtract",
      document, std::vector{board.m_bottom, board.m_top}));
    REQUIRE(compound);
    auto id = static_cast<const BooleanCommand*>(compound)->get_compound();
    auto result = *get_compound(document, id).m_path;
    require_undoable(history,
      registry.make("object.path.convert", document, std::vector{id}));
    REQUIRE(get_shape(document, id).m_geometry ==
      ShapeGeometry(PathShape(Shared(result))));
    REQUIRE(document.get(id).m_children.empty());
    REQUIRE(!document.find(board.m_bottom));
    REQUIRE(!document.find(board.m_top));
  }

  TEST_CASE("boolean") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = make_registry();
    auto history = History(document);
    auto bottom_style = get_shape(document, board.m_bottom).m_style;
    auto command = registry.make("object.path.add", document,
      std::vector{board.m_top, board.m_bottom});
    REQUIRE(command);
    REQUIRE(command->get_label() == "Add");
    auto recorded = require_undoable(history, std::move(command));
    auto id = static_cast<const BooleanCommand*>(recorded)->get_compound();
    REQUIRE(recorded->get_selection() == std::vector{id});
    auto& node = document.get(id);
    REQUIRE(node.m_parent == board.m_artboard);
    REQUIRE(node.m_header.m_name == "Union 1");
    REQUIRE(node.m_header.m_transform == Matrix());
    REQUIRE(node.m_children == std::vector{board.m_bottom, board.m_top});
    REQUIRE(document.get_children(board.m_artboard)[0] == id);
    REQUIRE(document.get(board.m_bottom).m_header.m_transform ==
      make_translation(10, 10));
    REQUIRE(document.get(board.m_top).m_header.m_transform ==
      make_translation(30, 30));
    auto& shape = get_shape(document, id);
    REQUIRE(shape.m_style == bottom_style);
    auto& compound = get_compound(document, id);
    REQUIRE(compound.m_operation == CompoundOperation::UNION);
    auto operands = std::vector<Path>();
    for(auto& child : node.m_children) {
      auto& operand = document.get(child);
      operands.push_back(transform(to_path(operand.get_shape()->m_geometry),
        operand.m_header.m_transform));
    }
    REQUIRE(*compound.m_path ==
      combine(operands, BooleanOperation::ADD, 0.01));
    REQUIRE(contains(*compound.m_path, Point(15, 15)));
    REQUIRE(contains(*compound.m_path, Point(65, 65)));
    REQUIRE(!contains(*compound.m_path, Point(65, 15)));
    auto second = history.execute(registry.make("object.path.add", document,
      std::vector{board.m_ellipse, board.m_line}));
    REQUIRE(document.get(static_cast<const BooleanCommand*>(
      second)->get_compound()).m_header.m_name == "Union 2");
  }

  TEST_CASE("operations") {
    for(auto [name, operation, prefix, inside, outside] : {
        std::tuple("object.path.subtract", CompoundOperation::SUBTRACT,
          "Subtraction 1", Point(15, 15), Point(45, 45)),
        std::tuple("object.path.intersect", CompoundOperation::INTERSECT,
          "Intersection 1", Point(45, 45), Point(15, 15)),
        std::tuple("object.path.exclude", CompoundOperation::EXCLUDE,
          "Exclusion 1", Point(65, 65), Point(45, 45))}) {
      auto document = Document();
      auto board = build_board(document);
      auto registry = make_registry();
      auto history = History(document);
      auto recorded = history.execute(registry.make(
        name, document, std::vector{board.m_bottom, board.m_top}));
      REQUIRE(recorded);
      REQUIRE(recorded->get_label() == get_operation_label(operation));
      auto id = static_cast<const BooleanCommand*>(recorded)->get_compound();
      REQUIRE(document.get(id).m_header.m_name == prefix);
      auto& compound = get_compound(document, id);
      REQUIRE(compound.m_operation == operation);
      REQUIRE(contains(*compound.m_path, inside));
      REQUIRE(!contains(*compound.m_path, outside));
    }
  }

  TEST_CASE("operation_change") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = make_registry();
    auto history = History(document);
    auto recorded = history.execute(registry.make("object.path.add",
      document, std::vector{board.m_bottom, board.m_top}));
    auto id = static_cast<const BooleanCommand*>(recorded)->get_compound();
    auto command =
      registry.make("object.path.intersect", document, std::vector{id});
    REQUIRE(dynamic_cast<SetCompoundOperationCommand*>(command.get()));
    REQUIRE(command->get_label() == "Intersect");
    require_undoable(history, std::move(command));
    auto& compound = get_compound(document, id);
    REQUIRE(compound.m_operation == CompoundOperation::INTERSECT);
    REQUIRE(contains(*compound.m_path, Point(45, 45)));
    REQUIRE(!contains(*compound.m_path, Point(15, 15)));
    REQUIRE(document.get(id).m_children ==
      std::vector{board.m_bottom, board.m_top});
    REQUIRE(document.get(id).m_header.m_name == "Intersection 1");
    REQUIRE(!document.get(id).m_header.m_has_custom_name);
    REQUIRE(!history.execute(
      registry.make("object.path.intersect", document, std::vector{id})));
    history.execute(std::make_unique<RenameCommand>(id, "Shape"));
    history.execute(
      registry.make("object.path.exclude", document, std::vector{id}));
    REQUIRE(document.get(id).m_header.m_name == "Shape");
  }

  TEST_CASE("boolean_pins") {
    auto document = Document();
    auto board = build_board(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(board.m_bottom).m_header;
      header.m_constraints.m_is_custom = true;
      header.m_constraints.m_left = true;
      header.m_constraints.m_width = true;
      transaction.replace_header(std::move(header));
      header = document.get(board.m_top).m_header;
      header.m_constraints.m_right = true;
      transaction.replace_header(std::move(header));
      transaction.commit();
    }
    auto registry = make_registry();
    auto history = History(document);
    history.execute(registry.make("object.path.add", document,
      std::vector{board.m_top, board.m_bottom}));
    auto& bottom = document.get(board.m_bottom).m_header.m_constraints;
    REQUIRE(bottom.m_is_custom);
    REQUIRE(!bottom.m_left);
    REQUIRE(!bottom.m_width);
    REQUIRE(!document.get(board.m_top).m_header.m_constraints.m_right);
  }

  TEST_CASE("nested") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = make_registry();
    auto history = History(document);
    auto inner = static_cast<const BooleanCommand*>(history.execute(
      registry.make("object.path.add", document,
        std::vector{board.m_bottom, board.m_top})))->get_compound();
    auto outer = static_cast<const BooleanCommand*>(history.execute(
      registry.make("object.path.subtract", document,
        std::vector{inner, board.m_loose})))->get_compound();
    REQUIRE(document.get(outer).m_parent == NodeId());
    REQUIRE(document.get(outer).m_children ==
      std::vector{inner, board.m_loose});
    require_near(get_world_transform(document, board.m_bottom),
      make_translation(110, 110));
    REQUIRE(contains(*get_compound(document, outer).m_path, Point(115, 115)));
  }

  TEST_CASE("applicability") {
    auto document = Document();
    auto board = build_board(document);
    auto registry = make_registry();
    for(auto name : {"object.path.add", "object.path.subtract",
        "object.path.intersect", "object.path.exclude",
        "object.path.convert", "object.path.outline_stroke"}) {
      REQUIRE(registry.contains(name));
    }
    REQUIRE(registry.get_names().size() == 6);
    REQUIRE(!registry.make(
      "object.path.add", document, std::vector{board.m_bottom}));
    REQUIRE(!registry.make("object.path.add", document,
      std::vector{board.m_bottom, board.m_text}));
    REQUIRE(!registry.make("object.path.add", document,
      std::vector{board.m_bottom, board.m_artboard}));
    REQUIRE(!registry.make(
      "object.path.add", document, std::vector<NodeId>()));
    REQUIRE(registry.make("object.path.exclude", document,
      std::vector{board.m_bottom, board.m_loose}));
  }
}

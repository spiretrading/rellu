#include <doctest/doctest.h>
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct StackScene {
    NodeId m_stack;
    NodeId m_background;
    NodeId m_a;
    NodeId m_b;
    NodeId m_c;
  };

  struct GridScene {
    NodeId m_grid;
    std::vector<NodeId> m_cells;
    std::vector<NodeId> m_rectangles;
    std::vector<NodeId> m_texts;
  };

  StackScene build_stack(Document& document, Orientation orientation,
      bool has_background = false) {
    auto scene = StackScene();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_stack = Stack(orientation);
    if(has_background) {
      group.m_stack->m_padding = Margins(10, 10, 10, 10);
      group.m_stack->m_has_background = true;
    }
    scene.m_stack = transaction.append(
      NodeId(), make_header("Stack", make_translation(100, 100)), group);
    auto is_vertical = orientation == Orientation::VERTICAL;
    if(has_background) {
      scene.m_background = transaction.append(scene.m_stack,
        make_header("Background", make_translation(-10, -10)),
        make_rectangle_shape(120, 130));
    }
    auto place = [&] (std::string name, double offset, double extent) {
      auto transform = is_vertical ? make_translation(0, offset) :
        make_translation(offset, 0);
      auto shape = is_vertical ? make_rectangle_shape(100, extent) :
        make_rectangle_shape(extent, 10);
      return transaction.append(
        scene.m_stack, make_header(std::move(name), transform), shape);
    };
    scene.m_a = place("A", 90, 20);
    scene.m_b = place("B", 50, 30);
    scene.m_c = place("C", 0, 40);
    auto payload = *document.get(scene.m_stack).get_group();
    payload.m_stack->m_spacing.emplace(scene.m_c, 10);
    payload.m_stack->m_spacing.emplace(scene.m_b, 10);
    transaction.replace_payload(scene.m_stack, payload);
    transaction.commit();
    document.clear_history();
    return scene;
  }

  Text make_cell_text(const std::u16string& content) {
    auto text = make_text(content);
    text.m_frame = AreaFrame(Size(40, 20));
    return text;
  }

  GridScene build_grid(Document& document) {
    auto scene = GridScene();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_repeat_grid =
      RepeatGrid(Size(40, 40), 20, 10, 1, 3, Size(40, 140));
    scene.m_grid = transaction.append(
      NodeId(), make_header("Grid", make_translation(100, 100)), group);
    auto contents = std::vector<std::u16string>({u"One", u"Two", u"Three"});
    for(auto i = 0; i != 3; ++i) {
      auto cell = transaction.append(scene.m_grid,
        make_header("", make_translation(0, 50 * i)), Group());
      scene.m_cells.push_back(cell);
      scene.m_rectangles.push_back(transaction.append(
        cell, make_header("Rectangle"), make_rectangle_shape(40, 20)));
      scene.m_texts.push_back(transaction.append(cell,
        make_header("Text", make_translation(0, 20)),
        make_cell_text(contents[i])));
    }
    transaction.commit();
    document.clear_history();
    return scene;
  }

  Rect get_frame(const Document& document, const NodeId& id) {
    return *get_bounds(document, id, get_transform(document, id));
  }

  void require_frame(
      const Document& document, const NodeId& id, const Rect& expected) {
    auto frame = get_frame(document, id);
    REQUIRE(frame.m_x == doctest::Approx(expected.m_x));
    REQUIRE(frame.m_y == doctest::Approx(expected.m_y));
    REQUIRE(frame.m_width == doctest::Approx(expected.m_width));
    REQUIRE(frame.m_height == doctest::Approx(expected.m_height));
  }

  std::u16string get_content(const Document& document, const NodeId& id) {
    return *document.get(id).get_text()->m_content;
  }
}

TEST_SUITE("Layouts") {
  TEST_CASE("cell_counts") {
    REQUIRE(count_repeat_cells(171, 40, 0) == 5);
    REQUIRE(count_repeat_cells(28, 27, 0) == 2);
    REQUIRE(count_repeat_cells(242, 7, 18) == 10);
    REQUIRE(count_repeat_cells(80, 40, 0) == 2);
    REQUIRE(count_repeat_cells(10, 0, 0) == 1);
    REQUIRE(get_repeat_extent(3, 40, 10) == 140);
  }

  TEST_CASE("stack_items") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL, true);
    REQUIRE(get_stack_items(document, scene.m_stack) ==
      std::vector{scene.m_c, scene.m_b, scene.m_a});
    auto& stack = *document.get(scene.m_stack).get_group()->m_stack;
    REQUIRE(get_stack_margin(document, stack, scene.m_c) == 10);
    REQUIRE(get_stack_margin(document, stack, scene.m_a) == 0);
  }

  TEST_CASE("stack_resize") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto history = History(document);
    history.execute(
      make_bounds_command(document, scene.m_b, Rect(0, 0, 100, 60)));
    require_frame(document, scene.m_c, Rect(0, 0, 100, 40));
    require_frame(document, scene.m_b, Rect(0, 50, 100, 60));
    require_frame(document, scene.m_a, Rect(0, 120, 100, 20));
    REQUIRE(history.get_undo_count() == 1);
    history.undo();
    require_frame(document, scene.m_a, Rect(0, 90, 100, 20));
    history.execute(
      make_bounds_command(document, scene.m_c, Rect(0, 0, 100, 5)));
    require_frame(document, scene.m_b, Rect(0, 15, 100, 30));
    require_frame(document, scene.m_a, Rect(0, 55, 100, 20));
  }

  TEST_CASE("stack_removal") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto history = History(document);
    history.execute(
      std::make_unique<DeleteCommand>(std::vector{scene.m_b}));
    require_frame(document, scene.m_a, Rect(0, 50, 100, 20));
    history.undo();
    history.execute(
      std::make_unique<DeleteCommand>(std::vector{scene.m_c}));
    require_frame(document, scene.m_b, Rect(0, 0, 100, 30));
    require_frame(document, scene.m_a, Rect(0, 40, 100, 20));
    history.execute(
      std::make_unique<DeleteCommand>(std::vector{scene.m_a}));
    auto& stack = *document.get(scene.m_stack).get_group()->m_stack;
    REQUIRE(stack.m_spacing.empty());
    require_frame(document, scene.m_b, Rect(0, 0, 100, 30));
  }

  TEST_CASE("stack_insertion") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto history = History(document);
    auto command = history.execute(std::make_unique<InsertNodeCommand>(
      "Rectangle", scene.m_stack, make_header("D", make_translation(5, 300)),
      make_rectangle_shape(100, 15)));
    auto d = static_cast<const InsertNodeCommand&>(*command).get_node();
    require_frame(document, d, Rect(5, 0, 100, 15));
    require_frame(document, scene.m_c, Rect(0, 25, 100, 40));
    require_frame(document, scene.m_b, Rect(0, 75, 100, 30));
    require_frame(document, scene.m_a, Rect(0, 115, 100, 20));
    auto& stack = *document.get(scene.m_stack).get_group()->m_stack;
    REQUIRE(stack.m_spacing.at(d) == 10);
    REQUIRE(!stack.m_spacing.contains(scene.m_a));
  }

  TEST_CASE("stack_hidden_item") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto history = History(document);
    history.execute(
      std::make_unique<SetVisibilityCommand>(std::vector{scene.m_b}, false));
    require_frame(document, scene.m_a, Rect(0, 50, 100, 20));
    require_frame(document, scene.m_b, Rect(0, 50, 100, 30));
  }

  TEST_CASE("stack_reorder") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto history = History(document);
    history.execute(std::make_unique<TranslateCommand>(
      std::vector{scene.m_c}, Point(0, 70)));
    REQUIRE(document.get(scene.m_stack).m_children ==
      std::vector{scene.m_a, scene.m_c, scene.m_b});
    require_frame(document, scene.m_b, Rect(0, 0, 100, 30));
    require_frame(document, scene.m_c, Rect(0, 40, 100, 40));
    require_frame(document, scene.m_a, Rect(0, 90, 100, 20));
    history.undo();
    REQUIRE(document.get(scene.m_stack).m_children ==
      std::vector{scene.m_a, scene.m_b, scene.m_c});
    require_frame(document, scene.m_c, Rect(0, 0, 100, 40));
    history.execute(std::make_unique<TranslateCommand>(
      std::vector{scene.m_b}, Point(25, 3)));
    require_frame(document, scene.m_b, Rect(25, 50, 100, 30));
  }

  TEST_CASE("stack_snap") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto history = History(document);
    REQUIRE(!history.execute(std::make_unique<TranslateCommand>(
      std::vector{scene.m_b}, Point(0, 4))));
    require_frame(document, scene.m_b, Rect(0, 50, 100, 30));
    REQUIRE(history.get_undo_count() == 0);
    REQUIRE(!history.can_undo());
  }

  TEST_CASE("horizontal_stack") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::HORIZONTAL);
    auto history = History(document);
    history.execute(
      make_bounds_command(document, scene.m_b, Rect(0, 0, 50, 10)));
    require_frame(document, scene.m_a, Rect(110, 0, 20, 10));
    require_frame(document, scene.m_c, Rect(0, 0, 40, 10));
  }

  TEST_CASE("held_layout") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto history = History(document);
    history.hold_layout(std::vector{scene.m_c});
    history.merge(std::make_unique<TranslateCommand>(
      std::vector{scene.m_c}, Point(0, 35)));
    history.merge(std::make_unique<TranslateCommand>(
      std::vector{scene.m_c}, Point(0, 35)));
    require_frame(document, scene.m_c, Rect(0, 70, 100, 40));
    require_frame(document, scene.m_b, Rect(0, 0, 100, 30));
    require_frame(document, scene.m_a, Rect(0, 90, 100, 20));
    history.release_layout();
    history.seal();
    require_frame(document, scene.m_c, Rect(0, 40, 100, 40));
    REQUIRE(history.get_undo_count() == 1);
    history.undo();
    require_frame(document, scene.m_c, Rect(0, 0, 100, 40));
    require_frame(document, scene.m_b, Rect(0, 50, 100, 30));
    history.hold_layout(std::vector{scene.m_b});
    history.merge(std::make_unique<TranslateCommand>(
      std::vector{scene.m_b}, Point(0, 5)));
    history.release_layout();
    require_frame(document, scene.m_b, Rect(0, 50, 100, 30));
    REQUIRE(history.get_undo_count() == 0);
  }

  TEST_CASE("padding") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_padding = Padding(Margins(10, 40, 30, 20), true, true);
    auto box = transaction.append(NodeId(), make_header("Box"), group);
    auto background = transaction.append(box,
      make_header("Background", make_translation(20, 40)),
      make_rectangle_shape(160, 100));
    auto content = transaction.append(box,
      make_header("Content", make_translation(40, 50)),
      make_rectangle_shape(100, 60));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, content, Rect(0, 0, 50, 20)));
    require_frame(document, background, Rect(20, 40, 110, 60));
    history.execute(std::make_unique<TranslateCommand>(
      std::vector{content}, Point(100, 100)));
    require_frame(document, background, Rect(120, 140, 110, 60));
    history.execute(
      make_bounds_command(document, background, Rect(0, 0, 400, 400)));
    require_frame(document, background, Rect(120, 140, 110, 60));
    REQUIRE(history.get_undo_count() == 2);
  }

  TEST_CASE("stack_background") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL, true);
    auto history = History(document);
    history.execute(
      make_bounds_command(document, scene.m_b, Rect(0, 0, 100, 60)));
    require_frame(document, scene.m_background, Rect(-10, -10, 120, 160));
    history.execute(
      std::make_unique<DeleteCommand>(std::vector{scene.m_c}));
    require_frame(document, scene.m_b, Rect(0, 0, 100, 60));
    require_frame(document, scene.m_background, Rect(-10, -10, 120, 110));
  }

  TEST_CASE("nested_stacks") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto transaction = Transaction(document);
    transaction.remove(scene.m_b);
    auto group = Group();
    group.m_stack = Stack(Orientation::HORIZONTAL);
    auto row = transaction.insert(scene.m_stack, 1,
      make_header("Row", make_translation(0, 50)), group);
    auto last = transaction.append(row,
      make_header("Last", make_translation(30, 0)),
      make_rectangle_shape(20, 30));
    auto first = transaction.append(row, make_header("First"),
      make_rectangle_shape(20, 30));
    auto inner = *document.get(row).get_group();
    inner.m_stack->m_spacing.emplace(first, 10);
    transaction.replace_payload(row, inner);
    auto outer = *document.get(scene.m_stack).get_group();
    outer.m_stack->m_spacing.emplace(row, 10);
    transaction.replace_payload(scene.m_stack, outer);
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(
      make_bounds_command(document, first, Rect(0, 0, 40, 50)));
    require_frame(document, last, Rect(50, 0, 20, 30));
    require_frame(document, row, Rect(0, 50, 70, 50));
    require_frame(document, scene.m_a, Rect(0, 110, 100, 20));
    REQUIRE(history.get_undo_count() == 1);
  }

  TEST_CASE("cell_propagation") {
    auto document = Document();
    auto scene = build_grid(document);
    auto history = History(document);
    history.execute(make_bounds_command(
      document, scene.m_rectangles[1], Rect(0, 0, 60, 20)));
    for(auto& rectangle : scene.m_rectangles) {
      require_frame(document, rectangle, Rect(0, 0, 60, 20));
    }
    auto& grid = *document.get(scene.m_grid).get_group()->m_repeat_grid;
    REQUIRE(grid.m_cell_size == Size(60, 40));
    REQUIRE(grid.m_size == Size(60, 140));
    REQUIRE(get_transform(document, scene.m_cells[2]) ==
      make_translation(0, 100));
    REQUIRE(history.get_undo_count() == 1);
    history.undo();
    require_frame(document, scene.m_rectangles[0], Rect(0, 0, 40, 20));
    history.execute(std::make_unique<SetFillCommand>(
      std::vector{scene.m_rectangles[0]}, SolidFill(make_color(0, 0, 255))));
    REQUIRE(document.get(scene.m_rectangles[2]).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 0, 255))));
  }

  TEST_CASE("cell_overrides") {
    auto document = Document();
    auto scene = build_grid(document);
    auto history = History(document);
    auto text = *document.get(scene.m_texts[2]).get_text();
    text.m_content = Shared(std::u16string(u"Changed"));
    text.m_runs.front().m_length = 7;
    history.execute(std::make_unique<EditTextCommand>(scene.m_texts[2], text));
    REQUIRE(get_content(document, scene.m_texts[0]) == u"One");
    REQUIRE(get_content(document, scene.m_texts[1]) == u"Two");
    REQUIRE(get_content(document, scene.m_texts[2]) == u"Changed");
    auto edit = TextStyleEdit();
    edit.m_size = 20;
    history.execute(std::make_unique<SetTextStyleCommand>(
      std::vector{scene.m_texts[0]}, edit));
    for(auto i = std::size_t(0); i != scene.m_texts.size(); ++i) {
      auto& runs = document.get(scene.m_texts[i]).get_text()->m_runs;
      REQUIRE(runs.size() == 1);
      REQUIRE(runs.front().m_style.m_font.m_size == 20);
      REQUIRE(runs.front().m_length ==
        get_content(document, scene.m_texts[i]).size());
    }
    REQUIRE(get_content(document, scene.m_texts[2]) == u"Changed");
    auto image = ImageFill();
    image.m_image = generate_node_id();
    history.execute(std::make_unique<SetFillCommand>(
      std::vector{scene.m_rectangles[1]}, image));
    REQUIRE(std::holds_alternative<ImageFill>(
      document.get(scene.m_rectangles[1]).get_shape()->m_style.m_fill));
    REQUIRE(std::holds_alternative<SolidFill>(
      document.get(scene.m_rectangles[0]).get_shape()->m_style.m_fill));
  }

  TEST_CASE("cell_structure") {
    auto document = Document();
    auto scene = build_grid(document);
    auto history = History(document);
    auto command = history.execute(std::make_unique<InsertNodeCommand>(
      "Ellipse", scene.m_cells[0], make_header("Dot", make_translation(5, 5)),
      make_rectangle_shape(4, 4)));
    auto dot = static_cast<const InsertNodeCommand&>(*command).get_node();
    for(auto& cell : scene.m_cells) {
      auto& children = document.get(cell).m_children;
      REQUIRE(children.size() == 3);
      REQUIRE(document.get(children.back()).m_header.m_name == "Dot");
      REQUIRE(get_frame(document, children.back()) == Rect(5, 5, 4, 4));
    }
    auto copy = document.get(scene.m_cells[2]).m_children.back();
    REQUIRE(copy != dot);
    history.execute(std::make_unique<DeleteCommand>(std::vector{copy}));
    for(auto& cell : scene.m_cells) {
      REQUIRE(document.get(cell).m_children.size() == 2);
    }
    history.undo();
    history.undo();
    REQUIRE(document.get(scene.m_cells[1]).m_children.size() == 2);
  }

  TEST_CASE("unedited_layouts") {
    auto document = Document();
    auto scene = build_stack(document, Orientation::VERTICAL);
    auto transaction = Transaction(document);
    set_transform(document, transaction, scene.m_a, make_translation(0, 93));
    transaction.commit();
    document.clear_history();
    auto history = History(document);
    history.execute(std::make_unique<SetFillCommand>(
      std::vector{scene.m_a}, SolidFill(make_color(0, 255, 0))));
    require_frame(document, scene.m_a, Rect(0, 93, 100, 20));
    REQUIRE(!refresh_layouts(document, ChangeSet(), LayoutOptions()));
    history.execute(
      make_bounds_command(document, scene.m_c, Rect(0, 0, 100, 45)));
    require_frame(document, scene.m_b, Rect(0, 55, 100, 30));
    require_frame(document, scene.m_a, Rect(0, 98, 100, 20));
  }
}

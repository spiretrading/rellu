#include <numbers>
#include <string>
#include <string_view>
#include <tuple>
#include <doctest/doctest.h>
#include "Rellu/Commands/AlignCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Layout {
    NodeId m_artboard;
    NodeId m_first;
    NodeId m_second;
    NodeId m_third;
    NodeId m_group;
    NodeId m_inner;
    NodeId m_outer;
    NodeId m_rotated;
    NodeId m_loose;
    NodeId m_other_loose;
    NodeId m_other_artboard;
  };

  NodeId add_rectangle(Transaction& transaction, const NodeId& parent,
      const Rect& rect) {
    return transaction.append(parent, make_header("Rectangle",
      make_translation(rect.m_x, rect.m_y)),
      make_rectangle_shape(rect.m_width, rect.m_height));
  }

  NodeId add_artboard(Transaction& transaction, const Rect& frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    return transaction.append(NodeId(), make_header("Artboard"), artboard);
  }

  Layout build_layout(Document& document) {
    auto layout = Layout();
    auto transaction = Transaction(document);
    layout.m_artboard = add_artboard(transaction, Rect(0, 0, 400, 300));
    layout.m_first =
      add_rectangle(transaction, layout.m_artboard, Rect(10, 10, 40, 20));
    layout.m_second =
      add_rectangle(transaction, layout.m_artboard, Rect(100, 50, 60, 30));
    layout.m_third =
      add_rectangle(transaction, layout.m_artboard, Rect(200, 120, 20, 40));
    layout.m_group = transaction.append(
      layout.m_artboard, make_header("Group"), Group());
    layout.m_inner =
      add_rectangle(transaction, layout.m_group, Rect(300, 200, 20, 20));
    layout.m_outer =
      add_rectangle(transaction, layout.m_group, Rect(340, 230, 30, 10));
    layout.m_rotated = transaction.append(layout.m_artboard,
      make_header("Rotated", make_translation(250, 50) *
        make_rotation(std::numbers::pi / 4)), make_rectangle_shape(40, 20));
    layout.m_loose =
      add_rectangle(transaction, NodeId(), Rect(-100, -100, 10, 10));
    layout.m_other_loose =
      add_rectangle(transaction, NodeId(), Rect(-50, -80, 20, 10));
    layout.m_other_artboard =
      add_artboard(transaction, Rect(500, 40, 200, 100));
    transaction.commit();
    document.clear_history();
    return layout;
  }

  CommandRegistry make_registry() {
    auto registry = CommandRegistry();
    add_align_commands(registry);
    return registry;
  }

  Rect get_bounds(const Document& document, const NodeId& id) {
    return *get_world_bounds(document, id);
  }

  void run(History& history, const CommandRegistry& registry,
      std::string_view name, const std::vector<NodeId>& targets) {
    auto command = registry.make(name, history.get_document(), targets);
    REQUIRE(command);
    require_undoable(history, std::move(command));
  }
}

TEST_SUITE("AlignCommands") {
  TEST_CASE("names") {
    auto registry = make_registry();
    REQUIRE(registry.get_names() == std::vector<std::string>({
      "object.align.bottom", "object.align.center", "object.align.left",
      "object.align.middle", "object.align.right", "object.align.top",
      "object.distribute.horizontally", "object.distribute.vertically"}));
  }

  TEST_CASE("align_horizontally") {
    auto registry = make_registry();
    for(auto [name, first, second, third] : {
        std::tuple("object.align.left", 10.0, 10.0, 10.0),
        std::tuple("object.align.center", 95.0, 85.0, 105.0),
        std::tuple("object.align.right", 180.0, 160.0, 200.0)}) {
      auto document = Document();
      auto layout = build_layout(document);
      auto history = History(document);
      run(history, registry, name,
        std::vector{layout.m_first, layout.m_second, layout.m_third});
      REQUIRE(history.get_undo_label() == "Align");
      REQUIRE(get_bounds(document, layout.m_first) == Rect(first, 10, 40, 20));
      REQUIRE(
        get_bounds(document, layout.m_second) == Rect(second, 50, 60, 30));
      REQUIRE(
        get_bounds(document, layout.m_third) == Rect(third, 120, 20, 40));
    }
  }

  TEST_CASE("align_vertically") {
    auto registry = make_registry();
    for(auto [name, first, second, third] : {
        std::tuple("object.align.top", 10.0, 10.0, 10.0),
        std::tuple("object.align.middle", 75.0, 70.0, 65.0),
        std::tuple("object.align.bottom", 140.0, 130.0, 120.0)}) {
      auto document = Document();
      auto layout = build_layout(document);
      auto history = History(document);
      run(history, registry, name,
        std::vector{layout.m_first, layout.m_second, layout.m_third});
      REQUIRE(get_bounds(document, layout.m_first) == Rect(10, first, 40, 20));
      REQUIRE(
        get_bounds(document, layout.m_second) == Rect(100, second, 60, 30));
      REQUIRE(
        get_bounds(document, layout.m_third) == Rect(200, third, 20, 40));
    }
  }

  TEST_CASE("single_object") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto history = History(document);
    auto targets = std::vector{layout.m_first};
    run(history, registry, "object.align.right", targets);
    REQUIRE(get_bounds(document, layout.m_first) == Rect(360, 10, 40, 20));
    run(history, registry, "object.align.center", targets);
    REQUIRE(get_bounds(document, layout.m_first) == Rect(180, 10, 40, 20));
    run(history, registry, "object.align.middle", targets);
    REQUIRE(get_bounds(document, layout.m_first) == Rect(180, 140, 40, 20));
    run(history, registry, "object.align.bottom", targets);
    REQUIRE(get_bounds(document, layout.m_first) == Rect(180, 280, 40, 20));
  }

  TEST_CASE("inside_group") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto history = History(document);
    for(auto name : registry.get_names()) {
      REQUIRE(!registry.make(name, document, std::vector{layout.m_inner}));
    }
    run(history, registry, "object.align.right",
      std::vector{layout.m_inner, layout.m_outer});
    REQUIRE(get_bounds(document, layout.m_inner) == Rect(350, 200, 20, 20));
    REQUIRE(get_bounds(document, layout.m_outer) == Rect(340, 230, 30, 10));
  }

  TEST_CASE("rotated") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto history = History(document);
    auto rotated = get_bounds(document, layout.m_rotated);
    run(history, registry, "object.align.left",
      std::vector{layout.m_first, layout.m_rotated});
    auto aligned = get_bounds(document, layout.m_rotated);
    REQUIRE(aligned.m_x == doctest::Approx(10));
    REQUIRE(aligned.m_y == doctest::Approx(rotated.m_y));
    REQUIRE(aligned.m_width == doctest::Approx(rotated.m_width));
    REQUIRE(get_bounds(document, layout.m_first) == Rect(10, 10, 40, 20));
    auto transform = document.get(layout.m_rotated).m_header.m_transform;
    REQUIRE(transform.m_a == doctest::Approx(std::numbers::sqrt2 / 2));
    REQUIRE(transform.m_b == doctest::Approx(std::numbers::sqrt2 / 2));
  }

  TEST_CASE("pasteboard") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto history = History(document);
    REQUIRE(!registry.make(
      "object.align.left", document, std::vector{layout.m_loose}));
    run(history, registry, "object.align.left",
      std::vector{layout.m_loose, layout.m_other_loose});
    REQUIRE(
      get_bounds(document, layout.m_other_loose) == Rect(-100, -80, 20, 10));
  }

  TEST_CASE("artboards") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto history = History(document);
    REQUIRE(!registry.make(
      "object.align.top", document, std::vector{layout.m_artboard}));
    run(history, registry, "object.align.bottom",
      std::vector{layout.m_artboard, layout.m_other_artboard});
    REQUIRE(document.get(layout.m_other_artboard).get_artboard()->m_frame ==
      Rect(500, 200, 200, 100));
    REQUIRE(document.get(layout.m_artboard).get_artboard()->m_frame ==
      Rect(0, 0, 400, 300));
  }

  TEST_CASE("unalignable_targets") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto transaction = Transaction(document);
    auto header = document.get(layout.m_second).m_header;
    header.m_is_locked = true;
    transaction.replace_header(header);
    auto root = transaction.insert_root(make_header("Root"), Group());
    transaction.commit();
    REQUIRE(get_alignable(document,
      std::vector{layout.m_first, layout.m_second, layout.m_group,
        layout.m_inner, root}) ==
      std::vector{layout.m_first, layout.m_group});
    REQUIRE(!registry.make(
      "object.align.left", document, std::vector{layout.m_second}));
    REQUIRE(!registry.make("object.align.left", document, std::vector{root}));
    auto history = History(document);
    run(history, registry, "object.align.left",
      std::vector{layout.m_third, layout.m_second});
    REQUIRE(get_bounds(document, layout.m_third) == Rect(0, 120, 20, 40));
    REQUIRE(get_bounds(document, layout.m_second) == Rect(100, 50, 60, 30));
  }

  TEST_CASE("distribute_horizontally") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto history = History(document);
    run(history, registry, "object.distribute.horizontally",
      std::vector{layout.m_third, layout.m_first, layout.m_second});
    REQUIRE(history.get_undo_label() == "Distribute");
    REQUIRE(get_bounds(document, layout.m_first) == Rect(10, 10, 40, 20));
    REQUIRE(get_bounds(document, layout.m_second) == Rect(95, 50, 60, 30));
    REQUIRE(get_bounds(document, layout.m_third) == Rect(200, 120, 20, 40));
    REQUIRE(!history.execute(registry.make("object.distribute.horizontally",
      document, std::vector{layout.m_first, layout.m_second, layout.m_third})));
  }

  TEST_CASE("distribute_vertically") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    auto history = History(document);
    run(history, registry, "object.distribute.vertically",
      std::vector{layout.m_first, layout.m_second, layout.m_third});
    REQUIRE(get_bounds(document, layout.m_first) == Rect(10, 10, 40, 20));
    REQUIRE(get_bounds(document, layout.m_second) == Rect(100, 60, 60, 30));
    REQUIRE(get_bounds(document, layout.m_third) == Rect(200, 120, 20, 40));
  }

  TEST_CASE("distribute_count") {
    auto registry = make_registry();
    auto document = Document();
    auto layout = build_layout(document);
    REQUIRE(!registry.make("object.distribute.horizontally", document,
      std::vector{layout.m_first, layout.m_second}));
    REQUIRE(registry.make("object.distribute.vertically", document,
      std::vector{layout.m_first, layout.m_first, layout.m_second,
        layout.m_inner, layout.m_group}));
  }
}

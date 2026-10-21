#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Tools/ComponentDrops.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  constexpr auto DROP_FILL = make_color(38, 128, 235, 0.3);

  NodeId build_artboard(Document& document) {
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 600, 400);
    auto id = transaction.append(NodeId(), make_header("Board"), artboard);
    auto card = transaction.append(
      id, make_header("Card", make_translation(50, 50)), Group());
    transaction.append(
      card, make_header("Face"), make_rectangle_shape(40, 40));
    auto badge = transaction.append(
      id, make_header("Badge", make_translation(200, 50)), Group());
    transaction.append(
      badge, make_header("Dot"), make_rectangle_shape(30, 20));
    transaction.append(id, make_header("Shape", make_translation(50, 250)),
      make_rectangle_shape(60, 60));
    transaction.commit();
    document.clear_history();
    return id;
  }

  struct Setup {
    Document m_document;
    NodeId m_artboard;
    Editor m_editor;
    ToolManager m_tools;
    ComponentId m_card;
    ComponentId m_badge;
    NodeId m_instance;

    Setup()
        : m_artboard(build_artboard(m_document)),
          m_editor(m_document),
          m_tools(m_editor) {
      m_tools.set_view(Matrix(), 1, Size(1000, 800));
      auto& children = m_document.get(m_artboard).m_children;
      auto card = children[0];
      auto badge = children[1];
      m_card = make(card);
      m_badge = make(badge);
      auto inserted = m_editor.execute(std::make_unique<InsertInstanceCommand>(
        m_card, m_artboard, make_translation(300, 200)));
      m_instance =
        static_cast<const InsertInstanceCommand&>(*inserted).get_instance();
    }
    ComponentId make(const NodeId& group) {
      auto made = m_editor.execute(
        std::make_unique<MakeComponentCommand>(std::vector{group}));
      return static_cast<const MakeComponentCommand&>(*made).get_component();
    }

    std::string drop(const ComponentId& component, Point position,
        Qt::KeyboardModifiers modifiers = {}) {
      auto command = make_component_drop(
        m_tools.get_context(), component, position, modifiers);
      REQUIRE(command);
      auto label = command->get_label();
      m_editor.execute(std::move(command));
      return label;
    }

    const NodeId& get_placed() const {
      return m_editor.get_selection().get_primary();
    }
  };

  std::size_t count_drop_fills(const CanvasOverlay& overlay) {
    return static_cast<std::size_t>(std::count_if(overlay.m_shapes.begin(),
      overlay.m_shapes.end(), [] (const auto& shape) {
        return shape.m_fill == DROP_FILL;
      }));
  }
}

TEST_SUITE("ComponentDrops") {
  TEST_CASE("swap_target") {
    auto setup = Setup();
    auto& context = setup.m_tools.get_context();
    REQUIRE(find_swap_target(context, setup.m_badge, Point(420, 320)) ==
      setup.m_instance);
    REQUIRE(find_swap_target(context, setup.m_card, Point(420, 320)).is_nil());
    REQUIRE(
      find_swap_target(context, setup.m_badge, Point(180, 380)).is_nil());
    REQUIRE(
      find_swap_target(context, setup.m_badge, Point(650, 450)).is_nil());
  }

  TEST_CASE("drop_fill") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    REQUIRE(count_drop_fills(tools.draw()) == 0);
    tools.set_drop_target(setup.m_instance);
    REQUIRE(tools.get_drop_target() == setup.m_instance);
    auto overlay = tools.draw();
    REQUIRE(count_drop_fills(overlay) == 1);
    auto fill = std::find_if(overlay.m_shapes.begin(), overlay.m_shapes.end(),
      [] (const auto& shape) {
        return shape.m_fill == DROP_FILL;
      });
    REQUIRE(fill->m_center == Point(420, 320));
    REQUIRE(fill->m_size == Size(40, 40));
    REQUIRE(fill->m_angle == 0);
    tools.set_drop_target(NodeId());
    REQUIRE(tools.get_drop_target().is_nil());
    REQUIRE(count_drop_fills(tools.draw()) == 0);
  }

  TEST_CASE("labels") {
    auto setup = Setup();
    auto& editor = setup.m_editor;
    auto& document = setup.m_document;
    REQUIRE(setup.drop(setup.m_badge, Point(420, 320)) == "Replace Component");
    REQUIRE(!document.find(setup.m_instance));
    REQUIRE(get_instance_role(document.get(setup.get_placed()))->m_component ==
      setup.m_badge);
    REQUIRE(editor.undo());
    REQUIRE(setup.drop(setup.m_badge, Point(420, 320), Qt::AltModifier) ==
      "Place Graphic");
    REQUIRE(!document.find(setup.m_instance));
    REQUIRE(editor.get_history().get_undo_label() == "Place Graphic");
    REQUIRE(editor.undo());
    REQUIRE(document.find(setup.m_instance));
    auto name = make_instance_name(document, setup.m_card);
    REQUIRE(setup.drop(setup.m_card, Point(420, 320)) == "Place Component");
    REQUIRE(document.find(setup.m_instance));
    REQUIRE(document.get(setup.get_placed()).m_header.m_name == name);
    REQUIRE(editor.undo());
    REQUIRE(setup.drop(setup.m_card, Point(420, 320), Qt::AltModifier) ==
      "Place Graphic");
    REQUIRE(document.get(setup.get_placed()).m_header.m_name == name);
    REQUIRE(setup.drop(setup.m_badge, Point(180, 380)) == "Place Component");
    REQUIRE(setup.drop(setup.m_badge, Point(180, 380), Qt::AltModifier) ==
      "Place Graphic");
    REQUIRE(setup.drop(setup.m_badge, Point(800, 600)) == "Place Component");
  }

  TEST_CASE("placement") {
    auto setup = Setup();
    auto& document = setup.m_document;
    setup.drop(setup.m_card, Point(523.4, 211.6), Qt::AltModifier);
    auto placed = setup.get_placed();
    REQUIRE(document.get(placed).m_parent == setup.m_artboard);
    REQUIRE(*get_world_bounds(document, placed) == Rect(523, 212, 40, 40));
    setup.drop(setup.m_badge, Point(250.2, 400.7));
    placed = setup.get_placed();
    REQUIRE(document.get(placed).m_parent == setup.m_artboard);
    REQUIRE(*get_world_bounds(document, placed) == Rect(250, 401, 30, 20));
    setup.drop(setup.m_card, Point(800, 600), Qt::AltModifier);
    placed = setup.get_placed();
    REQUIRE(document.get(placed).m_parent.is_nil());
    REQUIRE(*get_world_bounds(document, placed) == Rect(800, 600, 40, 40));
  }
}

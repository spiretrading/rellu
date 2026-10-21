#include <algorithm>
#include <doctest/doctest.h>
#include "Rellu/Tools/ArtboardGuides.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void drag(ToolManager& tools, Point from, Point to,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton, modifiers));
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(interpolate(from, to, i / 4.0), Qt::NoButton,
        Qt::LeftButton, modifiers));
    }
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  NodeId build_artboard(Document& document) {
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 100, 400, 300);
    auto id = transaction.append(NodeId(), make_header("Board"), artboard);
    transaction.append(id, make_header("Rectangle", make_translation(50, 60)),
      make_rectangle_shape(40, 40));
    transaction.commit();
    document.clear_history();
    return id;
  }

  struct Setup {
    Document m_document;
    NodeId m_artboard;
    Editor m_editor;
    ToolManager m_tools;

    Setup()
        : m_artboard(build_artboard(m_document)),
          m_editor(m_document),
          m_tools(m_editor) {
      m_tools.set_view(Matrix(), 1, Size(1000, 800));
    }

    const std::vector<Guide>& get_guides() const {
      return m_document.get(m_artboard).get_artboard()->m_guides;
    }

    std::string get_label() {
      return m_editor.get_history().get_undo_label();
    }
  };

  std::size_t count_guides(const CanvasOverlay& overlay) {
    return static_cast<std::size_t>(std::count_if(overlay.m_shapes.begin(),
      overlay.m_shapes.end(), [] (const auto& shape) {
        return shape.m_stroke == GUIDE_COLOR || shape.m_fill == GUIDE_COLOR;
      }));
  }
}

TEST_SUITE("ArtboardGuides") {
  TEST_CASE("create") {
    auto setup = Setup();
    drag(setup.m_tools, Point(99, 300), Point(230.4, 300));
    REQUIRE(setup.get_guides().size() == 1);
    auto& guide = setup.get_guides().front();
    REQUIRE(guide.m_orientation == Orientation::VERTICAL);
    REQUIRE(guide.m_position == 130);
    REQUIRE(!guide.m_id.empty());
    REQUIRE(setup.get_label() == "Add Guide");
    REQUIRE(setup.m_editor.get_history().is_dirty());
    drag(setup.m_tools, Point(300, 99), Point(300, 180));
    REQUIRE(setup.get_guides().size() == 2);
    REQUIRE(setup.get_guides().front().m_orientation ==
      Orientation::HORIZONTAL);
    REQUIRE(setup.get_guides().front().m_position == 80);
    auto count = setup.m_editor.get_history().get_undo_count();
    drag(setup.m_tools, Point(99, 300), Point(60, 300));
    REQUIRE(setup.get_guides().size() == 2);
    REQUIRE(setup.m_editor.get_history().get_undo_count() == count);
  }

  TEST_CASE("creation_band") {
    auto setup = Setup();
    for(auto x : {100.0, 101.0, 88.5}) {
      drag(setup.m_tools, Point(x, 300), Point(230, 300));
      REQUIRE(setup.get_guides().empty());
      setup.m_editor.get_selection().clear();
    }
    drag(setup.m_tools, Point(89, 300), Point(230, 300));
    REQUIRE(setup.get_guides().size() == 1);
    drag(setup.m_tools, Point(300, 99.5), Point(300, 180));
    REQUIRE(setup.get_guides().size() == 2);
    drag(setup.m_tools, Point(300, 100), Point(300, 250));
    REQUIRE(setup.get_guides().size() == 2);
  }

  TEST_CASE("title_band") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    tools.set_view(make_scale(0.88, 0.88), 0.88, Size(1000, 800));
    auto above = [] (double pixels) {
      return 88 - pixels / 1.5;
    };
    for(auto pixels : {4.0, 10.0, 14.0}) {
      drag(tools, Point(300, above(pixels)), Point(300, 200));
      REQUIRE(setup.get_guides().size() == 1);
      REQUIRE(setup.m_editor.undo());
    }
    drag(tools, Point(300, above(18)), Point(300, 200));
    REQUIRE(setup.get_guides().empty());
    auto& titles = tools.get_context().get_titles();
    REQUIRE(titles.get_titles().size() == 1);
    auto x = titles.get_titles().front().m_bounds.m_x + 5;
    for(auto pixels : {4.0, 6.0}) {
      drag(tools, Point(x, above(pixels)), Point(x, 200));
      REQUIRE(setup.get_guides().size() == 1);
      REQUIRE(setup.m_editor.undo());
    }
    REQUIRE(titles.find(Point(x, above(18))) == setup.m_artboard);
    drag(tools, Point(x, above(8)), Point(x + 44, above(8)));
    REQUIRE(setup.get_guides().empty());
    REQUIRE(setup.m_document.get(setup.m_artboard).get_artboard()->m_frame ==
      Rect(150, 100, 400, 300));
  }

  TEST_CASE("move") {
    auto setup = Setup();
    drag(setup.m_tools, Point(99, 300), Point(230, 300));
    auto id = setup.get_guides().front().m_id;
    drag(setup.m_tools, Point(231, 300), Point(300.2, 300));
    REQUIRE(setup.get_guides().front().m_position == 200);
    REQUIRE(setup.get_guides().front().m_id == id);
    REQUIRE(setup.get_label() == "Move Guide");
    drag(setup.m_tools, Point(300, 300), Point(153, 300));
    REQUIRE(setup.get_guides().front().m_position == 50);
    drag(setup.m_tools, Point(150, 300), Point(214, 300),
      Qt::ShiftModifier);
    REQUIRE(setup.get_guides().front().m_position == 110);
    drag(setup.m_tools, Point(210, 300), Point(152, 300),
      Qt::ControlModifier);
    REQUIRE(setup.get_guides().front().m_position == 52);
  }

  TEST_CASE("cursor") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    auto hover = [&] (Point position) {
      tools.move(make_event(position, Qt::NoButton, Qt::NoButton, {}));
      return tools.get_cursor().m_kind;
    };
    REQUIRE(hover(Point(99, 300)) == CursorKind::VERTICAL_GUIDE);
    REQUIRE(hover(Point(300, 99)) == CursorKind::HORIZONTAL_GUIDE);
    REQUIRE(hover(Point(300, 300)) == CursorKind::ARROW);
    tools.press(make_event(Point(300, 99), Qt::LeftButton, Qt::LeftButton, {}));
    tools.move(
      make_event(Point(300, 180), Qt::NoButton, Qt::LeftButton, {}));
    REQUIRE(tools.get_cursor().m_kind == CursorKind::HORIZONTAL_GUIDE);
    tools.release(
      make_event(Point(300, 180), Qt::LeftButton, Qt::NoButton, {}));
    REQUIRE(hover(Point(250, 180)) == CursorKind::HORIZONTAL_GUIDE);
    drag(tools, Point(99, 300), Point(230, 300));
    REQUIRE(hover(Point(230, 250)) == CursorKind::VERTICAL_GUIDE);
  }

  TEST_CASE("delete") {
    auto setup = Setup();
    drag(setup.m_tools, Point(99, 300), Point(230, 300));
    drag(setup.m_tools, Point(99, 300), Point(300, 300));
    REQUIRE(setup.get_guides().size() == 2);
    drag(setup.m_tools, Point(300, 300), Point(230, 300));
    REQUIRE(setup.get_guides().size() == 1);
    REQUIRE(setup.get_label() == "Delete Guide");
    drag(setup.m_tools, Point(230, 300), Point(40, 300));
    REQUIRE(setup.get_guides().empty());
    REQUIRE(setup.get_label() == "Delete Guide");
  }

  TEST_CASE("selection") {
    auto setup = Setup();
    auto& tools = setup.m_tools;
    auto& selection = setup.m_editor.get_selection();
    auto rectangle =
      setup.m_document.get(setup.m_artboard).m_children.front();
    selection.select(rectangle);
    tools.press(
      make_event(Point(99, 300), Qt::LeftButton, Qt::LeftButton, {}));
    tools.move(make_event(Point(230, 300), Qt::NoButton, Qt::LeftButton, {}));
    REQUIRE(selection.get_nodes() == std::vector{setup.m_artboard});
    for(auto& shape : tools.draw().m_shapes) {
      REQUIRE(shape.m_radius == 0);
    }
    tools.release(
      make_event(Point(230, 300), Qt::LeftButton, Qt::NoButton, {}));
    REQUIRE(setup.get_guides().size() == 1);
    REQUIRE(selection.get_nodes() == std::vector{rectangle});
  }

  TEST_CASE("settings") {
    auto setup = Setup();
    drag(setup.m_tools, Point(99, 300), Point(230, 300));
    auto& context = setup.m_tools.get_context();
    REQUIRE(count_guides(setup.m_tools.draw()) == 1);
    context.set_guide_settings(GuideSettings(true, true));
    drag(setup.m_tools, Point(230, 300), Point(300, 300));
    REQUIRE(setup.get_guides().front().m_position == 130);
    drag(setup.m_tools, Point(99, 300), Point(300, 300));
    REQUIRE(setup.get_guides().size() == 2);
    context.set_guide_settings(GuideSettings(false, false));
    REQUIRE(count_guides(setup.m_tools.draw()) == 0);
    drag(setup.m_tools, Point(99, 300), Point(350, 300));
    REQUIRE(setup.get_guides().size() == 2);
  }
}

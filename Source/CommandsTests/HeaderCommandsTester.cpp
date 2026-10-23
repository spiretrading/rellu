#include <numbers>
#include <doctest/doctest.h>
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("HeaderCommands") {
  TEST_CASE("export_mark") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetExportMarkCommand>(
      std::vector{scene.m_rectangle, scene.m_artboard}, true));
    REQUIRE(history.get_undo_label() == "Mark for Export");
    REQUIRE(document.get(scene.m_rectangle).m_header.m_is_marked_for_export);
    REQUIRE(document.get(scene.m_artboard).m_header.m_is_marked_for_export);
    require_undoable(history, std::make_unique<SetExportMarkCommand>(
      std::vector{scene.m_rectangle}, false));
    REQUIRE(history.get_undo_label() == "Unmark for Export");
    REQUIRE(!document.get(scene.m_rectangle).m_header.m_is_marked_for_export);
  }

  TEST_CASE("fixed_position") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetFixedPositionCommand>(
      std::vector{scene.m_rectangle, scene.m_artboard}, true));
    REQUIRE(history.get_undo_label() == "Fix Position When Scrolling");
    REQUIRE(document.get(scene.m_rectangle).m_header.m_is_fixed_position);
    REQUIRE(!document.get(scene.m_artboard).m_header.m_is_fixed_position);
  }

  TEST_CASE("aspect_lock") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetAspectLockCommand>(
      std::vector{scene.m_rectangle, scene.m_artboard}, true));
    REQUIRE(history.get_undo_label() == "Lock Aspect");
    REQUIRE(document.get(scene.m_rectangle).m_header.m_aspect_lock ==
      Size(40, 20));
    REQUIRE(document.get(scene.m_artboard).m_header.m_aspect_lock ==
      Size(400, 300));
    require_undoable(history, std::make_unique<SetAspectLockCommand>(
      std::vector{scene.m_rectangle}, false));
    REQUIRE(!document.get(scene.m_rectangle).m_header.m_aspect_lock);
  }

  TEST_CASE("constraints") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle};
    auto disable = ConstraintsEdit();
    disable.m_is_enabled = false;
    auto manual = ConstraintsEdit();
    manual.m_is_custom = true;
    manual.m_right = true;
    REQUIRE(history.merge(std::make_unique<SetConstraintsCommand>(
      nodes, disable)));
    REQUIRE(history.merge(std::make_unique<SetConstraintsCommand>(
      nodes, manual)));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Change Constraints");
    auto& constraints = document.get(scene.m_rectangle).m_header.m_constraints;
    REQUIRE(!constraints.m_is_enabled);
    REQUIRE(constraints.m_is_custom);
    REQUIRE(!constraints.m_left);
    REQUIRE(constraints.m_right);
    REQUIRE(constraints.m_width);
    REQUIRE(constraints.m_top);
    REQUIRE(constraints.m_height);
    REQUIRE(history.undo());
    REQUIRE(document.get(scene.m_rectangle).m_header.m_constraints ==
      Constraints());
  }

  TEST_CASE("constraint_pins") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle};
    auto pins = ConstraintsEdit();
    pins.m_left = true;
    pins.m_right = true;
    pins.m_width = true;
    require_undoable(
      history, std::make_unique<SetConstraintsCommand>(nodes, pins));
    auto& constraints = document.get(scene.m_rectangle).m_header.m_constraints;
    REQUIRE(constraints.m_is_custom);
    REQUIRE(!constraints.m_left);
    REQUIRE(constraints.m_width);
    REQUIRE(constraints.m_right);
    auto left = ConstraintsEdit();
    left.m_left = true;
    require_undoable(
      history, std::make_unique<SetConstraintsCommand>(nodes, left));
    auto& pinned = document.get(scene.m_rectangle).m_header.m_constraints;
    REQUIRE(pinned.m_left);
    REQUIRE(!pinned.m_right);
  }

  TEST_CASE("constraint_propagation") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto disable = ConstraintsEdit();
    disable.m_is_enabled = false;
    require_undoable(history, std::make_unique<SetConstraintsCommand>(
      std::vector{scene.m_group}, disable));
    REQUIRE(!document.get(scene.m_ellipse).m_header.m_constraints.
      m_is_enabled);
    REQUIRE(
      !document.get(scene.m_text).m_header.m_constraints.m_is_enabled);
    require_undoable(history, std::make_unique<SetConstraintsCommand>(
      std::vector{scene.m_artboard}, disable));
    REQUIRE(!document.get(scene.m_artboard).m_header.m_constraints.
      m_is_enabled);
    REQUIRE(
      document.get(scene.m_rectangle).m_header.m_constraints.m_is_enabled);
  }

  TEST_CASE("reset_3d") {
    auto document = Document();
    auto scene = build_scene(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_rectangle).m_header;
      header.m_transform_3d = Transform3d();
      header.m_transform_3d->m_rotation_x = 20;
      transaction.replace_header(header);
      transaction.commit();
    }
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    REQUIRE(!registry.make("object.transform.reset_3d", document,
      std::vector{scene.m_loose}));
    auto history = History(document);
    require_undoable(history, registry.make("object.transform.reset_3d",
      document, std::vector{scene.m_rectangle}));
    REQUIRE(history.get_undo_label() == "Reset 3D Transforms");
    REQUIRE(!document.get(scene.m_rectangle).m_header.m_transform_3d);
  }

  TEST_CASE("flip") {
    auto document = Document();
    auto scene = build_scene(document);
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    REQUIRE(!registry.make("object.transform.flip_horizontally", document,
      std::vector{scene.m_artboard}));
    REQUIRE(!registry.make("object.transform.flip_vertically", document,
      std::vector{scene.m_instance}));
    {
      auto transaction = Transaction(document);
      auto role = Group();
      role.m_repeat_grid =
        RepeatGrid(Size(40, 40), 20, 10, 1, 1, Size(40, 40));
      auto grid = transaction.append(scene.m_artboard, make_header("Grid"),
        role);
      transaction.append(grid, make_header("Cell"), Group());
      transaction.commit();
      REQUIRE(!registry.make("object.transform.flip_horizontally", document,
        std::vector{grid}));
    }
    auto nodes = std::vector{scene.m_rectangle, scene.m_group};
    auto rectangle = *get_world_bounds(document, scene.m_rectangle);
    auto bounds = *get_world_bounds(document, nodes);
    auto history = History(document);
    require_undoable(history, registry.make(
      "object.transform.flip_horizontally", document, nodes));
    REQUIRE(history.get_undo_label() == "Flip Horizontally");
    REQUIRE(document.get(
      scene.m_rectangle).m_header.m_transform.get_determinant() > 0);
    require_near(*get_world_bounds(document, scene.m_rectangle),
      Rect(2 * bounds.m_x + bounds.m_width - rectangle.m_x - rectangle.m_width,
        rectangle.m_y, rectangle.m_width, rectangle.m_height));
    require_undoable(history, registry.make(
      "object.transform.flip_vertically", document, nodes));
    REQUIRE(history.get_undo_label() == "Flip Vertically");
    REQUIRE(document.get(
      scene.m_rectangle).m_header.m_transform.get_determinant() > 0);
    require_near(*get_world_bounds(document, scene.m_rectangle),
      Rect(2 * bounds.m_x + bounds.m_width - rectangle.m_x - rectangle.m_width,
        2 * bounds.m_y + bounds.m_height - rectangle.m_y - rectangle.m_height,
        rectangle.m_width, rectangle.m_height));
  }

  TEST_CASE("flip_text") {
    auto document = Document();
    auto text = NodeId();
    {
      auto transaction = Transaction(document);
      auto payload = make_text(u"Flip");
      payload.m_frame = PositionedFrame();
      text = transaction.append(
        NodeId(), make_header("Flip", make_translation(100, 100)), payload);
      transaction.commit();
    }
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    auto bounds = *get_world_bounds(document, text);
    auto history = History(document);
    require_undoable(history, registry.make(
      "object.transform.flip_vertically", document, std::vector{text}));
    REQUIRE(document.get(text).get_text()->m_is_flipped_vertically);
    require_near(document.get(text).m_header.m_transform,
      make_translation(100, 100));
    require_near(*get_world_bounds(document, text), bounds);
    history.undo();
    auto local = estimate_text_bounds(*document.get(text).get_text());
    require_undoable(history, registry.make(
      "object.transform.flip_horizontally", document, std::vector{text}));
    REQUIRE(document.get(text).get_text()->m_is_flipped_vertically);
    auto& transform = document.get(text).m_header.m_transform;
    REQUIRE(transform.m_a == doctest::Approx(-1));
    REQUIRE(transform.m_d == doctest::Approx(-1));
    REQUIRE(transform.m_tx ==
      doctest::Approx(100 + 2 * local.m_x + local.m_width));
    REQUIRE(transform.m_ty ==
      doctest::Approx(100 + 2 * local.m_y + local.m_height));
    require_near(*get_world_bounds(document, text), bounds);
    require_undoable(history, registry.make(
      "object.transform.flip_vertically", document, std::vector{text}));
    REQUIRE(!document.get(text).get_text()->m_is_flipped_vertically);
  }

  TEST_CASE("flip_rotation") {
    struct Case {
      double m_rotation;
      bool m_is_horizontal;
      double m_expected;
    };
    for(auto& entry : {Case(30, true, 150), Case(-30, true, -150),
        Case(30, false, 330), Case(90, false, 270), Case(-90, false, -270),
        Case(-90, true, -90), Case(0, true, 0), Case(360, true, 0)}) {
      auto document = Document();
      auto id = NodeId();
      {
        auto transaction = Transaction(document);
        auto header = make_header("Turned", make_translation(100, 100) *
          make_rotation(entry.m_rotation * std::numbers::pi / 180));
        header.m_rotation = entry.m_rotation;
        id = transaction.append(
          NodeId(), header, make_rectangle_shape(40, 20));
        transaction.commit();
      }
      auto registry = CommandRegistry();
      add_header_commands(registry, estimate_text_bounds);
      auto bounds = *get_world_bounds(document, id);
      auto history = History(document);
      history.execute(registry.make(entry.m_is_horizontal ?
        "object.transform.flip_horizontally" :
        "object.transform.flip_vertically", document, std::vector{id}));
      auto& header = document.get(id).m_header;
      REQUIRE(header.m_rotation == doctest::Approx(entry.m_expected));
      REQUIRE(header.m_transform.get_determinant() > 0);
      REQUIRE(is_congruent(entry.m_expected, header.m_transform));
      require_near(*get_world_bounds(document, id), bounds);
    }
  }

  TEST_CASE("flip_rotated_text") {
    auto document = Document();
    auto text = NodeId();
    {
      auto transaction = Transaction(document);
      auto payload = make_text(u"Turn");
      payload.m_frame = PositionedFrame();
      auto header = make_header("Turn", make_translation(100, 100) *
        make_rotation(std::numbers::pi / 6));
      header.m_rotation = 30;
      text = transaction.append(NodeId(), header, payload);
      transaction.commit();
    }
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    auto bounds = *get_world_bounds(document, text);
    auto history = History(document);
    require_undoable(history, registry.make(
      "object.transform.flip_horizontally", document, std::vector{text}));
    REQUIRE(document.get(text).m_header.m_rotation == doctest::Approx(150));
    REQUIRE(document.get(text).get_text()->m_is_flipped_vertically);
    REQUIRE(get_rotation(document.get(text).m_header.m_transform) ==
      doctest::Approx(150));
    require_near(*get_world_bounds(document, text), bounds);
    require_undoable(history, registry.make(
      "object.transform.flip_vertically", document, std::vector{text}));
    REQUIRE(document.get(text).m_header.m_rotation == doctest::Approx(210));
    REQUIRE(!document.get(text).get_text()->m_is_flipped_vertically);
    REQUIRE(get_rotation(document.get(text).m_header.m_transform) ==
      doctest::Approx(-150));
  }

  TEST_CASE("flip_mixed") {
    auto document = Document();
    auto scene = build_scene(document);
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    auto nodes = std::vector{scene.m_rectangle, scene.m_instance};
    auto instance = get_world_transform(document, scene.m_instance);
    auto bounds = *get_world_bounds(document, nodes);
    auto history = History(document);
    require_undoable(history,
      registry.make("object.transform.flip_horizontally", document, nodes));
    auto flipped = get_world_transform(document, scene.m_instance);
    REQUIRE(flipped.get_determinant() > 0);
    REQUIRE(flipped.m_tx ==
      doctest::Approx(2 * bounds.get_center().m_x - instance.m_tx - 80));
    REQUIRE(!document.get(scene.m_reference).get_reference()->m_transform);
  }

  TEST_CASE("pixel_grid") {
    auto document = Document();
    auto scene = build_scene(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_rectangle).m_header;
      header.m_transform = make_translation(10.4, 9.6);
      transaction.replace_header(header);
      auto shape = *document.get(scene.m_rectangle).get_shape();
      shape.m_geometry = RectangleShape(Rect(0, 0, 40.3, 19.8));
      transaction.replace_payload(scene.m_rectangle, shape);
      transaction.commit();
    }
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    REQUIRE(!registry.make("object.align_to_pixel_grid", document,
      std::vector<NodeId>()));
    auto history = History(document);
    auto aligned = registry.make("object.align_to_pixel_grid", document,
      std::vector{scene.m_loose});
    REQUIRE(aligned);
    REQUIRE(!history.execute(std::move(aligned)));
    require_undoable(history, registry.make("object.align_to_pixel_grid",
      document, std::vector{scene.m_rectangle, scene.m_loose}));
    REQUIRE(history.get_undo_label() == "Align to Pixel Grid");
    require_near(*get_world_bounds(document, scene.m_rectangle),
      Rect(110, 60, 41, 19));
  }

  TEST_CASE("mark_for_export_toggle") {
    auto document = Document();
    auto scene = build_scene(document);
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle, scene.m_ellipse};
    require_undoable(
      history, registry.make("object.mark_for_export", document, nodes));
    REQUIRE(document.get(scene.m_ellipse).m_header.m_is_marked_for_export);
    require_undoable(
      history, registry.make("object.mark_for_export", document, nodes));
    REQUIRE(!document.get(scene.m_ellipse).m_header.m_is_marked_for_export);
    REQUIRE(!registry.make("object.mark_for_export", document,
      std::vector{scene.m_master}));
  }
}

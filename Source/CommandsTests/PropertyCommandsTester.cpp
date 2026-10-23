#include <doctest/doctest.h>
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("PropertyCommands") {
  TEST_CASE("opacity") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetOpacityCommand>(
      std::vector{scene.m_rectangle, scene.m_group}, 0.5));
    REQUIRE(history.get_undo_label() == "Change Opacity");
    REQUIRE(document.get(scene.m_rectangle).m_header.m_opacity == 0.5);
    REQUIRE(document.get(scene.m_group).m_header.m_opacity == 0.5);
    REQUIRE(history.execute(std::make_unique<SetOpacityCommand>(
      std::vector{scene.m_rectangle}, 2)));
    REQUIRE(document.get(scene.m_rectangle).m_header.m_opacity == 1);
    REQUIRE(!history.execute(std::make_unique<SetOpacityCommand>(
      std::vector{scene.m_rectangle}, 1)));
  }

  TEST_CASE("opacity_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle, scene.m_group};
    REQUIRE(history.merge(std::make_unique<SetOpacityCommand>(nodes, 0.9)));
    REQUIRE(history.merge(std::make_unique<SetOpacityCommand>(nodes, 0.8)));
    REQUIRE(history.merge(std::make_unique<SetOpacityCommand>(
      std::vector{scene.m_rectangle}, 0.7)));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(document.get(scene.m_group).m_header.m_opacity == 0.8);
    REQUIRE(document.get(scene.m_rectangle).m_header.m_opacity == 0.7);
  }

  TEST_CASE("visibility") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetVisibilityCommand>(
      std::vector{scene.m_rectangle}, false));
    REQUIRE(history.get_undo_label() == "Hide");
    REQUIRE(!document.get(scene.m_rectangle).m_header.m_is_visible);
    require_undoable(history, std::make_unique<SetVisibilityCommand>(
      std::vector{scene.m_rectangle}, true));
    REQUIRE(history.get_undo_label() == "Show");
    REQUIRE(document.get(scene.m_rectangle).m_header.m_is_visible);
  }

  TEST_CASE("lock") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history, std::make_unique<SetLockCommand>(
      std::vector{scene.m_rectangle, scene.m_loose}, true));
    REQUIRE(history.get_undo_label() == "Lock");
    REQUIRE(document.get(scene.m_loose).m_header.m_is_locked);
    require_undoable(history, std::make_unique<SetLockCommand>(
      std::vector{scene.m_loose}, false));
    REQUIRE(history.get_undo_label() == "Unlock");
    REQUIRE(!document.get(scene.m_loose).m_header.m_is_locked);
  }

  TEST_CASE("rename") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    require_undoable(history,
      std::make_unique<RenameCommand>(scene.m_rectangle, "Box"));
    REQUIRE(history.get_undo_label() == "Rename");
    REQUIRE(document.get(scene.m_rectangle).m_header.m_name == "Box");
  }

  TEST_CASE("rename_merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    REQUIRE(history.merge(
      std::make_unique<RenameCommand>(scene.m_rectangle, "B")));
    REQUIRE(history.merge(
      std::make_unique<RenameCommand>(scene.m_rectangle, "Bo")));
    REQUIRE(history.merge(
      std::make_unique<RenameCommand>(scene.m_rectangle, "Box")));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get(scene.m_rectangle).m_header.m_name == "Box");
    REQUIRE(history.merge(
      std::make_unique<RenameCommand>(scene.m_loose, "Corner")));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(document.get(scene.m_rectangle).m_header.m_name == "Rectangle");
  }

  TEST_CASE("color_profile") {
    auto document = Document();
    build_scene(document);
    auto history = History(document);
    auto& resources = document.get_resources();
    REQUIRE(!history.execute(
      std::make_unique<SetColorProfileCommand>(ColorProfile::UNMANAGED)));
    REQUIRE(resources.m_color_profile == ColorProfile::NONE);
    require_undoable(history,
      std::make_unique<SetColorProfileCommand>(ColorProfile::SRGB));
    REQUIRE(history.get_undo_label() == "sRGB");
    REQUIRE(resources.m_color_profile == ColorProfile::SRGB);
    REQUIRE(history.is_dirty());
    REQUIRE(!history.execute(
      std::make_unique<SetColorProfileCommand>(ColorProfile::SRGB)));
    require_undoable(history,
      std::make_unique<SetColorProfileCommand>(ColorProfile::UNMANAGED));
    REQUIRE(history.get_undo_label() == "Unmanaged");
    REQUIRE(resources.m_color_profile == ColorProfile::UNMANAGED);
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(resources.m_color_profile == ColorProfile::NONE);
    REQUIRE(!history.is_dirty());
  }
}

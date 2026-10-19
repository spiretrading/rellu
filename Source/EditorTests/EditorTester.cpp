#include <doctest/doctest.h>
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  CommandRegistry make_registry() {
    auto registry = CommandRegistry();
    add_standard_commands(registry);
    return registry;
  }
}

TEST_SUITE("Editor") {
  TEST_CASE("group") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto registry = make_registry();
    auto& selection = editor.get_selection();
    selection.select({scene.m_rectangle, scene.m_mask});
    REQUIRE(editor.execute(registry, "object.group"));
    REQUIRE(editor.get_history().get_undo_label() == "Group");
    REQUIRE(selection.get_size() == 1);
    auto group = selection.get_primary();
    REQUIRE(document.get_children(group) ==
      std::vector{scene.m_rectangle, scene.m_mask});
    REQUIRE(editor.undo());
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_mask});
    REQUIRE(selection.get_primary() == scene.m_mask);
    REQUIRE(editor.redo());
    REQUIRE(selection.get_nodes() == std::vector{group});
    REQUIRE(editor.execute(registry, "object.ungroup"));
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_mask});
    REQUIRE(!editor.execute(registry, "object.ungroup"));
  }

  TEST_CASE("delete") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto registry = make_registry();
    auto& selection = editor.get_selection();
    selection.select({scene.m_rectangle, scene.m_group}, scene.m_rectangle);
    REQUIRE(editor.execute(registry, "edit.delete"));
    REQUIRE(selection.is_empty());
    REQUIRE(!editor.execute(registry, "edit.delete"));
    REQUIRE(editor.undo());
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_group});
    REQUIRE(selection.get_primary() == scene.m_rectangle);
    REQUIRE(editor.redo());
    REQUIRE(selection.is_empty());
    REQUIRE(!editor.redo());
  }

  TEST_CASE("duplicate") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto registry = make_registry();
    editor.get_selection().select(scene.m_rectangle);
    REQUIRE(editor.execute(registry, "edit.duplicate"));
    auto copy = editor.get_selection().get_primary();
    REQUIRE(copy != scene.m_rectangle);
    REQUIRE(document.get(copy).m_header.m_name == "Rectangle");
  }

  TEST_CASE("merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto& selection = editor.get_selection();
    auto& history = editor.get_history();
    selection.select(scene.m_rectangle);
    auto nudge = [&] {
      return editor.merge(make_nudge_command(selection.get_nodes(),
        Point(1, 0)));
    };
    REQUIRE(nudge());
    REQUIRE(nudge());
    REQUIRE(history.get_undo_count() == 1);
    selection.select(scene.m_loose);
    selection.select(scene.m_rectangle);
    REQUIRE(nudge());
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(113, 60));
    selection.select(scene.m_loose);
    REQUIRE(editor.undo());
    REQUIRE(selection.get_nodes() == std::vector{scene.m_rectangle});
    REQUIRE(editor.undo());
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(110, 60));
  }

  TEST_CASE("drag") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto& selection = editor.get_selection();
    selection.select({scene.m_rectangle, scene.m_loose});
    REQUIRE(editor.execute(std::make_unique<TranslateCommand>(
      selection.get_nodes(), Point(2, 0))));
    for(auto i = 0; i != 5; ++i) {
      REQUIRE(editor.merge(std::make_unique<TranslateCommand>(
        selection.get_nodes(), Point(2, 1))));
    }
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(get_world_transform(document, scene.m_loose) ==
      make_translation(-38, -45));
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_rectangle, scene.m_loose});
  }

  TEST_CASE("active_artboard") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    REQUIRE(editor.get_context().get_artboard().is_nil());
    editor.get_selection().select(scene.m_ellipse);
    REQUIRE(editor.get_context().get_artboard() == scene.m_artboard);
    editor.get_selection().select(scene.m_loose);
    REQUIRE(editor.get_context().get_artboard() == scene.m_artboard);
  }

  TEST_CASE("select_all") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto& selection = editor.get_selection();
    auto everything = std::vector{scene.m_rectangle, scene.m_group,
      scene.m_mask, scene.m_instance, scene.m_loose};
    editor.execute(std::make_unique<SetLockCommand>(
      std::vector{scene.m_mask, scene.m_loose}, true));
    editor.select_all();
    REQUIRE(selection.get_nodes() == everything);
    editor.get_context().set_artboard(scene.m_artboard);
    editor.select_all();
    REQUIRE(selection.get_nodes() == everything);
    editor.get_context().enter(scene.m_group);
    editor.select_all();
    REQUIRE(selection.get_nodes() ==
      std::vector{scene.m_ellipse, scene.m_text});
  }

  TEST_CASE("undo_without_records") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto& selection = editor.get_selection();
    REQUIRE(!editor.undo());
    editor.get_history().execute(std::make_unique<DeleteCommand>(
      std::vector{scene.m_loose}));
    selection.select(scene.m_rectangle);
    REQUIRE(editor.undo());
    REQUIRE(document.find(scene.m_loose));
    REQUIRE(selection.get_nodes() == std::vector{scene.m_rectangle});
  }

  TEST_CASE("instance_edit") {
    auto document = Document();
    auto scene = build_scene(document);
    auto editor = Editor(document);
    auto& selection = editor.get_selection();
    selection.select(scene.m_reference);
    auto command = editor.execute(std::make_unique<SetFillCommand>(
      std::vector{scene.m_reference}, Fill(SolidFill(make_color(0, 0, 255)))));
    REQUIRE(command);
    REQUIRE(dynamic_cast<const SetFillCommand*>(command));
    auto& node = document.get(scene.m_reference);
    REQUIRE(!node.get_reference());
    REQUIRE(std::get<SolidFill>(node.get_shape()->m_style.m_fill).m_color ==
      make_color(0, 0, 255));
    auto& overrides = document.get(scene.m_instance).get_group()->
      m_component_instance->m_overrides;
    REQUIRE(overrides.m_properties.at(scene.m_reference) ==
      std::vector<std::string>{"fill"});
    REQUIRE(editor.merge(std::make_unique<SetFillCommand>(
      std::vector{scene.m_reference}, Fill(SolidFill(make_color(0, 255, 0))))));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    REQUIRE(editor.undo());
    REQUIRE(document.get(scene.m_reference).get_reference());
    REQUIRE(selection.get_nodes() == std::vector{scene.m_reference});
  }

  TEST_CASE("const_access") {
    auto document = Document();
    build_scene(document);
    auto editor = Editor(document);
    const auto& view = editor;
    REQUIRE(view.get_selection().is_empty());
    REQUIRE(!view.get_history().can_undo());
    REQUIRE(view.get_context().get_container().is_nil());
    REQUIRE(&view.get_document() == &document);
  }
}

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Nested {
    ComponentId m_icon;
    ComponentId m_button;
    NodeId m_icon_master;
    NodeId m_icon_shape;
    NodeId m_button_master;
    NodeId m_nested;
    NodeId m_nested_reference;
    NodeId m_frame;
    NodeId m_instance;
  };

  Nested build_nested(Document& document) {
    auto nested = Nested();
    auto transaction = Transaction(document);
    nested.m_icon_master =
      transaction.insert_root(make_header("Icon"), Group());
    nested.m_icon_shape = transaction.append(nested.m_icon_master,
      make_header("Glyph"), make_rectangle_shape(8, 8));
    auto icon = Component();
    icon.m_name = "Icon";
    icon.m_master = nested.m_icon_master;
    nested.m_icon = transaction.add_component(icon);
    nested.m_button_master =
      transaction.insert_root(make_header("Button"), Group());
    nested.m_frame = transaction.append(nested.m_button_master,
      make_header("Frame"), make_rectangle_shape(40, 20));
    auto role = Group();
    role.m_component_instance = ComponentInstance(nested.m_icon, StateId(),
      Size(8, 8));
    nested.m_nested = transaction.append(nested.m_button_master,
      make_header("Icon", make_translation(4, 4)), role);
    nested.m_nested_reference = transaction.append(nested.m_nested,
      make_header(""), Reference(nested.m_icon_shape));
    auto synced = *document.get(nested.m_nested).get_group();
    synced.m_component_instance->m_sync_map.emplace(
      nested.m_nested_reference, nested.m_icon_shape);
    transaction.replace_payload(nested.m_nested, synced);
    auto button = Component();
    button.m_name = "Button";
    button.m_master = nested.m_button_master;
    nested.m_button = transaction.add_component(button);
    nested.m_instance = insert_instance(document, transaction,
      nested.m_button, NodeId(), 0, make_translation(100, 100));
    transaction.commit();
    document.clear_history();
    return nested;
  }

  class ReplaceHeaderCommand : public Command {
    public:
      explicit ReplaceHeaderCommand(NodeHeader header)
        : m_header(std::move(header)) {}

      std::string get_label() const override {
        return "Replace Header";
      }

      void apply(const Document&, Transaction& transaction) override {
        transaction.replace_header(m_header);
      }

    private:
      NodeHeader m_header;
  };
}

TEST_SUITE("Components") {
  TEST_CASE("queries") {
    auto document = Document();
    auto nested = build_nested(document);
    auto& children = document.get(nested.m_instance).m_children;
    REQUIRE(children.size() == 2);
    REQUIRE(find_instance(document, children.front()) == nested.m_instance);
    REQUIRE(find_instance(document, nested.m_instance) == nested.m_instance);
    REQUIRE(find_instance(document, nested.m_frame).is_nil());
    REQUIRE(find_instance(document, nested.m_nested_reference) ==
      nested.m_nested);
    auto instances = find_instances(document, nested.m_icon);
    REQUIRE(instances == std::vector{nested.m_nested});
    REQUIRE(find_main_component(document, nested.m_button).is_nil());
    REQUIRE(get_source_tree(document,
      *get_instance_role(document.get(nested.m_instance))) ==
      nested.m_button_master);
    REQUIRE(!is_linked(document, nested.m_button));
  }

  TEST_CASE("materialize_reference") {
    auto document = Document();
    auto nested = build_nested(document);
    auto reference = document.get(nested.m_instance).m_children.back();
    auto children = std::vector<NodeId>();
    {
      auto transaction = Transaction(document);
      children = materialize_reference(document, transaction, reference);
      transaction.commit();
    }
    auto& node = document.get(reference);
    REQUIRE(node.m_header.m_name == "Icon");
    REQUIRE(node.m_header.m_transform == make_translation(4, 4));
    auto role = get_instance_role(node);
    REQUIRE(role);
    REQUIRE(role->m_component == nested.m_icon);
    REQUIRE(!role->m_is_main);
    REQUIRE(children.size() == 1);
    REQUIRE(node.m_children == children);
    REQUIRE(document.get(children.front()).get_reference()->m_source ==
      nested.m_nested_reference);
    REQUIRE(role->m_sync_map.at(children.front()) == nested.m_icon_shape);
    auto& outer = *get_instance_role(document.get(nested.m_instance));
    REQUIRE(outer.m_sync_map.at(children.front()) ==
      nested.m_nested_reference);
    REQUIRE(get_index(document, reference) == 1);
  }

  TEST_CASE("materialize_references") {
    auto document = Document();
    auto nested = build_nested(document);
    {
      auto transaction = Transaction(document);
      materialize_references(
        document, transaction, std::vector{nested.m_instance});
      transaction.commit();
    }
    auto& children = document.get(nested.m_instance).m_children;
    REQUIRE(document.get(children.front()).get_shape());
    auto& icon = document.get(children.back());
    REQUIRE(get_instance_role(icon));
    REQUIRE(document.get(icon.m_children.front()).get_shape());
    REQUIRE(document.get(nested.m_nested_reference).get_reference());
  }

  TEST_CASE("differences") {
    auto document = Document();
    auto nested = build_nested(document);
    auto reference = document.get(nested.m_instance).m_children.front();
    REQUIRE(find_differences(document, reference, nested.m_frame).empty());
    auto shape = *document.get(nested.m_frame).get_shape();
    shape.m_style.m_fill = SolidFill(make_color(0, 0, 255));
    std::get<RectangleShape>(shape.m_geometry).m_bounds.m_width = 50;
    auto node = document.get(nested.m_frame);
    node.m_header.m_opacity = 0.5;
    node.m_payload = shape;
    auto& source = document.get(nested.m_frame);
    auto changed = source;
    REQUIRE(update_properties(changed, node, false, {"fill"}));
    REQUIRE(changed.m_header.m_opacity == 0.5);
    REQUIRE(std::get<SolidFill>(changed.get_shape()->m_style.m_fill).m_color ==
      make_color(255, 0, 0));
    REQUIRE(std::get<RectangleShape>(changed.get_shape()->m_geometry).
      m_bounds.m_width == 50);
    auto copied = source;
    REQUIRE(copy_properties(copied, node, {"fill"}));
    REQUIRE(copied.m_header.m_opacity == 1);
    REQUIRE(std::get<SolidFill>(copied.get_shape()->m_style.m_fill).m_color ==
      make_color(0, 0, 255));
    REQUIRE(!copy_properties(copied, node, {"visible"}));
  }

  TEST_CASE("instance_edit") {
    auto document = Document();
    auto nested = build_nested(document);
    auto reference = document.get(nested.m_instance).m_children.front();
    auto move = std::make_unique<TranslateCommand>(
      std::vector{reference}, Point(1, 1));
    auto move_address = move.get();
    REQUIRE(make_instance_edit(document, std::vector{reference},
      std::move(move)).get() == move_address);
    auto fill = std::make_unique<SetFillCommand>(std::vector{nested.m_frame},
      Fill(SolidFill(make_color(0, 0, 255))));
    auto fill_address = fill.get();
    REQUIRE(make_instance_edit(document, std::vector{nested.m_frame},
      std::move(fill)).get() == fill_address);
    auto history = History(document);
    auto first = make_instance_edit(document, std::vector{reference},
      std::make_unique<SetOpacityCommand>(std::vector{reference}, 0.5));
    REQUIRE(first->get_label() == "Change Opacity");
    REQUIRE(history.merge(std::move(first)));
    REQUIRE(!document.get(reference).get_reference());
    REQUIRE(history.merge(make_instance_edit(document, std::vector{reference},
      std::make_unique<SetOpacityCommand>(std::vector{reference}, 0.25))));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get(reference).m_header.m_opacity == 0.25);
    REQUIRE(history.undo());
    REQUIRE(document.get(reference).get_reference());
  }

  TEST_CASE("blend_mode_instance_edit") {
    auto document = Document();
    auto nested = build_nested(document);
    auto icon = document.get(nested.m_instance).m_children.back();
    auto blend = std::make_unique<SetBlendModeCommand>(
      std::vector{icon}, BlendMode::MULTIPLY);
    auto address = blend.get();
    auto command =
      make_instance_edit(document, std::vector{icon}, std::move(blend));
    REQUIRE(command.get() == address);
    auto history = History(document);
    REQUIRE(history.execute(std::move(command)));
    REQUIRE(!document.get(icon).get_reference());
    REQUIRE(document.get(icon).m_header.m_blend_mode == BlendMode::MULTIPLY);
    auto& children = document.get(icon).m_children;
    REQUIRE(!children.empty());
    for(auto& child : children) {
      REQUIRE(document.get(child).get_reference());
    }
    REQUIRE(get_instance_role(document.get(nested.m_instance))->m_overrides.
      m_properties.at(icon) == std::vector<std::string>{"blendMode"});
  }

  TEST_CASE("rendered_header") {
    auto document = Document();
    auto nested = build_nested(document);
    auto reference = document.get(nested.m_instance).m_children.front();
    REQUIRE(find_rendered_header(document, reference) ==
      &document.get(nested.m_frame).m_header);
    REQUIRE(find_rendered_header(document, nested.m_frame) ==
      &document.get(nested.m_frame).m_header);
    REQUIRE(!find_rendered_header(document, generate_node_id()));
  }

  TEST_CASE("edit_rendered_header") {
    auto document = Document();
    auto nested = build_nested(document);
    auto reference = document.get(nested.m_instance).m_children.back();
    {
      auto transaction = Transaction(document);
      REQUIRE(!edit_rendered_header(document, transaction, reference,
        [] (const auto& node, auto& header) {
          REQUIRE(get_instance_role(node));
          header.m_is_visible = true;
        }));
      transaction.commit();
    }
    REQUIRE(document.get(reference).get_reference());
    {
      auto transaction = Transaction(document);
      REQUIRE(edit_rendered_header(document, transaction, reference,
        [] (const auto&, auto& header) { header.m_is_visible = false; }));
      transaction.commit();
    }
    auto& node = document.get(reference);
    REQUIRE(!node.m_header.m_is_visible);
    REQUIRE(node.m_header.m_name == "Icon");
    REQUIRE(get_instance_role(node));
    REQUIRE(document.get(node.m_children.front()).get_reference());
    {
      auto transaction = Transaction(document);
      REQUIRE(edit_rendered_header(document, transaction, nested.m_frame,
        [] (const auto&, auto& header) { header.m_opacity = 0.5; }));
      transaction.commit();
    }
    REQUIRE(document.get(nested.m_frame).m_header.m_opacity == 0.5);
  }

  TEST_CASE("reference_header") {
    auto document = Document();
    auto nested = build_nested(document);
    auto history = History(document);
    auto reference = document.get(nested.m_instance).m_children.back();
    REQUIRE(!history.execute(std::make_unique<SetVisibilityCommand>(
      std::vector{reference}, true)));
    REQUIRE(history.execute(std::make_unique<SetVisibilityCommand>(
      std::vector{reference}, false)));
    auto& node = document.get(reference);
    REQUIRE(!node.get_reference());
    REQUIRE(!node.m_header.m_is_visible);
    REQUIRE(document.get(node.m_children.front()).get_reference());
    auto& role = *get_instance_role(document.get(nested.m_instance));
    REQUIRE(role.m_overrides.m_properties.at(reference) ==
      std::vector<std::string>{"visible"});
    REQUIRE(get_instance_role(node)->m_overrides.m_properties.at(reference) ==
      std::vector<std::string>{"visible"});
    REQUIRE(history.execute(
      std::make_unique<RenameCommand>(reference, "Badge")));
    REQUIRE(get_instance_role(document.get(nested.m_instance))->m_overrides.
      m_properties.at(reference) == std::vector<std::string>{"visible",
        "isCustomNameInherited", "name"});
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(document.get(reference).get_reference());
  }

  TEST_CASE("adopted_header") {
    auto document = Document();
    auto nested = build_nested(document);
    auto history = History(document);
    auto reference = document.get(nested.m_instance).m_children.front();
    auto header = document.get(reference).m_header;
    header.m_is_locked = true;
    REQUIRE(history.execute(std::make_unique<ReplaceHeaderCommand>(header)));
    auto& node = document.get(reference);
    REQUIRE(!node.get_reference());
    REQUIRE(node.m_header.m_is_locked);
    REQUIRE(node.m_header.m_name == "Frame");
    REQUIRE(get_instance_role(document.get(nested.m_instance))->
      m_overrides.m_properties.at(reference) ==
        std::vector<std::string>{"locked"});
  }

  TEST_CASE("toggle_reference") {
    auto document = Document();
    auto nested = build_nested(document);
    {
      auto transaction = Transaction(document);
      auto header = document.get(nested.m_frame).m_header;
      header.m_is_visible = false;
      transaction.replace_header(header);
      transaction.commit();
    }
    auto history = History(document);
    auto registry = CommandRegistry();
    add_standard_commands(registry);
    auto reference = document.get(nested.m_instance).m_children.front();
    REQUIRE(history.execute(registry.make(
      "object.toggle_visibility", document, std::vector{reference})));
    REQUIRE(find_rendered_header(document, reference)->m_is_visible);
    REQUIRE(!document.get(nested.m_frame).m_header.m_is_visible);
  }

  TEST_CASE("nested_reference_header") {
    auto document = Document();
    auto nested = build_nested(document);
    auto icon = document.get(nested.m_instance).m_children.back();
    {
      auto transaction = Transaction(document);
      materialize_reference(document, transaction, icon);
      transaction.commit();
    }
    auto history = History(document);
    auto glyph = document.get(icon).m_children.front();
    REQUIRE(document.get(glyph).get_reference());
    REQUIRE(history.execute(
      std::make_unique<SetOpacityCommand>(std::vector{glyph}, 0.25)));
    REQUIRE(!document.get(glyph).get_reference());
    REQUIRE(document.get(glyph).m_header.m_opacity == 0.25);
    REQUIRE(get_instance_role(document.get(icon))->m_overrides.m_properties.
      at(glyph) == std::vector<std::string>{"opacity"});
    REQUIRE(get_instance_role(document.get(nested.m_instance))->m_overrides.
      m_properties.at(glyph) == std::vector<std::string>{"opacity"});
    REQUIRE(document.get(nested.m_icon_shape).m_header.m_opacity == 1);
  }

  TEST_CASE("nested_propagation") {
    auto document = Document();
    auto nested = build_nested(document);
    auto history = History(document);
    {
      auto transaction = Transaction(document);
      auto shape = *document.get(nested.m_icon_shape).get_shape();
      shape.m_style.m_fill = SolidFill(make_color(0, 255, 0));
      transaction.replace_payload(nested.m_icon_shape, shape);
      transaction.commit();
    }
    auto main = history.execute(std::make_unique<RestoreMainComponentCommand>(
      nested.m_icon, make_translation(0, 300)));
    REQUIRE(main);
    auto main_id =
      static_cast<const RestoreMainComponentCommand&>(*main).get_main();
    auto glyph = document.get(main_id).m_children.front();
    history.execute(make_instance_edit(document, std::vector{glyph},
      std::make_unique<SetOpacityCommand>(std::vector{glyph}, 0.5)));
    REQUIRE(document.get(nested.m_icon_shape).m_header.m_opacity == 0.5);
  }
}

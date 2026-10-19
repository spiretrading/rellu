#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"

using namespace Rellu;

namespace {
  constexpr auto LARGE_NUDGE = 10.0;

  bool is_tree_root(const Document& document, const NodeId& id) {
    return document.get(id).m_parent.is_nil() && !document.is_on_canvas(id);
  }

  std::vector<NodeId> get_movable(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_top_level(document, targets);
    std::erase_if(nodes, [&] (const auto& id) {
      return is_tree_root(document, id);
    });
    return nodes;
  }

  std::vector<NodeId> get_distinct(
      const Document& document, const std::vector<NodeId>& targets) {
    auto visited = std::unordered_set<NodeId>();
    auto nodes = std::vector<NodeId>();
    for(auto& id : targets) {
      if(document.find(id) && visited.insert(id).second) {
        nodes.push_back(id);
      }
    }
    return nodes;
  }

  CommandFactory make_arrange_factory(Arrangement arrangement) {
    return [=] (const auto& document, const auto& targets) ->
        std::unique_ptr<Command> {
      auto nodes = get_movable(document, targets);
      if(nodes.empty()) {
        return nullptr;
      }
      return std::make_unique<ArrangeCommand>(std::move(nodes), arrangement);
    };
  }

  CommandFactory make_nudge_factory(Point delta) {
    return [=] (const auto& document, const auto& targets) ->
        std::unique_ptr<Command> {
      auto nodes = get_movable(document, targets);
      if(nodes.empty()) {
        return nullptr;
      }
      return make_nudge_command(std::move(nodes), delta);
    };
  }

  std::unique_ptr<Command> make_delete(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_movable(document, targets);
    if(nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<DeleteCommand>(std::move(nodes));
  }

  std::unique_ptr<Command> make_duplicate(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_movable(document, targets);
    if(nodes.empty()) {
      return nullptr;
    }
    return std::make_unique<DuplicateCommand>(std::move(nodes));
  }

  CommandFactory make_group_factory(TextMeasurer measurer) {
    return [measurer = std::move(measurer)] (const auto& document,
        const auto& targets) -> std::unique_ptr<Command> {
      auto nodes = get_movable(document, targets);
      if(nodes.empty() || std::any_of(nodes.begin(), nodes.end(),
          [&] (const auto& id) { return document.get(id).get_artboard(); })) {
        return nullptr;
      }
      return std::make_unique<GroupCommand>(std::move(nodes), measurer);
    };
  }

  std::unique_ptr<Command> make_ungroup(
      const Document& document, const std::vector<NodeId>& targets) {
    if(auto command = make_ungroup_grid_command(document, targets)) {
      return command;
    }
    if(auto command = make_unlink_component(document, targets)) {
      return command;
    }
    auto nodes = get_top_level(document, targets);
    if(std::none_of(nodes.begin(), nodes.end(),
        [&] (const auto& id) { return is_ungroupable(document, id); })) {
      return make_ungroup_component(document, nodes);
    }
    return std::make_unique<UngroupCommand>(std::move(nodes));
  }

  std::unique_ptr<Command> make_lock_toggle(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_distinct(document, targets);
    if(nodes.empty()) {
      return nullptr;
    }
    auto is_locked = std::any_of(nodes.begin(), nodes.end(),
      [&] (const auto& id) {
        auto header = find_rendered_header(document, id);
        return header && !header->m_is_locked;
      });
    return std::make_unique<SetLockCommand>(std::move(nodes), is_locked);
  }

  std::unique_ptr<Command> make_visibility_toggle(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_distinct(document, targets);
    std::erase_if(nodes, [&] (const auto& id) {
      return document.get(id).get_artboard() ||
        is_inside_compound(document, id);
    });
    if(nodes.empty()) {
      return nullptr;
    }
    auto is_visible = std::none_of(nodes.begin(), nodes.end(),
      [&] (const auto& id) {
        auto header = find_rendered_header(document, id);
        return header && header->m_is_visible;
      });
    return std::make_unique<SetVisibilityCommand>(
      std::move(nodes), is_visible);
  }
}

void CommandRegistry::add(std::string name, CommandFactory factory) {
  if(m_factories.contains(name)) {
    throw std::invalid_argument("Duplicate command: " + name);
  }
  m_factories.emplace(std::move(name), std::move(factory));
}

bool CommandRegistry::contains(std::string_view name) const {
  return m_factories.find(name) != m_factories.end();
}

std::vector<std::string> CommandRegistry::get_names() const {
  auto names = std::vector<std::string>();
  names.reserve(m_factories.size());
  for(auto& entry : m_factories) {
    names.push_back(entry.first);
  }
  return names;
}

std::unique_ptr<Command> CommandRegistry::make(std::string_view name,
    const Document& document, const std::vector<NodeId>& targets) const {
  auto entry = m_factories.find(name);
  if(entry == m_factories.end()) {
    throw std::out_of_range("Unknown command: " + std::string(name));
  }
  return entry->second(document, targets);
}

void Rellu::add_standard_commands(
    CommandRegistry& registry, TextMeasurer measurer) {
  registry.add("edit.delete", make_delete);
  registry.add("edit.duplicate", make_duplicate);
  registry.add("object.group", make_group_factory(std::move(measurer)));
  registry.add("object.ungroup", make_ungroup);
  registry.add("object.toggle_lock", make_lock_toggle);
  registry.add("object.toggle_visibility", make_visibility_toggle);
  registry.add("arrange.bring_to_front",
    make_arrange_factory(Arrangement::BRING_TO_FRONT));
  registry.add("arrange.bring_forward",
    make_arrange_factory(Arrangement::BRING_FORWARD));
  registry.add("arrange.send_backward",
    make_arrange_factory(Arrangement::SEND_BACKWARD));
  registry.add("arrange.send_to_back",
    make_arrange_factory(Arrangement::SEND_TO_BACK));
  registry.add("object.nudge_left", make_nudge_factory(Point(-1, 0)));
  registry.add("object.nudge_right", make_nudge_factory(Point(1, 0)));
  registry.add("object.nudge_up", make_nudge_factory(Point(0, -1)));
  registry.add("object.nudge_down", make_nudge_factory(Point(0, 1)));
  registry.add(
    "object.nudge_left_large", make_nudge_factory(Point(-LARGE_NUDGE, 0)));
  registry.add(
    "object.nudge_right_large", make_nudge_factory(Point(LARGE_NUDGE, 0)));
  registry.add(
    "object.nudge_up_large", make_nudge_factory(Point(0, -LARGE_NUDGE)));
  registry.add(
    "object.nudge_down_large", make_nudge_factory(Point(0, LARGE_NUDGE)));
}

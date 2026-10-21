#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Tools/ComponentDrops.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;

  std::unique_ptr<Command> relabel(
      std::string label, std::unique_ptr<Command> command) {
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(std::move(command));
    return std::make_unique<CompositeCommand>(
      std::move(label), std::move(commands));
  }
}

NodeId Rellu::find_swap_target(const ToolContext& context,
    const ComponentId& component, Point position) {
  auto& document = context.get_document();
  auto& container = context.get_editor().get_context().get_container();
  auto target = NodeId();
  auto current =
    context.get_hit_tester().hit(context.to_document(position), 0);
  for(auto depth = 0; depth != MAXIMUM_DEPTH && !current.is_nil() &&
      current != container; ++depth) {
    auto node = document.find(current);
    if(!node) {
      return NodeId();
    }
    auto group = node->get_group();
    if(node->m_header.m_is_locked || !node->m_header.m_is_visible ||
        group && group->m_repeat_grid) {
      target = NodeId();
    } else if(get_instance_role(*node)) {
      target = current;
    }
    current = node->m_parent;
  }
  if(current != container || target.is_nil() ||
      get_instance_role(document.get(target))->m_component == component) {
    return NodeId();
  }
  return target;
}

std::unique_ptr<Command> Rellu::make_component_drop(
    const ToolContext& context, const ComponentId& component, Point position,
    Qt::KeyboardModifiers modifiers) {
  auto& document = context.get_document();
  auto& components = document.get_resources().m_components;
  auto entry = components.find(component);
  if(entry == components.end()) {
    return nullptr;
  }
  auto is_alt = modifiers.testFlag(Qt::AltModifier);
  auto& measurer = context.get_text_measurer();
  if(auto target = find_swap_target(context, component, position);
      !target.is_nil()) {
    auto swap = std::make_unique<SwapComponentCommand>(
      std::vector{target}, component, measurer);
    if(is_alt) {
      return relabel("Place Graphic", std::move(swap));
    }
    return swap;
  }
  auto point = context.to_document(position);
  point = Point(std::round(point.m_x), std::round(point.m_y));
  auto parent = NodeId();
  auto origin = Point(0, 0);
  auto& canvas = document.get_canvas();
  for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
    if(auto artboard = document.get(*i).get_artboard();
        artboard && artboard->m_frame.contains(point)) {
      parent = *i;
      origin = artboard->m_frame.get_top_left();
      break;
    }
  }
  auto corner = Point(0, 0);
  if(auto bounds = get_local_bounds(document, entry->second.m_master)) {
    corner = bounds->get_top_left();
  }
  auto offset = point - origin - corner;
  auto label = is_alt ? "Place Graphic" : "Place Component";
  return relabel(label, std::make_unique<InsertInstanceCommand>(component,
    parent, make_translation(offset.m_x, offset.m_y), measurer));
}

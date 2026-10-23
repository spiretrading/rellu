#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/TransformCommands.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;

  void edit_headers(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& nodes, auto edit) {
    auto visited = std::unordered_set<NodeId>();
    for(auto& id : nodes) {
      if(visited.insert(id).second) {
        edit_rendered_header(document, transaction, id, edit);
      }
    }
  }

  void assign(bool& target, const std::optional<bool>& source) {
    if(source) {
      target = *source;
    }
  }

  void pin(Constraints& constraints, bool Constraints::* flag, bool value,
      bool Constraints::* released) {
    constraints.*flag = value;
    constraints.m_is_custom = true;
    if(constraints.m_left && constraints.m_width && constraints.m_right &&
        (flag == &Constraints::m_left || flag == &Constraints::m_width ||
          flag == &Constraints::m_right)) {
      constraints.*released = false;
    } else if(constraints.m_top && constraints.m_height &&
        constraints.m_bottom && (flag == &Constraints::m_top ||
          flag == &Constraints::m_height || flag == &Constraints::m_bottom)) {
      constraints.*released = false;
    }
  }

  void set_descendants_enabled(const Document& document,
      Transaction& transaction, const NodeId& id, bool is_enabled,
      int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto children = document.get(id).m_children;
    for(auto& child : children) {
      auto& node = document.get(child);
      if(auto group = node.get_group(); group && group->m_component_instance) {
        continue;
      }
      if(node.m_header.m_constraints.m_is_enabled != is_enabled) {
        auto header = node.m_header;
        header.m_constraints.m_is_enabled = is_enabled;
        transaction.replace_header(std::move(header));
      }
      set_descendants_enabled(
        document, transaction, child, is_enabled, depth + 1);
    }
  }

  void assign(std::optional<bool>& target, const std::optional<bool>& source) {
    if(source) {
      target = source;
    }
  }

  std::vector<NodeId> get_flippable(
      const Document& document, const std::vector<NodeId>& nodes) {
    auto flippable = get_top_level(document, nodes);
    std::erase_if(flippable, [&] (const auto& id) {
      auto& node = document.get(id);
      return node.get_artboard() ||
        (node.m_parent.is_nil() && !document.is_on_canvas(id));
    });
    return flippable;
  }

  std::vector<NodeId> get_existing(
      const Document& document, const std::vector<NodeId>& nodes) {
    auto existing = std::vector<NodeId>();
    for(auto& id : nodes) {
      if(document.find(id) &&
          std::find(existing.begin(), existing.end(), id) == existing.end()) {
        existing.push_back(id);
      }
    }
    return existing;
  }
}

SetExportMarkCommand::SetExportMarkCommand(
  std::vector<NodeId> nodes, bool is_marked) noexcept
  : m_nodes(std::move(nodes)),
    m_is_marked(is_marked) {}

std::string SetExportMarkCommand::get_label() const {
  if(m_is_marked) {
    return "Mark for Export";
  }
  return "Unmark for Export";
}

void SetExportMarkCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes,
    [&] (const auto&, auto& header) {
      header.m_is_marked_for_export = m_is_marked;
    });
}

SetFixedPositionCommand::SetFixedPositionCommand(
  std::vector<NodeId> nodes, bool is_fixed) noexcept
  : m_nodes(std::move(nodes)),
    m_is_fixed(is_fixed) {}

std::string SetFixedPositionCommand::get_label() const {
  return "Fix Position When Scrolling";
}

void SetFixedPositionCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes,
    [&] (const auto& node, auto& header) {
      if(!node.get_artboard()) {
        header.m_is_fixed_position = m_is_fixed;
      }
    });
}

SetAspectLockCommand::SetAspectLockCommand(
  std::vector<NodeId> nodes, bool is_locked, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_is_locked(is_locked),
    m_measurer(std::move(measurer)) {}

std::string SetAspectLockCommand::get_label() const {
  return "Lock Aspect";
}

void SetAspectLockCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes,
    [&] (const auto& node, auto& header) {
      if(!m_is_locked) {
        header.m_aspect_lock = std::nullopt;
        return;
      } else if(header.m_aspect_lock) {
        return;
      }
      if(auto artboard = node.get_artboard()) {
        header.m_aspect_lock = artboard->m_frame.get_size();
      } else if(auto bounds = get_local_bounds(
          document, node.get_id(), BoundsType::GEOMETRY, m_measurer)) {
        header.m_aspect_lock = bounds->get_size();
      }
    });
}

SetConstraintsCommand::SetConstraintsCommand(
  std::vector<NodeId> nodes, ConstraintsEdit edit) noexcept
  : SetConstraintsCommand(
      std::move(nodes), std::move(edit), estimate_text_bounds) {}

SetConstraintsCommand::SetConstraintsCommand(std::vector<NodeId> nodes,
  ConstraintsEdit edit, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_edit(std::move(edit)),
    m_measurer(std::move(measurer)) {}

std::string SetConstraintsCommand::get_label() const {
  return "Change Constraints";
}

void SetConstraintsCommand::apply(
    const Document& document, Transaction& transaction) {
  auto nodes = get_existing(document, m_nodes);
  if(m_edit.m_is_custom.value_or(false)) {
    auto parents = std::vector<NodeId>();
    for(auto& id : nodes) {
      auto& node = document.get(id);
      if(!node.get_artboard() && !node.m_parent.is_nil() &&
          std::find(parents.begin(), parents.end(), node.m_parent) ==
            parents.end()) {
        parents.push_back(node.m_parent);
      }
    }
    for(auto& parent : parents) {
      record_constraints(document, transaction, parent, m_measurer);
    }
  }
  edit_headers(document, transaction, nodes,
    [&] (const auto&, auto& header) {
      auto& constraints = header.m_constraints;
      assign(constraints.m_is_enabled, m_edit.m_is_enabled);
      assign(constraints.m_is_custom, m_edit.m_is_custom);
      for(auto [edit, flag, released] : {
          std::tuple(&ConstraintsEdit::m_left, &Constraints::m_left,
            &Constraints::m_right),
          std::tuple(&ConstraintsEdit::m_width, &Constraints::m_width,
            &Constraints::m_right),
          std::tuple(&ConstraintsEdit::m_right, &Constraints::m_right,
            &Constraints::m_left),
          std::tuple(&ConstraintsEdit::m_top, &Constraints::m_top,
            &Constraints::m_bottom),
          std::tuple(&ConstraintsEdit::m_height, &Constraints::m_height,
            &Constraints::m_bottom),
          std::tuple(&ConstraintsEdit::m_bottom, &Constraints::m_bottom,
            &Constraints::m_top)}) {
        if(auto& value = m_edit.*edit) {
          pin(constraints, flag, *value, released);
        }
      }
    });
  if(!m_edit.m_is_enabled) {
    return;
  }
  for(auto& id : nodes) {
    if(!document.get(id).get_artboard()) {
      set_descendants_enabled(
        document, transaction, id, *m_edit.m_is_enabled, 0);
    }
  }
}

bool SetConstraintsCommand::merge(const Command& command) {
  auto constraints = dynamic_cast<const SetConstraintsCommand*>(&command);
  if(!constraints || constraints->m_nodes != m_nodes) {
    return false;
  }
  auto& edit = constraints->m_edit;
  assign(m_edit.m_is_enabled, edit.m_is_enabled);
  assign(m_edit.m_is_custom, edit.m_is_custom);
  assign(m_edit.m_left, edit.m_left);
  assign(m_edit.m_right, edit.m_right);
  assign(m_edit.m_top, edit.m_top);
  assign(m_edit.m_bottom, edit.m_bottom);
  assign(m_edit.m_width, edit.m_width);
  assign(m_edit.m_height, edit.m_height);
  return true;
}

ResetTransform3dCommand::ResetTransform3dCommand(
  std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

std::string ResetTransform3dCommand::get_label() const {
  return "Reset 3D Transforms";
}

void ResetTransform3dCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes,
    [&] (const auto&, auto& header) {
      header.m_transform_3d = std::nullopt;
    });
}

std::unique_ptr<Command> Rellu::make_flip_command(const Document& document,
    const std::vector<NodeId>& nodes, bool is_horizontal,
    const TextMeasurer& measurer) {
  auto targets = get_flippable(document, get_existing(document, nodes));
  if(std::all_of(targets.begin(), targets.end(), [&] (const auto& id) {
      auto group = document.get(id).get_group();
      return group && (group->m_component_instance ||
        group->m_repeat_grid || group->m_scroll_group);
    })) {
    return nullptr;
  }
  auto bounds =
    get_world_bounds(document, targets, BoundsType::GEOMETRY, measurer);
  if(!bounds) {
    return nullptr;
  }
  auto center = bounds->get_center();
  auto scale = is_horizontal ? make_scale(-1, 1) : make_scale(1, -1);
  auto mapping = make_translation(center.m_x, center.m_y) * scale *
    make_translation(-center.m_x, -center.m_y);
  return std::make_unique<TransformCommand>(std::move(targets), mapping,
    is_horizontal ? "Flip Horizontally" : "Flip Vertically");
}

std::unique_ptr<Command> Rellu::make_pixel_grid_command(
    const Document& document, const std::vector<NodeId>& nodes,
    const TextMeasurer& measurer) {
  auto commands = std::vector<std::unique_ptr<Command>>();
  auto has_target = false;
  for(auto& id : get_top_level(document, get_existing(document, nodes))) {
    if(document.get(id).m_parent.is_nil() && !document.is_on_canvas(id)) {
      continue;
    }
    has_target = true;
    auto bounds =
      get_world_bounds(document, id, BoundsType::GEOMETRY, measurer);
    if(!bounds) {
      continue;
    }
    auto left = std::round(bounds->m_x);
    auto top = std::round(bounds->m_y);
    auto right = std::round(bounds->m_x + bounds->m_width);
    auto bottom = std::round(bounds->m_y + bounds->m_height);
    if(bounds->m_width > 0) {
      right = std::max(right, left + 1);
    }
    if(bounds->m_height > 0) {
      bottom = std::max(bottom, top + 1);
    }
    auto aligned = Rect(left, top, right - left, bottom - top);
    auto world = get_world_transform(document, id);
    if(world.m_b != 0 || world.m_c != 0) {
      aligned.m_width = bounds->m_width;
      aligned.m_height = bounds->m_height;
    }
    if(aligned == *bounds) {
      continue;
    }
    commands.push_back(std::make_unique<TransformCommand>(std::vector{id},
      make_rect_mapping(*bounds, aligned), "Align to Pixel Grid"));
  }
  if(!has_target) {
    return nullptr;
  }
  return std::make_unique<CompositeCommand>(
    "Align to Pixel Grid", std::move(commands));
}

void Rellu::add_header_commands(
    CommandRegistry& registry, TextMeasurer measurer) {
  registry.add("object.align_to_pixel_grid",
    [=] (const auto& document, const auto& nodes) {
      return make_pixel_grid_command(document, nodes, measurer);
    });
  registry.add("object.transform.flip_horizontally",
    [=] (const auto& document, const auto& nodes) {
      return make_flip_command(document, nodes, true, measurer);
    });
  registry.add("object.transform.flip_vertically",
    [=] (const auto& document, const auto& nodes) {
      return make_flip_command(document, nodes, false, measurer);
    });
  registry.add("object.transform.reset_3d",
    [] (const auto& document, const auto& nodes) -> std::unique_ptr<Command> {
      auto targets = get_existing(document, nodes);
      if(std::none_of(targets.begin(), targets.end(), [&] (const auto& id) {
          return static_cast<bool>(document.get(id).m_header.m_transform_3d);
        })) {
        return nullptr;
      }
      return std::make_unique<ResetTransform3dCommand>(std::move(targets));
    });
  registry.add("object.mark_for_export",
    [] (const auto& document, const auto& nodes) -> std::unique_ptr<Command> {
      auto targets = get_existing(document, nodes);
      std::erase_if(targets, [&] (const auto& id) {
        auto& node = document.get(id);
        return node.m_parent.is_nil() && !document.is_on_canvas(id);
      });
      if(targets.empty()) {
        return nullptr;
      }
      auto is_marked = std::any_of(targets.begin(), targets.end(),
        [&] (const auto& id) {
          return !document.get(id).m_header.m_is_marked_for_export;
        });
      return std::make_unique<SetExportMarkCommand>(
        std::move(targets), is_marked);
    });
}

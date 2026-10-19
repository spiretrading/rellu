#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/TransformCommands.hpp"

using namespace Rellu;

namespace {
  void edit_headers(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& nodes, auto edit) {
    auto visited = std::unordered_set<NodeId>();
    for(auto& id : nodes) {
      auto node = document.find(id);
      if(!node || !visited.insert(id).second) {
        continue;
      }
      auto header = node->m_header;
      edit(*node, header);
      if(header != node->m_header) {
        transaction.replace_header(std::move(header));
      }
    }
  }

  void assign(bool& target, const std::optional<bool>& source) {
    if(source) {
      target = *source;
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
  return "Mark for Export";
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
  : m_nodes(std::move(nodes)),
    m_edit(std::move(edit)) {}

std::string SetConstraintsCommand::get_label() const {
  return "Change Constraints";
}

void SetConstraintsCommand::apply(
    const Document& document, Transaction& transaction) {
  edit_headers(document, transaction, m_nodes,
    [&] (const auto&, auto& header) {
      auto& constraints = header.m_constraints;
      assign(constraints.m_is_enabled, m_edit.m_is_enabled);
      assign(constraints.m_is_custom, m_edit.m_is_custom);
      assign(constraints.m_left, m_edit.m_left);
      assign(constraints.m_right, m_edit.m_right);
      assign(constraints.m_top, m_edit.m_top);
      assign(constraints.m_bottom, m_edit.m_bottom);
      assign(constraints.m_width, m_edit.m_width);
      assign(constraints.m_height, m_edit.m_height);
    });
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
  for(auto& id : get_top_level(document, get_existing(document, nodes))) {
    if(document.get(id).m_parent.is_nil() && !document.is_on_canvas(id)) {
      continue;
    }
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
  if(commands.empty()) {
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

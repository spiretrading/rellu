#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/ArtboardPlacement.hpp"
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CopyNames.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Localization/Localization.hpp"

using namespace Rellu;

namespace {
  using Mapping = std::unordered_map<NodeId, NodeId>;

  bool is_tree_root(const Document& document, const NodeId& id) {
    return document.get(id).m_parent.is_nil() && !document.is_on_canvas(id);
  }

  bool is_in_instance(const Document& document, const NodeId& id) {
    for(auto node = document.find(id); node;
        node = document.find(node->m_parent)) {
      if(auto group = node->get_group(); group && group->m_component_instance) {
        return true;
      }
    }
    return false;
  }

  void place_after(Transaction& transaction, std::vector<NodeId>& children,
      const NodeId& parent, const NodeId& id, const NodeId& predecessor) {
    auto position = std::find(children.begin(), children.end(), id);
    auto previous = std::optional<std::size_t>();
    if(position != children.end()) {
      previous = static_cast<std::size_t>(position - children.begin());
      children.erase(position);
    }
    auto index = std::size_t(0);
    if(!predecessor.is_nil()) {
      index = static_cast<std::size_t>(std::find(
        children.begin(), children.end(), predecessor) - children.begin()) + 1;
    }
    children.insert(children.begin() + index, id);
    if(previous != index) {
      transaction.move(id, parent, index);
    }
  }

  double get_reparented_rotation(const Document& document, const NodeId& id) {
    auto& node = document.get(id);
    auto rotation = node.m_header.m_rotation;
    if(auto parent = document.find(node.m_parent);
        parent && !parent->get_artboard()) {
      rotation += parent->m_header.m_rotation;
    }
    if(std::abs(rotation) != 360) {
      rotation = std::fmod(rotation, 360);
    }
    return rotation;
  }

  void set_rotation(const Document& document, Transaction& transaction,
      const NodeId& id, double rotation) {
    auto& node = document.get(id);
    if(node.get_artboard() || node.get_reference() ||
        node.m_header.m_transform_3d) {
      return;
    }
    auto header = node.m_header;
    header.m_rotation = fit_rotation(rotation, header.m_transform);
    if(header != node.m_header) {
      transaction.replace_header(std::move(header));
    }
  }

  void reparent(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& nodes, const NodeId& parent,
      std::size_t index, bool is_rotation_carried) {
    auto inverse = invert(get_world_transform(document, parent));
    if(!inverse) {
      throw std::invalid_argument("The container's transform is singular.");
    }
    auto transforms = std::vector<Matrix>();
    transforms.reserve(nodes.size());
    for(auto& id : nodes) {
      transforms.push_back(*inverse * get_world_transform(document, id));
    }
    auto children = document.get_children(parent);
    auto members = std::unordered_set<NodeId>(nodes.begin(), nodes.end());
    auto remaining = std::vector<NodeId>();
    for(auto& child : children) {
      if(!members.contains(child)) {
        remaining.push_back(child);
      }
    }
    auto predecessor = NodeId();
    index = std::min(index, remaining.size());
    if(index != 0) {
      predecessor = remaining[index - 1];
    }
    for(auto i = std::size_t(0); i != nodes.size(); ++i) {
      auto& id = nodes[i];
      auto is_moved =
        is_rotation_carried && document.get(id).m_parent != parent;
      auto rotation = get_reparented_rotation(document, id);
      place_after(transaction, children, parent, id, predecessor);
      predecessor = id;
      if(get_transform(document, id) != transforms[i]) {
        set_transform(document, transaction, id, transforms[i]);
      }
      if(is_moved) {
        set_rotation(document, transaction, id, rotation);
      }
    }
  }

  std::vector<NodeId> arrange(
      const std::vector<NodeId>& children,
      const std::unordered_set<NodeId>& selection, Arrangement arrangement) {
    auto order = children;
    auto is_selected = [&] (const auto& id) {
      return selection.contains(id);
    };
    if(arrangement == Arrangement::BRING_TO_FRONT) {
      std::stable_partition(order.begin(), order.end(),
        [&] (const auto& id) { return !is_selected(id); });
    } else if(arrangement == Arrangement::SEND_TO_BACK) {
      std::stable_partition(order.begin(), order.end(), is_selected);
    } else if(arrangement == Arrangement::BRING_FORWARD) {
      for(auto i = order.size(); i-- > 1;) {
        if(is_selected(order[i - 1]) && !is_selected(order[i])) {
          std::swap(order[i - 1], order[i]);
        }
      }
    } else {
      for(auto i = std::size_t(1); i < order.size(); ++i) {
        if(is_selected(order[i]) && !is_selected(order[i - 1])) {
          std::swap(order[i - 1], order[i]);
        }
      }
    }
    return order;
  }

  void remap(NodeId& id, const Mapping& mapping) {
    if(auto entry = mapping.find(id); entry != mapping.end()) {
      id = entry->second;
    }
  }

  void remap(std::vector<NodeId>& ids, const Mapping& mapping) {
    for(auto& id : ids) {
      remap(id, mapping);
    }
  }

  template<typename V>
  void remap_keys(std::map<NodeId, V>& map, const Mapping& mapping) {
    auto remapped = std::map<NodeId, V>();
    for(auto& entry : map) {
      auto key = entry.first;
      remap(key, mapping);
      remapped.emplace(key, std::move(entry.second));
    }
    map = std::move(remapped);
  }

  void remap(std::map<NodeId, NodeId>& map, const Mapping& mapping) {
    remap_keys(map, mapping);
    for(auto& entry : map) {
      remap(entry.second, mapping);
    }
  }

  void remap(Overrides& overrides, const Mapping& mapping) {
    remap(overrides.m_added, mapping);
    remap(overrides.m_removed, mapping);
    remap(overrides.m_reordered, mapping);
    remap_keys(overrides.m_properties, mapping);
  }

  void remap(Payload& payload, const Mapping& mapping) {
    if(auto reference = std::get_if<Reference>(&payload)) {
      remap(reference->m_source, mapping);
    } else if(auto group = std::get_if<Group>(&payload)) {
      if(group->m_stack) {
        remap_keys(group->m_stack->m_spacing, mapping);
      }
      if(auto& instance = group->m_component_instance) {
        remap(instance->m_sync_map, mapping);
        remap(instance->m_overrides, mapping);
        for(auto& state : instance->m_states) {
          remap(state.m_tree, mapping);
          remap(state.m_sync_map, mapping);
          remap(state.m_overrides, mapping);
        }
      }
    }
  }

  void remap(Action& action, const Mapping& mapping) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      remap(transition->m_destination, mapping);
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      remap(overlay->m_destination, mapping);
    } else if(auto playback = std::get_if<VideoPlayback>(&action)) {
      remap(playback->m_target, mapping);
    } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
      remap(scroll->m_destination, mapping);
    }
  }

  std::vector<NodeId> collect_subtree(const Document& document,
      const NodeId& root, Mapping& mapping, std::vector<NodeId>& trees) {
    auto subtree = std::vector<NodeId>();
    auto pending = std::vector<NodeId>();
    pending.push_back(root);
    while(!pending.empty()) {
      auto id = pending.back();
      pending.pop_back();
      if(!mapping.emplace(id, generate_node_id()).second) {
        continue;
      }
      subtree.push_back(id);
      auto& node = document.get(id);
      if(auto group = node.get_group(); group && group->m_component_instance) {
        for(auto& state : group->m_component_instance->m_states) {
          if(document.find(state.m_tree)) {
            trees.push_back(state.m_tree);
          }
        }
      }
      pending.insert(
        pending.end(), node.m_children.rbegin(), node.m_children.rend());
    }
    return subtree;
  }

  std::pair<NodeHeader, Payload> make_copy(
      const Document& document, const NodeId& id, const Mapping& mapping) {
    auto& node = document.get(id);
    auto header = node.m_header;
    header.m_id = mapping.at(id);
    auto payload = node.m_payload;
    remap(payload, mapping);
    return std::pair(std::move(header), std::move(payload));
  }

  void insert_descendants(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& subtree, const Mapping& mapping) {
    for(auto i = std::size_t(1); i < subtree.size(); ++i) {
      auto copy = make_copy(document, subtree[i], mapping);
      transaction.append(mapping.at(document.get(subtree[i]).m_parent),
        std::move(copy.first), std::move(copy.second));
    }
  }
}

ArrangeCommand::ArrangeCommand(
  std::vector<NodeId> nodes, Arrangement arrangement) noexcept
  : m_nodes(std::move(nodes)),
    m_arrangement(arrangement) {}

std::string ArrangeCommand::get_label() const {
  if(m_arrangement == Arrangement::BRING_FORWARD) {
    return "Bring Forward";
  } else if(m_arrangement == Arrangement::SEND_BACKWARD) {
    return "Send Backward";
  } else if(m_arrangement == Arrangement::BRING_TO_FRONT) {
    return "Bring to Front";
  }
  return "Send to Back";
}

void ArrangeCommand::apply(const Document& document, Transaction& transaction) {
  auto selections = std::map<NodeId, std::unordered_set<NodeId>>();
  for(auto& id : m_nodes) {
    if(document.find(id) && !is_tree_root(document, id)) {
      selections[document.get(id).m_parent].insert(id);
    }
  }
  for(auto& selection : selections) {
    auto& parent = selection.first;
    auto children = document.get_children(parent);
    auto order = arrange(children, selection.second, m_arrangement);
    if(order == children) {
      continue;
    }
    auto predecessor = NodeId();
    for(auto& id : order) {
      if(selection.second.contains(id)) {
        place_after(transaction, children, parent, id, predecessor);
      }
      predecessor = id;
    }
  }
}

ReparentCommand::ReparentCommand(std::vector<NodeId> nodes,
  const NodeId& parent, std::size_t index) noexcept
  : m_nodes(std::move(nodes)),
    m_parent(parent),
    m_index(index) {}

std::string ReparentCommand::get_label() const {
  return "Move";
}

void ReparentCommand::apply(
    const Document& document, Transaction& transaction) {
  if(!m_parent.is_nil() && !document.find(m_parent)) {
    throw std::invalid_argument("Unknown container.");
  }
  auto nodes = get_top_level(document, m_nodes);
  sort_by_z_order(document, nodes);
  reparent(document, transaction, nodes, m_parent, m_index, true);
}

GroupCommand::GroupCommand(std::vector<NodeId> nodes, TextMeasurer measurer)
  : GroupCommand(std::move(nodes), std::string(), std::move(measurer)) {}

GroupCommand::GroupCommand(std::vector<NodeId> nodes, std::string name,
  TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_name(std::move(name)),
    m_measurer(std::move(measurer)) {}

const NodeId& GroupCommand::get_group() const {
  return m_group;
}

std::string GroupCommand::get_label() const {
  return "Group";
}

void GroupCommand::apply(const Document& document, Transaction& transaction) {
  auto nodes = get_top_level(document, m_nodes);
  if(nodes.empty()) {
    throw std::invalid_argument("There is nothing to group.");
  }
  for(auto& id : nodes) {
    if(document.get(id).get_artboard()) {
      throw std::invalid_argument("An artboard can not be grouped.");
    } else if(is_tree_root(document, id)) {
      throw std::invalid_argument("A component tree can not be grouped.");
    }
  }
  sort_by_z_order(document, nodes);
  auto& topmost = nodes.back();
  auto parent = document.get(topmost).m_parent;
  auto index = get_index(document, topmost) + 1;
  auto inverse = invert(get_world_transform(document, parent));
  if(!inverse) {
    throw std::invalid_argument("The container's transform is singular.");
  }
  auto origin = std::optional<Point>();
  for(auto& id : nodes) {
    if(auto bounds = get_bounds(document, id,
        *inverse * get_world_transform(document, id), BoundsType::GEOMETRY,
        m_measurer)) {
      if(origin) {
        origin = Point(std::min(origin->m_x, bounds->m_x),
          std::min(origin->m_y, bounds->m_y));
      } else {
        origin = bounds->get_top_left();
      }
    }
  }
  auto header = NodeHeader();
  header.m_name = m_name;
  if(header.m_name.empty()) {
    header.m_name = make_node_name(
      document, std::string(localize("name", "Group")) + ' ');
  }
  if(origin) {
    header.m_transform = make_translation(origin->m_x, origin->m_y);
  }
  m_group = transaction.insert(parent, index, std::move(header), Group());
  reparent(document, transaction, nodes, m_group, 0, false);
}

std::optional<std::vector<NodeId>> GroupCommand::get_selection() const {
  return std::vector{m_group};
}

bool Rellu::is_ungroupable(const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node) {
    return false;
  }
  auto group = node->get_group();
  return group && !group->m_mask && !group->m_scroll_group &&
    !group->m_component_instance && !is_tree_root(document, id);
}

UngroupCommand::UngroupCommand(std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

std::string UngroupCommand::get_label() const {
  return "Ungroup";
}

void UngroupCommand::apply(const Document& document, Transaction& transaction) {
  m_selection.clear();
  auto unchanged = std::vector<NodeId>();
  for(auto& id : get_top_level(document, m_nodes)) {
    if(!is_ungroupable(document, id)) {
      unchanged.push_back(id);
      continue;
    }
    auto& group = document.get(id);
    auto header = group.m_header;
    auto children = group.m_children;
    auto parent = group.m_parent;
    auto index = get_index(document, id);
    for(auto i = std::size_t(0); i != children.size(); ++i) {
      auto& child = children[i];
      auto transform = get_transform(document, child);
      auto rotation = get_reparented_rotation(document, child);
      transaction.move(child, parent, index + i);
      set_transform(
        document, transaction, child, header.m_transform * transform);
      set_rotation(document, transaction, child, rotation);
      auto child_header = document.get(child).m_header;
      child_header.m_opacity *= header.m_opacity;
      child_header.m_is_visible =
        child_header.m_is_visible && header.m_is_visible;
      child_header.m_is_locked = child_header.m_is_locked || header.m_is_locked;
      if(child_header != document.get(child).m_header) {
        transaction.replace_header(std::move(child_header));
      }
      m_selection.push_back(child);
    }
    transaction.remove(id);
  }
  m_selection.insert(m_selection.end(), unchanged.begin(), unchanged.end());
}

std::optional<std::vector<NodeId>> UngroupCommand::get_selection() const {
  return m_selection;
}

DeleteCommand::DeleteCommand(std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

std::string DeleteCommand::get_label() const {
  return "Delete";
}

void DeleteCommand::apply(const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    transaction.remove(id);
  }
}

std::optional<std::vector<NodeId>> DeleteCommand::get_selection() const {
  return std::vector<NodeId>();
}

DuplicateCommand::DuplicateCommand(std::vector<NodeId> nodes)
  : DuplicateCommand(std::move(nodes), Point(0, 0)) {}

DuplicateCommand::DuplicateCommand(std::vector<NodeId> nodes, Point offset)
  : DuplicateCommand(std::move(nodes), offset, DuplicateMode::DESIGN) {}

DuplicateCommand::DuplicateCommand(
  std::vector<NodeId> nodes, Point offset, DuplicateMode mode)
  : m_nodes(std::move(nodes)),
    m_offset(offset),
    m_mode(mode) {}

DuplicateCommand::DuplicateCommand(
  std::vector<NodeId> nodes, const Rect& view, DuplicateMode mode)
  : m_nodes(std::move(nodes)),
    m_offset(0, 0),
    m_mode(mode),
    m_view(view) {}

const std::vector<NodeId>& DuplicateCommand::get_copies() const {
  return m_copies;
}

const std::unordered_map<NodeId, NodeId>&
    DuplicateCommand::get_mapping() const {
  return m_mapping;
}

std::string DuplicateCommand::get_label() const {
  return "Duplicate";
}

void DuplicateCommand::apply(
    const Document& document, Transaction& transaction) {
  m_copies.clear();
  m_mapping.clear();
  auto roots = get_top_level(document, m_nodes);
  std::erase_if(roots, [&] (const auto& id) {
    return is_tree_root(document, id);
  });
  sort_by_z_order(document, roots);
  auto subtrees = std::vector<std::vector<NodeId>>();
  auto trees = std::vector<NodeId>();
  for(auto& root : roots) {
    subtrees.push_back(collect_subtree(document, root, m_mapping, trees));
  }
  auto tree_subtrees = std::vector<std::vector<NodeId>>();
  for(auto i = std::size_t(0); i != trees.size(); ++i) {
    auto tree = trees[i];
    if(!m_mapping.contains(tree)) {
      tree_subtrees.push_back(
        collect_subtree(document, tree, m_mapping, trees));
    }
  }
  auto is_placed = m_view && !roots.empty() &&
    std::all_of(roots.begin(), roots.end(), [&] (const auto& id) {
      return document.get(id).get_artboard();
    });
  auto offset = m_offset;
  if(is_placed) {
    auto bounds = Rect();
    for(auto& root : roots) {
      bounds = unite(bounds, document.get(root).get_artboard()->m_frame);
    }
    auto placed = place_artboards(document, m_nodes, *m_view, bounds);
    offset = Point(placed.m_x - bounds.m_x, placed.m_y - bounds.m_y);
  }
  for(auto& subtree : subtrees) {
    auto& root = subtree.front();
    auto parent = document.get(root).m_parent;
    auto index = get_index(document, root) + 1;
    if(is_placed) {
      index = get_artboard_index(document);
    }
    auto copy = make_copy(document, root, m_mapping);
    m_copies.push_back(transaction.insert(
      parent, index, std::move(copy.first), std::move(copy.second)));
    insert_descendants(document, transaction, subtree, m_mapping);
  }
  for(auto& subtree : tree_subtrees) {
    auto copy = make_copy(document, subtree.front(), m_mapping);
    transaction.insert_root(std::move(copy.first), std::move(copy.second));
    insert_descendants(document, transaction, subtree, m_mapping);
  }
  auto interactions = document.get_prototype().m_interactions;
  for(auto& interaction : interactions) {
    if(m_mapping.contains(interaction.m_owner) &&
        (m_mode != DuplicateMode::DESIGN ||
          is_in_instance(document, interaction.m_owner))) {
      interaction.m_id = NodeId();
      remap(interaction.m_owner, m_mapping);
      remap(interaction.m_action, m_mapping);
      transaction.add_interaction(std::move(interaction));
    }
  }
  for(auto& copy : m_copies) {
    translate_node(document, transaction, copy, offset);
  }
  if(m_mode == DuplicateMode::CLONE) {
    return;
  }
  for(auto& copy : m_copies) {
    if(!is_placed) {
      name_copy(document, transaction, copy, false);
    }
    auto& node = document.get(copy);
    if(auto artboard = node.get_artboard()) {
      if(!artboard->m_layout_grid && m_mode == DuplicateMode::DESIGN) {
        auto payload = *artboard;
        payload.m_layout_grid = make_initial_grid(
          payload, document.get_resources().m_grid_defaults);
        transaction.replace_payload(copy, std::move(payload));
      }
      auto header = document.get(copy).m_header;
      header.m_name = make_artboard_name(document, header.m_name, false);
      transaction.replace_header(std::move(header));
    }
  }
}

std::optional<std::vector<NodeId>> DuplicateCommand::get_selection() const {
  return m_copies;
}

#include <algorithm>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"

using namespace Rellu;

namespace {
  using Mapping = std::unordered_map<NodeId, NodeId>;

  bool is_tree_root(const Document& document, const NodeId& id) {
    return document.get(id).m_parent.is_nil() && !document.is_on_canvas(id);
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

  void reparent(const Document& document, Transaction& transaction,
      const std::vector<NodeId>& nodes, const NodeId& parent,
      std::size_t index) {
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
      place_after(transaction, children, parent, id, predecessor);
      predecessor = id;
      if(get_transform(document, id) != transforms[i]) {
        set_transform(document, transaction, id, transforms[i]);
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
  reparent(document, transaction, nodes, m_parent, m_index);
}

GroupCommand::GroupCommand(std::vector<NodeId> nodes)
  : GroupCommand(std::move(nodes), "Group") {}

GroupCommand::GroupCommand(
  std::vector<NodeId> nodes, std::string name) noexcept
  : m_nodes(std::move(nodes)),
    m_name(std::move(name)) {}

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
    if(auto bounds = get_bounds(
        document, id, *inverse * get_world_transform(document, id))) {
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
  if(origin) {
    header.m_transform = make_translation(origin->m_x, origin->m_y);
  }
  m_group = transaction.insert(parent, index, std::move(header), Group());
  reparent(document, transaction, nodes, m_group, 0);
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
      transaction.move(child, parent, index + i);
      set_transform(
        document, transaction, child, header.m_transform * transform);
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
  : m_nodes(std::move(nodes)),
    m_offset(offset) {}

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
  for(auto& subtree : subtrees) {
    auto& root = subtree.front();
    auto parent = document.get(root).m_parent;
    auto index = get_index(document, root) + 1;
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
    if(m_mapping.contains(interaction.m_owner)) {
      interaction.m_id = NodeId();
      remap(interaction.m_owner, m_mapping);
      remap(interaction.m_action, m_mapping);
      transaction.add_interaction(std::move(interaction));
    }
  }
  for(auto& copy : m_copies) {
    translate_node(document, transaction, copy, m_offset);
  }
}

std::optional<std::vector<NodeId>> DuplicateCommand::get_selection() const {
  return m_copies;
}

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"

using namespace Rellu;

namespace {
  constexpr auto STATE_DURATION = 0.3;

  bool is_tree_root(const Document& document, const NodeId& id) {
    return document.get(id).m_parent.is_nil() && !document.is_on_canvas(id);
  }

  bool is_plain_group(const Node& node) {
    auto group = node.get_group();
    return group && !group->m_mask && !group->m_repeat_grid &&
      !group->m_scroll_group && !group->m_component_instance;
  }

  bool is_inside_instance(const Document& document, const NodeId& id) {
    return !find_instance(document, document.get(id).m_parent).is_nil();
  }

  std::vector<NodeId> get_instances(
      const Document& document, const std::vector<NodeId>& targets) {
    auto instances = std::vector<NodeId>();
    for(auto& id : get_top_level(document, targets)) {
      if(auto node = document.find(id); node && get_instance_role(*node)) {
        instances.push_back(id);
      }
    }
    return instances;
  }

  const Component& get_component(
      const Document& document, const ComponentId& id) {
    auto& components = document.get_resources().m_components;
    auto entry = components.find(id);
    if(entry == components.end()) {
      throw std::invalid_argument("Unknown component.");
    }
    return entry->second;
  }

  const ComponentState* find_state(
      const Component& component, const StateId& id) {
    for(auto& state : component.m_states) {
      if(state.m_id == id) {
        return &state;
      }
    }
    return nullptr;
  }

  void collect(const Document& document, const NodeId& id,
      std::vector<NodeId>& nodes) {
    nodes.push_back(id);
    for(auto& child : document.get(id).m_children) {
      collect(document, child, nodes);
    }
  }

  NodeId find_master(const Component& component, const NodeId& source) {
    for(auto& state : component.m_states) {
      if(auto entry = state.m_sync_map.find(source);
          entry != state.m_sync_map.end()) {
        return entry->second;
      }
    }
    return source;
  }

  NodeId get_mirrored(const Document& document, const ComponentInstance& role,
      const NodeId& id) {
    if(auto entry = role.m_sync_map.find(id); entry != role.m_sync_map.end()) {
      return entry->second;
    }
    if(auto node = document.find(id)) {
      if(auto reference = node->get_reference()) {
        return reference->m_source;
      }
    }
    return NodeId();
  }

  std::optional<Node> resolve_node(const Document& document,
      const NodeId& id) {
    auto node = document.find(id);
    if(!node) {
      return std::nullopt;
    }
    if(!node->get_reference()) {
      return *node;
    }
    auto resolution = resolve_reference(document.get_nodes(), id);
    if(!resolution) {
      return std::nullopt;
    }
    auto source = *document.find(resolution->m_source);
    source.m_header.m_transform = resolution->m_transform;
    return source;
  }

  std::vector<NodeId> get_rendered_children(
      const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node) {
      return {};
    }
    if(auto reference = node->get_reference();
        reference && !reference->m_has_children) {
      if(auto resolution = resolve_reference(document.get_nodes(), id)) {
        if(auto source = document.find(resolution->m_child_source)) {
          return source->m_children;
        }
      }
      return {};
    }
    return node->m_children;
  }

  class Rebuild {
    public:
      Rebuild(const Document& document, Transaction& transaction,
          const NodeId& instance)
          : m_document(&document),
            m_transaction(&transaction),
            m_instance(instance),
            m_role(*get_instance_role(document.get(instance))),
            m_component(&get_component(document, m_role.m_component)) {}

      void run(const StateId& state, bool keeps_overrides) {
        auto new_state = state;
        if(new_state == m_component->m_default_state) {
          new_state = StateId();
        }
        auto role = m_role;
        role.m_state = new_state;
        auto tree = get_source_tree(*m_document, role);
        if(tree.is_nil()) {
          throw std::invalid_argument("The component has no master.");
        }
        auto new_component_state = new_state.is_nil() ? nullptr :
          find_state(*m_component, new_state);
        if(new_component_state && new_component_state->m_tree != tree) {
          new_component_state = nullptr;
        }
        auto nodes = std::vector<NodeId>();
        collect(*m_document, m_instance, nodes);
        nodes.erase(nodes.begin());
        auto added = std::unordered_set<NodeId>();
        if(keeps_overrides) {
          added.insert(m_role.m_overrides.m_added.begin(),
            m_role.m_overrides.m_added.end());
        }
        for(auto& id : nodes) {
          auto mirrored = get_mirrored(*m_document, m_role, id);
          if(mirrored.is_nil()) {
            continue;
          }
          auto master = to_master(mirrored);
          m_old.try_emplace(master, id);
          if(keeps_overrides) {
            if(auto names = m_role.m_overrides.m_properties.find(id);
                names != m_role.m_overrides.m_properties.end()) {
              if(auto resolved = resolve_node(*m_document, id)) {
                m_values.emplace(id, std::pair(*resolved, names->second));
              }
              mark_overridden(id);
            }
          }
        }
        for(auto& removed : m_role.m_overrides.m_removed) {
          if(keeps_overrides) {
            m_removed.insert(to_master(removed));
          }
        }
        auto root_names = std::vector<std::string>();
        if(keeps_overrides) {
          if(auto names = m_role.m_overrides.m_properties.find(m_instance);
              names != m_role.m_overrides.m_properties.end()) {
            root_names = names->second;
          }
        }
        auto subtree = std::unordered_set<NodeId>(nodes.begin(), nodes.end());
        for(auto& interaction : m_document->get_prototype().m_interactions) {
          if(subtree.contains(interaction.m_owner)) {
            m_interactions.push_back(interaction);
          }
        }
        for(auto& child : std::vector<NodeId>(
            m_document->get(m_instance).m_children)) {
          if(!added.contains(child)) {
            m_transaction->remove(child);
          }
        }
        m_role = role;
        m_role.m_sync_map.clear();
        m_role.m_overrides.m_properties.clear();
        m_role.m_overrides.m_removed.clear();
        m_role.m_overrides.m_reordered.clear();
        if(!keeps_overrides) {
          m_role.m_overrides = Overrides();
        } else {
          std::erase_if(m_role.m_overrides.m_added, [&] (const auto& id) {
            return !m_document->find(id);
          });
        }
        m_state = new_component_state;
        auto index = std::size_t(0);
        for(auto& child : m_document->get(tree).m_children) {
          if(build(child, m_instance, index)) {
            ++index;
          }
        }
        if(keeps_overrides) {
          for(auto& master : m_removed) {
            m_role.m_overrides.m_removed.push_back(to_source(master));
          }
          if(!root_names.empty()) {
            m_role.m_overrides.m_properties.emplace(m_instance, root_names);
          }
        }
        if(auto values = resolve_node(*m_document, tree)) {
          auto root = m_document->get(m_instance);
          if(update_properties(root, *values, true, root_names)) {
            m_transaction->replace_header(root.m_header);
          }
        }
        auto group = *m_document->get(m_instance).get_group();
        group.m_component_instance = m_role;
        m_transaction->replace_payload(m_instance, std::move(group));
        auto existing = std::unordered_set<InteractionId>();
        for(auto& interaction : m_document->get_prototype().m_interactions) {
          existing.insert(interaction.m_id);
        }
        for(auto& interaction : m_interactions) {
          if(!existing.contains(interaction.m_id) &&
              m_document->find(interaction.m_owner)) {
            m_transaction->add_interaction(interaction);
          }
        }
      }

    private:
      const Document* m_document;
      Transaction* m_transaction;
      NodeId m_instance;
      ComponentInstance m_role;
      const Component* m_component;
      const ComponentState* m_state = nullptr;
      std::unordered_map<NodeId, NodeId> m_old;
      std::unordered_map<NodeId, std::pair<Node, std::vector<std::string>>>
        m_values;
      std::unordered_set<NodeId> m_overridden;
      std::unordered_set<NodeId> m_removed;
      std::vector<Interaction> m_interactions;

      void mark_overridden(const NodeId& id) {
        for(auto current = id; !current.is_nil() && current != m_instance;
            current = m_document->get(current).m_parent) {
          if(!m_overridden.insert(current).second) {
            return;
          }
        }
      }

      NodeId to_master(const NodeId& source) const {
        if(m_state) {
          if(auto entry = m_state->m_sync_map.find(source);
              entry != m_state->m_sync_map.end()) {
            return entry->second;
          }
        }
        return find_master(*m_component, source);
      }

      NodeId to_source(const NodeId& master) const {
        if(m_state) {
          for(auto& entry : m_state->m_sync_map) {
            if(entry.second == master) {
              return entry.first;
            }
          }
        }
        return master;
      }

      bool build(const NodeId& source, const NodeId& parent,
          std::size_t index) {
        auto master = to_master(source);
        if(m_removed.contains(master)) {
          return false;
        }
        auto id = generate_node_id();
        auto previous = NodeId();
        if(auto old = m_old.find(master); old != m_old.end() &&
            !m_document->find(old->second)) {
          id = old->second;
          previous = old->second;
        }
        auto& parent_payload = m_document->get(parent).m_payload;
        auto is_full =
          (!previous.is_nil() && m_overridden.contains(previous)) ||
          !can_contain(parent_payload, NodeKind::REFERENCE);
        if(!is_full) {
          auto header = NodeHeader();
          header.m_id = id;
          m_transaction->insert(parent, index, std::move(header),
            Reference(source));
          m_role.m_sync_map.emplace(id, source);
          return true;
        }
        auto values = resolve_node(*m_document, source);
        if(!values) {
          return false;
        }
        auto node = Node(values->m_header, values->m_payload, NodeId(),
          std::vector<NodeId>());
        node.m_header.m_id = id;
        if(auto old = m_values.find(previous); old != m_values.end()) {
          auto& names = old->second.second;
          copy_properties(node, old->second.first, names);
          m_role.m_overrides.m_properties.emplace(id, names);
        }
        auto nested = std::optional<ComponentInstance>();
        if(auto group = std::get_if<Group>(&node.m_payload);
            group && group->m_component_instance) {
          nested = *group->m_component_instance;
          group->m_component_instance->m_states.clear();
          group->m_component_instance->m_is_main = false;
          group->m_component_instance->m_sync_map.clear();
          group->m_component_instance->m_overrides = Overrides();
        }
        m_transaction->insert(parent, index, std::move(node.m_header),
          std::move(node.m_payload));
        m_role.m_sync_map.emplace(id, source);
        auto child_index = std::size_t(0);
        auto sync_map = std::map<NodeId, NodeId>();
        for(auto& child : get_rendered_children(*m_document, source)) {
          if(build(child, id, child_index)) {
            auto& children = m_document->get(id).m_children;
            auto built = children[child_index];
            auto mirrored = child;
            if(nested) {
              if(auto entry = nested->m_sync_map.find(child);
                  entry != nested->m_sync_map.end()) {
                mirrored = entry->second;
              }
            }
            sync_map.emplace(built, mirrored);
            ++child_index;
          }
        }
        if(nested) {
          auto group = *m_document->get(id).get_group();
          group.m_component_instance->m_sync_map = std::move(sync_map);
          m_transaction->replace_payload(id, std::move(group));
        }
        return true;
      }
  };

  void rebuild(const Document& document, Transaction& transaction,
      const NodeId& instance, const StateId& state, bool keeps_overrides) {
    auto rebuild = Rebuild(document, transaction, instance);
    rebuild.run(state, keeps_overrides);
  }

  void materialize_own(const Document& document, Transaction& transaction,
      const NodeId& instance) {
    auto pending = std::deque<NodeId>(document.get(instance).m_children.begin(),
      document.get(instance).m_children.end());
    while(!pending.empty()) {
      auto id = pending.front();
      pending.pop_front();
      auto node = document.find(id);
      if(!node) {
        continue;
      }
      if(node->get_reference() &&
          find_instance(document, node->m_parent) == instance) {
        materialize_reference(document, transaction, id);
        node = document.find(id);
        if(!node) {
          continue;
        }
      }
      if(!get_instance_role(*node)) {
        pending.insert(
          pending.end(), node->m_children.begin(), node->m_children.end());
      }
    }
  }

  void check_editable(const Document& document, const ComponentId& id) {
    get_component(document, id);
    if(is_linked(document, id)) {
      throw std::invalid_argument("A linked component can not be edited.");
    }
  }

  void add_state_interactions(Transaction& transaction,
      const Component& component, const ComponentState& state,
      StateKind kind) {
    auto animation =
      Animation(Transition::AUTO_ANIMATE, Easing::EASE_OUT, STATE_DURATION);
    if(kind == StateKind::HOVER) {
      transaction.add_interaction(Interaction(InteractionId(),
        component.m_master, HoverTrigger(),
        StateTransition(state.m_id, animation)));
    } else if(kind == StateKind::TOGGLE) {
      transaction.add_interaction(Interaction(InteractionId(),
        component.m_master, TapTrigger(),
        StateTransition(state.m_id, animation)));
      transaction.add_interaction(Interaction(InteractionId(), state.m_tree,
        TapTrigger(), StateTransition(component.m_default_state, animation)));
    }
  }
}

MakeComponentCommand::MakeComponentCommand(std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

const ComponentId& MakeComponentCommand::get_component() const {
  return m_component;
}

const NodeId& MakeComponentCommand::get_main() const {
  return m_main;
}

std::string MakeComponentCommand::get_label() const {
  return "Make Component";
}

void MakeComponentCommand::apply(
    const Document& document, Transaction& transaction) {
  auto nodes = get_top_level(document, m_nodes);
  std::erase_if(nodes, [&] (const auto& id) {
    return is_tree_root(document, id);
  });
  if(nodes.empty()) {
    throw std::invalid_argument("There is nothing to make a component of.");
  }
  for(auto& id : nodes) {
    if(document.get(id).get_artboard()) {
      throw std::invalid_argument("An artboard can not be a component.");
    } else if(is_inside_instance(document, id)) {
      throw std::invalid_argument(
        "A component can not be made inside a component instance.");
    }
  }
  if(nodes.size() == 1 && is_plain_group(document.get(nodes.front()))) {
    m_main = nodes.front();
  } else {
    auto group =
      GroupCommand(std::move(nodes), make_node_name(document, "Component "));
    group.apply(document, transaction);
    m_main = group.get_group();
  }
  auto mapping = std::unordered_map<NodeId, NodeId>();
  auto master = clone_tree(document, transaction, m_main, mapping);
  auto master_header = document.get(master).m_header;
  master_header.m_transform = Matrix();
  master_header.m_transform_3d = std::nullopt;
  master_header.m_opaque = OpaqueData();
  transaction.replace_header(std::move(master_header));
  auto component = Component();
  component.m_name = document.get(m_main).m_header.m_name;
  component.m_master = master;
  m_component = transaction.add_component(std::move(component));
  auto role = ComponentInstance();
  role.m_component = m_component;
  if(auto bounds = get_local_bounds(document, m_main)) {
    role.m_size = Size(bounds->m_width, bounds->m_height);
  }
  for(auto& entry : mapping) {
    if(entry.first != m_main) {
      role.m_sync_map.emplace(entry.first, entry.second);
    }
  }
  role.m_is_main = true;
  auto group = *document.get(m_main).get_group();
  group.m_component_instance = std::move(role);
  transaction.replace_payload(m_main, std::move(group));
}

std::optional<std::vector<NodeId>> MakeComponentCommand::get_selection() const {
  return std::vector{m_main};
}

UngroupComponentCommand::UngroupComponentCommand(
  std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

std::string UngroupComponentCommand::get_label() const {
  return "Ungroup Component";
}

void UngroupComponentCommand::apply(
    const Document& document, Transaction& transaction) {
  m_selection.clear();
  auto instances = get_instances(document, m_nodes);
  if(instances.empty()) {
    throw std::invalid_argument("There is no component instance to ungroup.");
  }
  for(auto& id : instances) {
    materialize_own(document, transaction, id);
    auto header = document.get(id).m_header;
    header.m_opaque = OpaqueData();
    transaction.replace_header(std::move(header));
    auto group = *document.get(id).get_group();
    group.m_component_instance = std::nullopt;
    transaction.replace_payload(id, std::move(group));
  }
  auto ungroup = UngroupCommand(instances);
  ungroup.apply(document, transaction);
  if(auto selection = ungroup.get_selection()) {
    m_selection = std::move(*selection);
  }
}

std::optional<std::vector<NodeId>>
    UngroupComponentCommand::get_selection() const {
  return m_selection;
}

ResetOverridesCommand::ResetOverridesCommand(
  std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

std::string ResetOverridesCommand::get_label() const {
  return "Reset to Main State";
}

void ResetOverridesCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_instances(document, m_nodes)) {
    auto role = *get_instance_role(document.get(id));
    if(role.m_is_main) {
      continue;
    }
    rebuild(document, transaction, id, role.m_state, false);
  }
}

SetInstanceStateCommand::SetInstanceStateCommand(
  std::vector<NodeId> nodes, const StateId& state) noexcept
  : m_nodes(std::move(nodes)),
    m_state(state) {}

std::string SetInstanceStateCommand::get_label() const {
  return "Change State";
}

void SetInstanceStateCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_instances(document, m_nodes)) {
    auto role = *get_instance_role(document.get(id));
    auto& component = get_component(document, role.m_component);
    auto state = m_state;
    if(state == component.m_default_state) {
      state = StateId();
    }
    if(!state.is_nil() && !find_state(component, state)) {
      throw std::invalid_argument("Unknown state.");
    }
    if(role.m_state == state ||
        role.m_state == component.m_default_state && state.is_nil()) {
      continue;
    }
    rebuild(document, transaction, id, state, true);
  }
}

SwapComponentCommand::SwapComponentCommand(
  std::vector<NodeId> nodes, const ComponentId& component) noexcept
  : m_nodes(std::move(nodes)),
    m_component(component) {}

std::string SwapComponentCommand::get_label() const {
  return "Swap Component";
}

void SwapComponentCommand::apply(
    const Document& document, Transaction& transaction) {
  get_component(document, m_component);
  m_selection.clear();
  for(auto& id : get_instances(document, m_nodes)) {
    auto& node = document.get(id);
    auto role = *get_instance_role(node);
    if(role.m_is_main || role.m_component == m_component ||
        is_inside_instance(document, id)) {
      m_selection.push_back(id);
      continue;
    }
    auto parent = node.m_parent;
    auto transform = node.m_header.m_transform;
    auto index = get_index(document, id);
    transaction.remove(id);
    m_selection.push_back(insert_instance(
      document, transaction, m_component, parent, index, transform));
  }
}

std::optional<std::vector<NodeId>> SwapComponentCommand::get_selection() const {
  return m_selection;
}

InsertInstanceCommand::InsertInstanceCommand(const ComponentId& component,
  const NodeId& parent, const Matrix& transform) noexcept
  : m_component(component),
    m_parent(parent),
    m_transform(transform) {}

const NodeId& InsertInstanceCommand::get_instance() const {
  return m_instance;
}

std::string InsertInstanceCommand::get_label() const {
  return "Add Component";
}

void InsertInstanceCommand::apply(
    const Document& document, Transaction& transaction) {
  auto count = document.get_children(m_parent).size();
  m_instance = insert_instance(
    document, transaction, m_component, m_parent, count, m_transform);
}

std::optional<std::vector<NodeId>>
    InsertInstanceCommand::get_selection() const {
  return std::vector{m_instance};
}

RestoreMainComponentCommand::RestoreMainComponentCommand(
  const ComponentId& component, const Matrix& transform) noexcept
  : m_component(component),
    m_transform(transform) {}

const NodeId& RestoreMainComponentCommand::get_main() const {
  return m_main;
}

std::string RestoreMainComponentCommand::get_label() const {
  return "Edit Main Component";
}

void RestoreMainComponentCommand::apply(
    const Document& document, Transaction& transaction) {
  check_editable(document, m_component);
  if(!find_main_component(document, m_component).is_nil()) {
    throw std::invalid_argument("The component has a main component.");
  }
  m_main = insert_instance(document, transaction, m_component, NodeId(),
    document.get_canvas().size(), m_transform);
  auto group = *document.get(m_main).get_group();
  group.m_component_instance->m_is_main = true;
  transaction.replace_payload(m_main, std::move(group));
}

std::optional<std::vector<NodeId>>
    RestoreMainComponentCommand::get_selection() const {
  return std::vector{m_main};
}

MakeEditableCommand::MakeEditableCommand(
  std::vector<NodeId> nodes, std::string label) noexcept
  : m_nodes(std::move(nodes)),
    m_label(std::move(label)) {}

std::string MakeEditableCommand::get_label() const {
  return m_label;
}

void MakeEditableCommand::apply(
    const Document& document, Transaction& transaction) {
  materialize_references(document, transaction, m_nodes);
}

AddStateCommand::AddStateCommand(const ComponentId& component,
  std::string name, StateKind kind) noexcept
  : m_component(component),
    m_name(std::move(name)),
    m_kind(kind) {}

const StateId& AddStateCommand::get_state() const {
  return m_state;
}

std::string AddStateCommand::get_label() const {
  return "Add State";
}

void AddStateCommand::apply(
    const Document& document, Transaction& transaction) {
  check_editable(document, m_component);
  auto component = get_component(document, m_component);
  auto mapping = std::unordered_map<NodeId, NodeId>();
  auto tree = clone_tree(document, transaction, component.m_master, mapping);
  auto header = document.get(tree).m_header;
  header.m_name = m_name;
  header.m_opaque = OpaqueData();
  transaction.replace_header(std::move(header));
  auto state = ComponentState();
  state.m_id = tree;
  state.m_name = m_name;
  state.m_tree = tree;
  for(auto& entry : mapping) {
    if(entry.first != component.m_master) {
      state.m_sync_map.emplace(entry.second, entry.first);
    }
  }
  if(component.m_default_state.is_nil()) {
    component.m_default_state = component.m_id;
  }
  component.m_states.push_back(state);
  transaction.replace_component(component);
  m_state = state.m_id;
  add_state_interactions(transaction, component, state, m_kind);
  auto main = find_main_component(document, m_component);
  if(!main.is_nil()) {
    rebuild(document, transaction, main, m_state, false);
  }
}

RemoveStateCommand::RemoveStateCommand(
  const ComponentId& component, const StateId& state) noexcept
  : m_component(component),
    m_state(state) {}

std::string RemoveStateCommand::get_label() const {
  return "Delete State";
}

void RemoveStateCommand::apply(
    const Document& document, Transaction& transaction) {
  check_editable(document, m_component);
  auto component = get_component(document, m_component);
  auto state = find_state(component, m_state);
  if(!state) {
    throw std::invalid_argument("Unknown state.");
  }
  auto tree = state->m_tree;
  for(auto& id : find_instances(document, m_component)) {
    if(get_instance_role(document.get(id))->m_state == m_state) {
      rebuild(document, transaction, id, StateId(), true);
    }
  }
  auto interactions = document.get_prototype().m_interactions;
  for(auto& interaction : interactions) {
    if(auto transition = std::get_if<StateTransition>(&interaction.m_action);
        transition && transition->m_state == m_state) {
      transaction.remove_interaction(interaction.m_id);
    }
  }
  std::erase_if(component.m_states, [&] (const auto& candidate) {
    return candidate.m_id == m_state;
  });
  if(component.m_states.empty()) {
    component.m_default_state = StateId();
  }
  transaction.replace_component(std::move(component));
  if(document.find(tree)) {
    transaction.remove(tree);
  }
}

RenameStateCommand::RenameStateCommand(const ComponentId& component,
  const StateId& state, std::string name) noexcept
  : m_component(component),
    m_state(state),
    m_name(std::move(name)) {}

std::string RenameStateCommand::get_label() const {
  return "Rename State";
}

void RenameStateCommand::apply(
    const Document& document, Transaction& transaction) {
  check_editable(document, m_component);
  auto component = get_component(document, m_component);
  auto is_found = false;
  for(auto& state : component.m_states) {
    if(state.m_id != m_state) {
      continue;
    }
    is_found = true;
    if(state.m_name == m_name) {
      return;
    }
    state.m_name = m_name;
    if(auto tree = document.find(state.m_tree)) {
      auto header = tree->m_header;
      header.m_name = m_name;
      transaction.replace_header(std::move(header));
    }
  }
  if(!is_found) {
    throw std::invalid_argument("Unknown state.");
  }
  transaction.replace_component(std::move(component));
}

bool RenameStateCommand::merge(const Command& command) {
  auto rename = dynamic_cast<const RenameStateCommand*>(&command);
  if(!rename || rename->m_component != m_component ||
      rename->m_state != m_state) {
    return false;
  }
  m_name = rename->m_name;
  return true;
}

RenameComponentCommand::RenameComponentCommand(
  const ComponentId& component, std::string name) noexcept
  : m_component(component),
    m_name(std::move(name)) {}

std::string RenameComponentCommand::get_label() const {
  return "Rename Component";
}

void RenameComponentCommand::apply(
    const Document& document, Transaction& transaction) {
  check_editable(document, m_component);
  auto component = get_component(document, m_component);
  if(component.m_name == m_name) {
    return;
  }
  component.m_name = m_name;
  auto master = component.m_master;
  transaction.replace_component(std::move(component));
  for(auto& id : {master, find_main_component(document, m_component)}) {
    if(auto node = id.is_nil() ? nullptr : document.find(id)) {
      auto header = node->m_header;
      header.m_name = m_name;
      transaction.replace_header(std::move(header));
    }
  }
}

bool RenameComponentCommand::merge(const Command& command) {
  auto rename = dynamic_cast<const RenameComponentCommand*>(&command);
  if(!rename || rename->m_component != m_component) {
    return false;
  }
  m_name = rename->m_name;
  return true;
}

std::unique_ptr<Command> Rellu::make_ungroup_component(
    const Document& document, const std::vector<NodeId>& targets) {
  auto instances = get_instances(document, targets);
  if(instances.empty()) {
    return nullptr;
  }
  return std::make_unique<UngroupComponentCommand>(std::move(instances));
}

void Rellu::add_component_commands(CommandRegistry& registry) {
  registry.add("object.make_component", [] (const auto& document,
      const auto& targets) -> std::unique_ptr<Command> {
    auto nodes = get_top_level(document, targets);
    if(nodes.empty() || std::any_of(nodes.begin(), nodes.end(),
        [&] (const auto& id) {
          return document.get(id).get_artboard() ||
            is_tree_root(document, id) || is_inside_instance(document, id);
        })) {
      return nullptr;
    }
    return std::make_unique<MakeComponentCommand>(std::move(nodes));
  });
  registry.add("object.ungroup_component", make_ungroup_component);
  registry.add("object.reset_main_state", [] (const auto& document,
      const auto& targets) -> std::unique_ptr<Command> {
    auto instances = get_instances(document, targets);
    std::erase_if(instances, [&] (const auto& id) {
      auto role = get_instance_role(document.get(id));
      return role->m_is_main || role->m_overrides == Overrides();
    });
    if(instances.empty()) {
      return nullptr;
    }
    return std::make_unique<ResetOverridesCommand>(std::move(instances));
  });
}

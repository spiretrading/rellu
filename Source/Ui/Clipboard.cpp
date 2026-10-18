#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/Clipboard.hpp"

using namespace Rellu;

namespace {
  using Mapping = std::unordered_map<NodeId, NodeId>;

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

  bool can_copy(const Document& document, const NodeId& id) {
    auto& node = document.get(id);
    return !node.get_reference() &&
      (!node.m_parent.is_nil() || document.is_on_canvas(id));
  }

  std::vector<NodeId> get_copyable(
      const Document& document, const std::vector<NodeId>& nodes) {
    auto roots = get_top_level(document, nodes);
    std::erase_if(roots, [&] (const auto& id) {
      return !can_copy(document, id);
    });
    return roots;
  }
}

struct Clipboard::Content {
  struct Entry {
    NodeHeader m_header;
    Payload m_payload;
    NodeId m_parent;
  };

  struct Tree {
    NodeId m_container;
    Matrix m_transform;
    std::vector<Entry> m_nodes;
  };

  std::vector<Tree> m_trees;
  std::vector<std::vector<Entry>> m_states;
  std::vector<Interaction> m_interactions;

  static std::vector<Entry> collect(const Document& document,
      const NodeId& root, std::unordered_set<NodeId>& visited,
      std::vector<NodeId>& states) {
    auto entries = std::vector<Entry>();
    auto pending = std::vector<NodeId>();
    pending.push_back(root);
    while(!pending.empty()) {
      auto id = pending.back();
      pending.pop_back();
      if(!visited.insert(id).second) {
        continue;
      }
      auto& node = document.get(id);
      entries.push_back(Entry(node.m_header, node.m_payload, node.m_parent));
      if(auto group = node.get_group(); group && group->m_component_instance) {
        for(auto& state : group->m_component_instance->m_states) {
          if(document.find(state.m_tree)) {
            states.push_back(state.m_tree);
          }
        }
      }
      pending.insert(
        pending.end(), node.m_children.rbegin(), node.m_children.rend());
    }
    return entries;
  }
};

class Clipboard::PasteCommand : public Command {
  public:
    explicit PasteCommand(std::shared_ptr<const Content> content) noexcept
      : m_content(std::move(content)) {}

    std::string get_label() const override {
      return "Paste";
    }

    void apply(const Document& document, Transaction& transaction) override {
      m_copies.clear();
      auto mapping = Mapping();
      for(auto& tree : m_content->m_trees) {
        for(auto& entry : tree.m_nodes) {
          mapping.emplace(entry.m_header.m_id, generate_node_id());
        }
      }
      for(auto& tree : m_content->m_states) {
        for(auto& entry : tree) {
          mapping.emplace(entry.m_header.m_id, generate_node_id());
        }
      }
      for(auto& tree : m_content->m_trees) {
        auto& root = tree.m_nodes.front();
        auto container = tree.m_container;
        auto is_artboard = get_kind(root.m_payload) == NodeKind::ARTBOARD;
        if(is_artboard || (!container.is_nil() && !document.find(container))) {
          container = NodeId();
        }
        auto copy = make_copy(root, mapping);
        if(!is_artboard) {
          if(auto inverse =
              invert(get_world_transform(document, container))) {
            copy.first.m_transform = *inverse * tree.m_transform;
          }
        }
        m_copies.push_back(transaction.append(
          container, std::move(copy.first), std::move(copy.second)));
        insert_descendants(transaction, tree.m_nodes, mapping);
      }
      for(auto& tree : m_content->m_states) {
        auto copy = make_copy(tree.front(), mapping);
        transaction.insert_root(std::move(copy.first), std::move(copy.second));
        insert_descendants(transaction, tree, mapping);
      }
      for(auto interaction : m_content->m_interactions) {
        interaction.m_id = NodeId();
        remap(interaction.m_owner, mapping);
        remap(interaction.m_action, mapping);
        transaction.add_interaction(std::move(interaction));
      }
    }

    std::optional<std::vector<NodeId>> get_selection() const override {
      return m_copies;
    }

  private:
    std::shared_ptr<const Content> m_content;
    std::vector<NodeId> m_copies;

    static std::pair<NodeHeader, Payload> make_copy(
        const Content::Entry& entry, const Mapping& mapping) {
      auto header = entry.m_header;
      header.m_id = mapping.at(entry.m_header.m_id);
      auto payload = entry.m_payload;
      remap(payload, mapping);
      return std::pair(std::move(header), std::move(payload));
    }

    static void insert_descendants(Transaction& transaction,
        const std::vector<Content::Entry>& entries, const Mapping& mapping) {
      for(auto i = std::size_t(1); i < entries.size(); ++i) {
        auto copy = make_copy(entries[i], mapping);
        transaction.append(mapping.at(entries[i].m_parent),
          std::move(copy.first), std::move(copy.second));
      }
    }
};

Clipboard::Clipboard() = default;

Clipboard::~Clipboard() = default;

bool Clipboard::is_empty() const {
  return !m_content;
}

bool Clipboard::is_copyable(
    const Document& document, const std::vector<NodeId>& nodes) {
  return !get_copyable(document, nodes).empty();
}

bool Clipboard::copy(
    const Document& document, const std::vector<NodeId>& nodes) {
  auto roots = get_copyable(document, nodes);
  if(roots.empty()) {
    return false;
  }
  sort_by_z_order(document, roots);
  auto content = std::make_shared<Content>();
  auto visited = std::unordered_set<NodeId>();
  auto states = std::vector<NodeId>();
  for(auto& root : roots) {
    content->m_trees.push_back(Content::Tree(document.get(root).m_parent,
      get_world_transform(document, root),
      Content::collect(document, root, visited, states)));
  }
  for(auto i = std::size_t(0); i != states.size(); ++i) {
    auto state = states[i];
    if(!visited.contains(state)) {
      content->m_states.push_back(
        Content::collect(document, state, visited, states));
    }
  }
  for(auto& interaction : document.get_prototype().m_interactions) {
    if(visited.contains(interaction.m_owner)) {
      content->m_interactions.push_back(interaction);
    }
  }
  m_content = std::move(content);
  return true;
}

void Clipboard::clear() {
  m_content.reset();
}

std::unique_ptr<Command> Clipboard::make_paste_command() const {
  if(!m_content) {
    return nullptr;
  }
  return std::make_unique<PasteCommand>(m_content);
}

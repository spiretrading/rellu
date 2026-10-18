#include <algorithm>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "Rellu/Commands/Hierarchy.hpp"

using namespace Rellu;

namespace {
  struct OrderKey {
    std::size_t m_rank;
    NodeId m_root;
    std::vector<std::size_t> m_path;

    auto operator <=>(const OrderKey&) const = default;
  };

  class IndexCache {
    public:
      explicit IndexCache(const Document& document)
        : m_document(&document) {}

      std::size_t get(const NodeId& parent, const NodeId& child) {
        auto entry = m_indices.find(parent);
        if(entry == m_indices.end()) {
          auto indices = std::unordered_map<NodeId, std::size_t>();
          auto& children = m_document->get_children(parent);
          for(auto i = std::size_t(0); i != children.size(); ++i) {
            indices.emplace(children[i], i);
          }
          entry = m_indices.emplace(parent, std::move(indices)).first;
        }
        auto index = entry->second.find(child);
        if(index == entry->second.end()) {
          return std::numeric_limits<std::size_t>::max();
        }
        return index->second;
      }

    private:
      const Document* m_document;
      std::unordered_map<NodeId, std::unordered_map<NodeId, std::size_t>>
        m_indices;
  };

  OrderKey make_key(
      const Document& document, const NodeId& id, IndexCache& cache) {
    auto key = OrderKey();
    auto current = id;
    while(true) {
      auto& node = document.get(current);
      if(node.m_parent.is_nil()) {
        key.m_rank = cache.get(NodeId(), current);
        if(key.m_rank != std::numeric_limits<std::size_t>::max()) {
          key.m_root = NodeId();
        } else {
          key.m_root = current;
        }
        break;
      }
      key.m_path.push_back(cache.get(node.m_parent, current));
      current = node.m_parent;
    }
    std::reverse(key.m_path.begin(), key.m_path.end());
    return key;
  }
}

bool Rellu::is_ancestor(
    const Document& document, const NodeId& ancestor, const NodeId& node) {
  if(ancestor.is_nil()) {
    return false;
  }
  auto current = document.find(node);
  while(current && !current->m_parent.is_nil()) {
    if(current->m_parent == ancestor) {
      return true;
    }
    current = document.find(current->m_parent);
  }
  return false;
}

NodeId Rellu::find_artboard(const Document& document, const NodeId& id) {
  auto current = document.find(id);
  while(current) {
    if(current->get_artboard()) {
      return current->get_id();
    }
    if(current->m_parent.is_nil()) {
      break;
    }
    current = document.find(current->m_parent);
  }
  return NodeId();
}

std::size_t Rellu::get_index(const Document& document, const NodeId& id) {
  auto& node = document.get(id);
  auto& children = document.get_children(node.m_parent);
  return static_cast<std::size_t>(
    std::find(children.begin(), children.end(), id) - children.begin());
}

std::vector<NodeId> Rellu::get_top_level(
    const Document& document, const std::vector<NodeId>& nodes) {
  auto members = std::unordered_set<NodeId>();
  for(auto& id : nodes) {
    if(document.find(id)) {
      members.insert(id);
    }
  }
  auto top_level = std::vector<NodeId>();
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : nodes) {
    if(!members.contains(id) || !visited.insert(id).second) {
      continue;
    }
    auto is_nested = false;
    auto current = &document.get(id);
    while(!current->m_parent.is_nil()) {
      if(members.contains(current->m_parent)) {
        is_nested = true;
        break;
      }
      current = document.find(current->m_parent);
      if(!current) {
        break;
      }
    }
    if(!is_nested) {
      top_level.push_back(id);
    }
  }
  return top_level;
}

void Rellu::sort_by_z_order(
    const Document& document, std::vector<NodeId>& nodes) {
  auto cache = IndexCache(document);
  auto keys = std::vector<std::pair<OrderKey, NodeId>>();
  keys.reserve(nodes.size());
  for(auto& id : nodes) {
    keys.emplace_back(make_key(document, id, cache), id);
  }
  std::stable_sort(keys.begin(), keys.end(),
    [] (const auto& left, const auto& right) {
      return left.first < right.first;
    });
  for(auto i = std::size_t(0); i != nodes.size(); ++i) {
    nodes[i] = keys[i].second;
  }
}

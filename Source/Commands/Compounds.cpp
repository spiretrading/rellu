#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include "Rellu/Commands/Compounds.hpp"
#include "Rellu/Model/CompoundResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 0.01;
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto OPERAND_CHANGES =
    ChangeMask::TRANSFORM | ChangeMask::GEOMETRY | ChangeMask::CHILDREN;
  constexpr auto RESULT_CHANGES = ChangeMask::GEOMETRY | ChangeMask::CHILDREN;

  const CompoundShape* get_compound(const Node& node) {
    if(auto shape = node.get_shape()) {
      return std::get_if<CompoundShape>(&shape->m_geometry);
    }
    return nullptr;
  }

  struct Entry {
    bool m_is_shared;
    int m_depth;
    NodeId m_id;

    bool operator ==(const Entry&) const = default;
  };

  Entry make_entry(const Document& document, const Node& node) {
    auto depth = 0;
    auto current = &node;
    while(depth != MAXIMUM_DEPTH && !current->m_parent.is_nil()) {
      auto parent = document.find(current->m_parent);
      if(!parent) {
        break;
      }
      current = parent;
      ++depth;
    }
    return Entry(!document.is_on_canvas(current->get_id()), depth,
      node.get_id());
  }

  void add_compounds(const Document& document, const NodeId& id,
      bool is_result_changed, std::vector<Entry>& compounds) {
    auto node = document.find(id);
    if(!node) {
      return;
    }
    if(is_result_changed && get_compound(*node)) {
      compounds.push_back(make_entry(document, *node));
    }
    for(auto depth = 0; depth != MAXIMUM_DEPTH; ++depth) {
      auto parent = document.find(node->m_parent);
      if(!parent || parent->get_artboard()) {
        return;
      }
      if(get_compound(*parent)) {
        compounds.push_back(make_entry(document, *parent));
      }
      node = parent;
    }
  }

  void add_lineage(const Document& document, const NodeId& id,
      std::unordered_set<NodeId>& lineage) {
    auto current = id;
    for(auto depth = 0; depth != MAXIMUM_DEPTH && !current.is_nil(); ++depth) {
      auto node = document.find(current);
      if(!node || !lineage.insert(current).second) {
        return;
      }
      current = node->m_parent;
    }
  }

  bool is_affected(const Document& document, const Node& node,
      const std::unordered_set<NodeId>& lineage, int depth) {
    if(depth == MAXIMUM_DEPTH) {
      return false;
    }
    auto source = node.get_reference() ? &node : nullptr;
    for(auto chain = 0; source && chain != MAXIMUM_DEPTH; ++chain) {
      auto reference = source->get_reference();
      if(!reference) {
        break;
      }
      if(lineage.contains(reference->m_source)) {
        return true;
      }
      source = document.find(reference->m_source);
    }
    if(get_compound(node) && depth != 0) {
      return false;
    }
    for(auto& id : node.m_children) {
      if(auto child = document.find(id)) {
        if(is_affected(document, *child, lineage, depth + 1)) {
          return true;
        }
      }
    }
    return false;
  }

  void add_referencing_compounds(const Document& document,
      const std::unordered_set<NodeId>& lineage,
      std::vector<Entry>& compounds) {
    document.get_nodes().for_each([&] (const Node& node) {
      if(get_compound(node) && is_affected(document, node, lineage, 0)) {
        compounds.push_back(make_entry(document, node));
        add_compounds(document, node.get_id(), false, compounds);
      }
    });
  }
}

bool Rellu::refresh_compounds(Document& document, const ChangeSet& changes,
    const CompoundFilter& filter) {
  auto compounds = std::vector<Entry>();
  auto lineage = std::unordered_set<NodeId>();
  auto add_changed = [&] (const NodeId& id) {
    auto node = document.find(id);
    if(node && make_entry(document, *node).m_is_shared) {
      add_lineage(document, id, lineage);
    }
  };
  for(auto& id : changes.m_inserted) {
    add_compounds(document, id, false, compounds);
    add_changed(id);
  }
  for(auto& id : changes.m_moved) {
    add_compounds(document, id, false, compounds);
    add_changed(id);
  }
  for(auto& change : changes.m_modified) {
    if(is_set(change.m_mask, OPERAND_CHANGES)) {
      add_compounds(document, change.m_id,
        is_set(change.m_mask, RESULT_CHANGES), compounds);
      add_changed(change.m_id);
    }
  }
  if(!lineage.empty()) {
    add_referencing_compounds(document, lineage, compounds);
  }
  if(compounds.empty()) {
    return false;
  }
  std::sort(compounds.begin(), compounds.end(),
    [] (const auto& left, const auto& right) {
      if(left.m_is_shared != right.m_is_shared) {
        return left.m_is_shared;
      } else if(left.m_depth != right.m_depth) {
        return left.m_depth > right.m_depth;
      }
      return left.m_id < right.m_id;
    });
  compounds.erase(std::unique(compounds.begin(), compounds.end()),
    compounds.end());
  auto transaction = std::optional<Transaction>();
  for(auto& entry : compounds) {
    auto node = document.find(entry.m_id);
    if(!node) {
      continue;
    }
    auto compound = get_compound(*node);
    if(!compound || (filter && !filter(*node))) {
      continue;
    }
    auto result = combine_operands(document.get_nodes(), *node, TOLERANCE);
    if(!result || *result == *compound->m_path) {
      continue;
    }
    auto shape = *node->get_shape();
    std::get<CompoundShape>(shape.m_geometry).m_path =
      Shared(std::move(*result));
    if(!transaction) {
      transaction.emplace(document);
    }
    transaction->replace_payload(entry.m_id, std::move(shape));
  }
  if(!transaction) {
    return false;
  }
  transaction->amend();
  return true;
}

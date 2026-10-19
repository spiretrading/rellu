#include <algorithm>
#include <optional>
#include <utility>
#include <variant>
#include <vector>
#include "Rellu/Commands/Compounds.hpp"
#include "Rellu/Geometry/BooleanOperation.hpp"
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

  BooleanOperation to_operation(CompoundOperation operation) {
    switch(operation) {
      case CompoundOperation::SUBTRACT:
        return BooleanOperation::SUBTRACT;
      case CompoundOperation::INTERSECT:
        return BooleanOperation::INTERSECT;
      case CompoundOperation::EXCLUDE:
        return BooleanOperation::EXCLUDE;
      default:
        return BooleanOperation::ADD;
    }
  }

  int get_depth(const Document& document, const Node& node) {
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
    return depth;
  }

  void add_compounds(const Document& document, const NodeId& id,
      bool is_result_changed, std::vector<std::pair<int, NodeId>>& compounds) {
    auto node = document.find(id);
    if(!node) {
      return;
    }
    if(is_result_changed && get_compound(*node)) {
      compounds.emplace_back(get_depth(document, *node), id);
    }
    for(auto depth = 0; depth != MAXIMUM_DEPTH; ++depth) {
      auto parent = document.find(node->m_parent);
      if(!parent || !get_compound(*parent)) {
        return;
      }
      compounds.emplace_back(get_depth(document, *parent), parent->get_id());
      node = parent;
    }
  }

  std::optional<Path> combine_operands(const Document& document,
      const Node& node, const CompoundShape& compound) {
    auto operands = std::vector<Path>();
    for(auto& id : node.m_children) {
      auto child = document.find(id);
      if(!child || !child->get_shape()) {
        return std::nullopt;
      }
      operands.push_back(transform(to_path(child->get_shape()->m_geometry),
        child->m_header.m_transform));
    }
    if(operands.empty()) {
      return std::nullopt;
    }
    return combine(operands, to_operation(compound.m_operation), TOLERANCE);
  }
}

bool Rellu::refresh_compounds(Document& document, const ChangeSet& changes,
    const CompoundFilter& filter) {
  auto compounds = std::vector<std::pair<int, NodeId>>();
  for(auto& id : changes.m_inserted) {
    add_compounds(document, id, false, compounds);
  }
  for(auto& id : changes.m_moved) {
    add_compounds(document, id, false, compounds);
  }
  for(auto& change : changes.m_modified) {
    if(is_set(change.m_mask, OPERAND_CHANGES)) {
      add_compounds(document, change.m_id,
        is_set(change.m_mask, RESULT_CHANGES), compounds);
    }
  }
  if(compounds.empty()) {
    return false;
  }
  std::sort(compounds.begin(), compounds.end(),
    [] (const auto& left, const auto& right) {
      if(left.first != right.first) {
        return left.first > right.first;
      }
      return left.second < right.second;
    });
  compounds.erase(std::unique(compounds.begin(), compounds.end()),
    compounds.end());
  auto transaction = std::optional<Transaction>();
  for(auto& entry : compounds) {
    auto node = document.find(entry.second);
    if(!node) {
      continue;
    }
    auto compound = get_compound(*node);
    if(!compound || (filter && !filter(*node))) {
      continue;
    }
    auto result = combine_operands(document, *node, *compound);
    if(!result || *result == *compound->m_path) {
      continue;
    }
    auto shape = *node->get_shape();
    std::get<CompoundShape>(shape.m_geometry).m_path =
      Shared(std::move(*result));
    if(!transaction) {
      transaction.emplace(document);
    }
    transaction->replace_payload(entry.second, std::move(shape));
  }
  if(!transaction) {
    return false;
  }
  transaction->amend();
  return true;
}

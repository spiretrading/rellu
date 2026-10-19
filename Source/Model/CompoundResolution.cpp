#include <utility>
#include <variant>
#include <vector>
#include "Rellu/Model/CompoundResolution.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;

  std::optional<Path> get_outline(const NodeStore& nodes, const Node& operand,
    double tolerance, const NestedCompound& nested, int depth);

  std::optional<Path> unite(const NodeStore& nodes,
      const std::vector<NodeId>& children, double tolerance,
      const NestedCompound& nested, int depth) {
    auto outlines = std::vector<Path>();
    outlines.reserve(children.size());
    for(auto& id : children) {
      auto child = nodes.find(id);
      if(!child) {
        return std::nullopt;
      }
      auto outline = get_outline(nodes, *child, tolerance, nested, depth + 1);
      if(!outline) {
        return std::nullopt;
      }
      outlines.push_back(std::move(*outline));
    }
    if(outlines.empty()) {
      return Path();
    } else if(outlines.size() == 1) {
      return std::move(outlines.front());
    }
    return combine(outlines, BooleanOperation::ADD, tolerance);
  }

  std::optional<Path> get_local_outline(const NodeStore& nodes,
      const Node& source, const std::vector<NodeId>& children,
      double tolerance, const NestedCompound& nested, int depth) {
    if(auto shape = source.get_shape()) {
      if(auto compound = std::get_if<CompoundShape>(&shape->m_geometry)) {
        if(nested) {
          return nested(source);
        }
        return *compound->m_path;
      }
      return to_path(shape->m_geometry);
    } else if(auto group = source.get_group()) {
      if(group->m_mask) {
        return std::nullopt;
      }
      return unite(nodes, children, tolerance, nested, depth);
    }
    return std::nullopt;
  }

  std::optional<Path> get_outline(const NodeStore& nodes, const Node& operand,
      double tolerance, const NestedCompound& nested, int depth) {
    if(depth == MAXIMUM_DEPTH) {
      return std::nullopt;
    }
    if(operand.get_reference()) {
      auto resolution = resolve_reference(nodes, operand.get_id());
      if(!resolution) {
        return std::nullopt;
      }
      auto source = nodes.find(resolution->m_source);
      auto child_source = nodes.find(resolution->m_child_source);
      if(!source || !child_source) {
        return std::nullopt;
      }
      auto outline = get_local_outline(nodes, *source,
        child_source->m_children, tolerance, nested, depth);
      if(!outline) {
        return std::nullopt;
      }
      return transform(*outline, resolution->m_transform);
    }
    auto outline = get_local_outline(
      nodes, operand, operand.m_children, tolerance, nested, depth);
    if(!outline) {
      return std::nullopt;
    }
    return transform(*outline, operand.m_header.m_transform);
  }
}

BooleanOperation Rellu::to_boolean_operation(CompoundOperation operation) {
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

std::optional<Path> Rellu::get_operand_outline(const NodeStore& nodes,
    const Node& operand, double tolerance, const NestedCompound& nested) {
  return get_outline(nodes, operand, tolerance, nested, 0);
}

std::optional<Path> Rellu::combine_operands(const NodeStore& nodes,
    const Node& compound, double tolerance, const NestedCompound& nested) {
  auto shape = compound.get_shape();
  if(!shape) {
    return std::nullopt;
  }
  auto geometry = std::get_if<CompoundShape>(&shape->m_geometry);
  if(!geometry || compound.m_children.empty()) {
    return std::nullopt;
  }
  auto operands = std::vector<Path>();
  operands.reserve(compound.m_children.size());
  for(auto& id : compound.m_children) {
    auto child = nodes.find(id);
    if(!child) {
      return std::nullopt;
    }
    auto outline = get_outline(nodes, *child, tolerance, nested, 0);
    if(!outline) {
      return std::nullopt;
    }
    operands.push_back(std::move(*outline));
  }
  return combine(
    operands, to_boolean_operation(geometry->m_operation), tolerance);
}

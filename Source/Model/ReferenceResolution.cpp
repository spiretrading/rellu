#include <algorithm>
#include <vector>
#include "Rellu/Model/ReferenceResolution.hpp"

using namespace Rellu;

std::optional<ResolvedReference> Rellu::resolve_reference(
    const NodeStore& nodes, const NodeId& reference) {
  auto node = nodes.find(reference);
  if(!node || !node->get_reference()) {
    return std::nullopt;
  }
  auto transform = std::optional<Matrix>();
  auto child_source = std::optional<NodeId>();
  auto visited = std::vector<NodeId>();
  while(auto payload = node->get_reference()) {
    if(std::find(visited.begin(), visited.end(), node->get_id()) !=
        visited.end()) {
      return std::nullopt;
    }
    visited.push_back(node->get_id());
    if(!transform && payload->m_transform) {
      transform = *payload->m_transform;
    }
    if(!child_source && payload->m_has_children) {
      child_source = node->get_id();
    }
    node = nodes.find(payload->m_source);
    if(!node) {
      return std::nullopt;
    }
  }
  return ResolvedReference(node->get_id(),
    transform.value_or(node->m_header.m_transform),
    child_source.value_or(node->get_id()));
}

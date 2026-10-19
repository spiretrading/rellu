#include "Rellu/Model/ReferenceResolution.hpp"

using namespace Rellu;

std::optional<ResolvedReference> Rellu::resolve_reference(
    const NodeStore& nodes, const NodeId& reference) {
  auto node = nodes.find(reference);
  if(!node || !node->get_reference()) {
    return std::nullopt;
  }
  auto transform = static_cast<const Matrix*>(nullptr);
  auto child_source = static_cast<const NodeId*>(nullptr);
  auto marker = &node->get_id();
  auto power = 1;
  auto length = 0;
  while(auto payload = node->get_reference()) {
    if(!transform && payload->m_transform) {
      transform = &*payload->m_transform;
    }
    if(!child_source && payload->m_has_children) {
      child_source = &node->get_id();
    }
    node = nodes.find(payload->m_source);
    if(!node || node->get_id() == *marker) {
      return std::nullopt;
    }
    if(++length == power) {
      marker = &node->get_id();
      power *= 2;
      length = 0;
    }
  }
  return ResolvedReference(node->get_id(),
    transform ? *transform : node->m_header.m_transform,
    child_source ? *child_source : node->get_id());
}

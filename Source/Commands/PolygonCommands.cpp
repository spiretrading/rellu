#include <algorithm>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/PolygonCommands.hpp"

using namespace Rellu;

SetCornerCountCommand::SetCornerCountCommand(
  std::vector<NodeId> nodes, int count) noexcept
  : m_nodes(std::move(nodes)),
    m_count(std::clamp(count, MINIMUM_CORNERS, MAXIMUM_CORNERS)) {}

std::string SetCornerCountCommand::get_label() const {
  return "Change Corner Count";
}

void SetCornerCountCommand::apply(
    const Document& document, Transaction& transaction) {
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : m_nodes) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      continue;
    }
    auto shape = node->get_shape();
    if(!shape) {
      continue;
    }
    auto polygon = std::get_if<PolygonShape>(&shape->m_geometry);
    if(!polygon || polygon->m_corner_count == m_count) {
      continue;
    }
    auto payload = *shape;
    auto& edited = std::get<PolygonShape>(payload.m_geometry);
    edited.m_corner_count = m_count;
    edited.m_vertices.clear();
    transaction.replace_payload(id, std::move(payload));
  }
}

bool SetCornerCountCommand::merge(const Command& command) {
  auto corners = dynamic_cast<const SetCornerCountCommand*>(&command);
  if(!corners || corners->m_nodes != m_nodes) {
    return false;
  }
  m_count = corners->m_count;
  return true;
}

SetStarRatioCommand::SetStarRatioCommand(
  std::vector<NodeId> nodes, double ratio) noexcept
  : m_nodes(std::move(nodes)),
    m_ratio(std::clamp(ratio, MINIMUM_RATIO, MAXIMUM_RATIO)) {}

std::string SetStarRatioCommand::get_label() const {
  return "Change Star Ratio";
}

void SetStarRatioCommand::apply(
    const Document& document, Transaction& transaction) {
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : m_nodes) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      continue;
    }
    auto shape = node->get_shape();
    if(!shape) {
      continue;
    }
    auto polygon = std::get_if<PolygonShape>(&shape->m_geometry);
    if(!polygon || polygon->m_star_ratio == m_ratio) {
      continue;
    }
    auto payload = *shape;
    auto& edited = std::get<PolygonShape>(payload.m_geometry);
    edited.m_star_ratio = m_ratio;
    edited.m_vertices.clear();
    transaction.replace_payload(id, std::move(payload));
  }
}

bool SetStarRatioCommand::merge(const Command& command) {
  auto ratio = dynamic_cast<const SetStarRatioCommand*>(&command);
  if(!ratio || ratio->m_nodes != m_nodes) {
    return false;
  }
  m_ratio = ratio->m_ratio;
  return true;
}

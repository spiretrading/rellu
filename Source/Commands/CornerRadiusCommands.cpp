#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <variant>
#include "Rellu/Commands/CornerRadiusCommands.hpp"

using namespace Rellu;

namespace {
  double clamp_radius(double radius) {
    if(!(radius > 0)) {
      return 0;
    }
    return radius;
  }

  double& get_corner(CornerRadii& radii, std::size_t corner) {
    if(corner == 0) {
      return radii.m_top_left;
    } else if(corner == 1) {
      return radii.m_top_right;
    } else if(corner == 2) {
      return radii.m_bottom_right;
    }
    return radii.m_bottom_left;
  }
}

SetCornerRadiusCommand::SetCornerRadiusCommand(
  std::vector<NodeId> nodes, double radius) noexcept
  : m_nodes(std::move(nodes)),
    m_radii({clamp_radius(radius), clamp_radius(radius), clamp_radius(radius),
      clamp_radius(radius)}),
    m_polygon_radius(clamp_radius(radius)) {}

SetCornerRadiusCommand::SetCornerRadiusCommand(
  std::vector<NodeId> nodes, const CornerRadii& radii) noexcept
  : m_nodes(std::move(nodes)),
    m_radii({clamp_radius(radii.m_top_left), clamp_radius(radii.m_top_right),
      clamp_radius(radii.m_bottom_right), clamp_radius(radii.m_bottom_left)}) {}

SetCornerRadiusCommand::SetCornerRadiusCommand(
    std::vector<NodeId> nodes, Corner corner, double radius) noexcept
    : m_nodes(std::move(nodes)) {
  m_radii[static_cast<std::size_t>(corner)] = clamp_radius(radius);
}

std::string SetCornerRadiusCommand::get_label() const {
  return "Corner Radius";
}

void SetCornerRadiusCommand::apply(
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
    auto payload = *shape;
    if(auto rectangle = std::get_if<RectangleShape>(&payload.m_geometry)) {
      for(auto i = std::size_t(0); i != m_radii.size(); ++i) {
        if(m_radii[i]) {
          get_corner(rectangle->m_radii, i) = *m_radii[i];
        }
      }
    } else if(auto polygon = std::get_if<PolygonShape>(&payload.m_geometry)) {
      if(m_polygon_radius) {
        polygon->m_corner_radius = *m_polygon_radius;
      }
    }
    if(payload != *shape) {
      transaction.replace_payload(id, std::move(payload));
    }
  }
}

bool SetCornerRadiusCommand::merge(const Command& command) {
  auto radius = dynamic_cast<const SetCornerRadiusCommand*>(&command);
  if(!radius || radius->m_nodes != m_nodes) {
    return false;
  }
  for(auto i = std::size_t(0); i != m_radii.size(); ++i) {
    if(radius->m_radii[i]) {
      m_radii[i] = radius->m_radii[i];
    }
  }
  if(radius->m_polygon_radius) {
    m_polygon_radius = radius->m_polygon_radius;
  }
  return true;
}

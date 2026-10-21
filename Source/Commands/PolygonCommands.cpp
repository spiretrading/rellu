#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/PolygonCommands.hpp"
#include "Rellu/Geometry/Shapes.hpp"

using namespace Rellu;

namespace {
  double snap(double start, double size, double offset) {
    auto snapped = std::round(start - offset) + offset;
    return std::round(start + size - offset) + offset - snapped;
  }

  double get_offset(double position) {
    if(position - std::floor(position) == 0.5) {
      return 0.5;
    }
    return 0;
  }

  void reshape(Matrix& transform, Size& size, int count) {
    auto aspect = get_equilateral_aspect(count);
    auto center = transform.map(Point(size.m_width / 2, size.m_height / 2));
    size.m_width = aspect.m_width / aspect.m_height * size.m_height;
    if(transform.m_a == 1 && transform.m_b == 0 && transform.m_c == 0 &&
        transform.m_d == 1) {
      size = Size(snap(transform.m_tx, size.m_width, get_offset(
        transform.m_tx)), snap(transform.m_ty, size.m_height,
          get_offset(transform.m_ty)));
    }
    auto moved = transform.map(Point(size.m_width / 2, size.m_height / 2));
    transform.m_tx += center.m_x - moved.m_x;
    transform.m_ty += center.m_y - moved.m_y;
  }
}

SetCornerCountCommand::SetCornerCountCommand(
  std::vector<NodeId> nodes, int count) noexcept
  : m_nodes(std::move(nodes)),
    m_count(std::clamp(count, MINIMUM_CORNERS, MAXIMUM_CORNERS)) {}

std::string SetCornerCountCommand::get_label() const {
  return "Corner Count";
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
    auto is_equal = is_equilateral(edited.m_corner_count, edited.m_size);
    edited.m_corner_count = m_count;
    edited.m_vertices.clear();
    if(is_equal) {
      auto header = node->m_header;
      reshape(header.m_transform, edited.m_size, m_count);
      transaction.replace_header(std::move(header));
    }
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
  return "Star Ratio";
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

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include "Rellu/Commands/AlignCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/TransformCommands.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_DISTRIBUTED = std::size_t(3);

  struct Placed {
    NodeId m_id;
    Rect m_bounds;
  };

  bool is_locked(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    while(node) {
      if(node->m_header.m_is_locked) {
        return true;
      }
      if(node->m_parent.is_nil()) {
        return false;
      }
      node = document.find(node->m_parent);
    }
    return false;
  }

  bool is_on_canvas(const Document& document, const NodeId& id) {
    auto current = id;
    while(auto node = document.find(current)) {
      if(node->m_parent.is_nil()) {
        return document.is_on_canvas(current);
      }
      current = node->m_parent;
    }
    return false;
  }

  double get_start(const Rect& rect, DistributionAxis axis) {
    if(axis == DistributionAxis::HORIZONTAL) {
      return rect.get_left();
    }
    return rect.get_top();
  }

  double get_end(const Rect& rect, DistributionAxis axis) {
    if(axis == DistributionAxis::HORIZONTAL) {
      return rect.get_right();
    }
    return rect.get_bottom();
  }

  double get_length(const Rect& rect, DistributionAxis axis) {
    return get_end(rect, axis) - get_start(rect, axis);
  }

  Point get_offset(
      const Rect& bounds, const Rect& target, ObjectAlignment alignment) {
    if(alignment == ObjectAlignment::LEFT) {
      return Point(target.get_left() - bounds.get_left(), 0);
    } else if(alignment == ObjectAlignment::CENTER) {
      return Point(target.get_center().m_x - bounds.get_center().m_x, 0);
    } else if(alignment == ObjectAlignment::RIGHT) {
      return Point(target.get_right() - bounds.get_right(), 0);
    } else if(alignment == ObjectAlignment::TOP) {
      return Point(0, target.get_top() - bounds.get_top());
    } else if(alignment == ObjectAlignment::MIDDLE) {
      return Point(0, target.get_center().m_y - bounds.get_center().m_y);
    }
    return Point(0, target.get_bottom() - bounds.get_bottom());
  }

  std::vector<Placed> place(const Document& document,
      const std::vector<NodeId>& nodes, const TextMeasurer& measurer) {
    auto placed = std::vector<Placed>();
    for(auto& id : nodes) {
      if(auto bounds =
          get_world_bounds(document, id, BoundsType::GEOMETRY, measurer)) {
        placed.push_back(Placed(id, *bounds));
      }
    }
    return placed;
  }

  CommandFactory make_align_factory(
      ObjectAlignment alignment, TextMeasurer measurer) {
    return [=] (const auto& document, const auto& targets) ->
        std::unique_ptr<Command> {
      auto nodes = get_alignable(document, targets);
      if(nodes.empty() ||
          !get_alignment_bounds(document, nodes, measurer)) {
        return nullptr;
      }
      return std::make_unique<AlignCommand>(
        std::move(nodes), alignment, measurer);
    };
  }

  CommandFactory make_distribute_factory(
      DistributionAxis axis, TextMeasurer measurer) {
    return [=] (const auto& document, const auto& targets) ->
        std::unique_ptr<Command> {
      auto nodes = get_alignable(document, targets);
      if(nodes.size() < MINIMUM_DISTRIBUTED) {
        return nullptr;
      }
      return std::make_unique<DistributeCommand>(
        std::move(nodes), axis, measurer);
    };
  }
}

std::vector<NodeId> Rellu::get_alignable(
    const Document& document, const std::vector<NodeId>& targets) {
  auto nodes = get_top_level(document, targets);
  std::erase_if(nodes, [&] (const auto& id) {
    return !is_on_canvas(document, id) || is_locked(document, id);
  });
  return nodes;
}

std::optional<Rect> Rellu::get_alignment_bounds(const Document& document,
    const std::vector<NodeId>& nodes, const TextMeasurer& measurer) {
  if(nodes.empty()) {
    return std::nullopt;
  } else if(nodes.size() > 1) {
    return get_world_bounds(document, nodes, BoundsType::GEOMETRY, measurer);
  }
  auto node = document.find(nodes.front());
  if(!node || node->get_artboard() || node->m_parent.is_nil()) {
    return std::nullopt;
  }
  auto parent = document.find(node->m_parent);
  if(!parent || !parent->get_artboard()) {
    return std::nullopt;
  }
  return get_world_bounds(
    document, node->m_parent, BoundsType::GEOMETRY, measurer);
}

AlignCommand::AlignCommand(std::vector<NodeId> nodes,
  ObjectAlignment alignment, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_alignment(alignment),
    m_measurer(std::move(measurer)) {}

std::string AlignCommand::get_label() const {
  return "Align";
}

void AlignCommand::apply(const Document& document, Transaction& transaction) {
  auto nodes = get_alignable(document, m_nodes);
  auto target = get_alignment_bounds(document, nodes, m_measurer);
  if(!target) {
    return;
  }
  for(auto& placed : place(document, nodes, m_measurer)) {
    auto offset = get_offset(placed.m_bounds, *target, m_alignment);
    translate_node(document, transaction, placed.m_id, offset);
  }
}

DistributeCommand::DistributeCommand(std::vector<NodeId> nodes,
  DistributionAxis axis, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_axis(axis),
    m_measurer(std::move(measurer)) {}

std::string DistributeCommand::get_label() const {
  return "Distribute";
}

void DistributeCommand::apply(
    const Document& document, Transaction& transaction) {
  auto placed = place(document, get_alignable(document, m_nodes), m_measurer);
  if(placed.size() < MINIMUM_DISTRIBUTED) {
    return;
  }
  std::stable_sort(placed.begin(), placed.end(),
    [&] (const auto& left, const auto& right) {
      auto left_start = get_start(left.m_bounds, m_axis);
      auto right_start = get_start(right.m_bounds, m_axis);
      if(left_start != right_start) {
        return left_start < right_start;
      }
      return get_end(left.m_bounds, m_axis) < get_end(right.m_bounds, m_axis);
    });
  auto start = get_start(placed.front().m_bounds, m_axis);
  auto end = start;
  auto total = 0.0;
  for(auto& entry : placed) {
    end = std::max(end, get_end(entry.m_bounds, m_axis));
    total += get_length(entry.m_bounds, m_axis);
  }
  auto gap = (end - start - total) / static_cast<double>(placed.size() - 1);
  auto position = start;
  for(auto& entry : placed) {
    auto offset = position - get_start(entry.m_bounds, m_axis);
    if(m_axis == DistributionAxis::HORIZONTAL) {
      translate_node(document, transaction, entry.m_id, Point(offset, 0));
    } else {
      translate_node(document, transaction, entry.m_id, Point(0, offset));
    }
    position += get_length(entry.m_bounds, m_axis) + gap;
  }
}

void Rellu::add_align_commands(
    CommandRegistry& registry, TextMeasurer measurer) {
  registry.add("object.align.left",
    make_align_factory(ObjectAlignment::LEFT, measurer));
  registry.add("object.align.center",
    make_align_factory(ObjectAlignment::CENTER, measurer));
  registry.add("object.align.right",
    make_align_factory(ObjectAlignment::RIGHT, measurer));
  registry.add("object.align.top",
    make_align_factory(ObjectAlignment::TOP, measurer));
  registry.add("object.align.middle",
    make_align_factory(ObjectAlignment::MIDDLE, measurer));
  registry.add("object.align.bottom",
    make_align_factory(ObjectAlignment::BOTTOM, measurer));
  registry.add("object.distribute.horizontally",
    make_distribute_factory(DistributionAxis::HORIZONTAL, measurer));
  registry.add("object.distribute.vertically",
    make_distribute_factory(DistributionAxis::VERTICAL, measurer));
}

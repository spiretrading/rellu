#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/StackCommands.hpp"

using namespace Rellu;

namespace {
  const Group* find_group(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node) {
      return nullptr;
    }
    auto group = node->get_group();
    if(!group || group->m_repeat_grid) {
      return nullptr;
    }
    return group;
  }

  bool is_visible(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->m_header.m_is_visible;
  }

  std::optional<Rect> measure(const Document& document, const NodeId& id,
      const TextMeasurer& measurer) {
    return get_bounds(document, id, get_transform(document, id),
      BoundsType::GEOMETRY, measurer);
  }

  void relayout(const Document& document, Transaction& transaction,
      const NodeId& id, const TextMeasurer& measurer) {
    auto group = document.get(id).get_group();
    if(group->m_stack) {
      layout_stack(document, transaction, id, measurer);
    } else if(group->m_padding) {
      layout_padding(document, transaction, id, measurer);
    }
  }

  void enable_stack(const Document& document, Transaction& transaction,
      const NodeId& id, Orientation orientation,
      const TextMeasurer& measurer) {
    auto group = *document.get(id).get_group();
    auto children = document.get(id).m_children;
    auto is_vertical = orientation == Orientation::VERTICAL;
    auto has_background = group.m_padding &&
      group.m_padding->m_has_background && children.size() >= 2;
    auto background = NodeId();
    if(has_background) {
      background = children.front();
      children.erase(children.begin());
    }
    auto bounds = std::unordered_map<NodeId, Rect>();
    auto items = std::vector<NodeId>();
    auto others = std::vector<NodeId>();
    for(auto i = children.rbegin(); i != children.rend(); ++i) {
      auto rect = is_visible(document, *i) ?
        measure(document, *i, measurer) : std::nullopt;
      if(rect) {
        bounds.emplace(*i, *rect);
        items.push_back(*i);
      } else {
        others.push_back(*i);
      }
    }
    auto get_start = [&] (const NodeId& item) {
      auto& rect = bounds.at(item);
      return is_vertical ? rect.m_y : rect.m_x;
    };
    std::stable_sort(items.begin(), items.end(),
      [&] (const auto& left, const auto& right) {
        return get_start(left) < get_start(right);
      });
    auto stack = Stack();
    stack.m_orientation = orientation;
    for(auto i = std::size_t(0); i + 1 < items.size(); ++i) {
      auto& rect = bounds.at(items[i]);
      auto end = is_vertical ? rect.get_bottom() : rect.get_right();
      stack.m_spacing.emplace(items[i], get_start(items[i + 1]) - end);
    }
    if(has_background) {
      stack.m_padding = group.m_padding->m_padding;
      stack.m_has_background = true;
    }
    auto order = std::vector<NodeId>();
    if(has_background) {
      order.push_back(background);
    }
    order.insert(order.end(), others.rbegin(), others.rend());
    order.insert(order.end(), items.rbegin(), items.rend());
    reorder_children(document, transaction, id, order);
    group.m_padding = std::nullopt;
    group.m_stack = std::move(stack);
    transaction.replace_payload(id, std::move(group));
    layout_stack(document, transaction, id, measurer);
  }

  Margins measure_padding(const Document& document, const NodeId& id,
      const TextMeasurer& measurer) {
    auto& children = document.get(id).m_children;
    auto background = measure(document, children.front(), measurer);
    auto content = std::optional<Rect>();
    for(auto i = std::size_t(1); i < children.size(); ++i) {
      if(!is_visible(document, children[i])) {
        continue;
      }
      if(auto rect = measure(document, children[i], measurer)) {
        content = content ? unite(*content, *rect) : *rect;
      }
    }
    if(!background || !content) {
      return Margins();
    }
    return Margins(std::max(0.0, content->get_top() - background->get_top()),
      std::max(0.0, background->get_right() - content->get_right()),
      std::max(0.0, background->get_bottom() - content->get_bottom()),
      std::max(0.0, content->get_left() - background->get_left()));
  }

  void set_side(Margins& margins, PaddingSide side, double value) {
    if(side == PaddingSide::ALL || side == PaddingSide::TOP) {
      margins.m_top = value;
    }
    if(side == PaddingSide::ALL || side == PaddingSide::RIGHT) {
      margins.m_right = value;
    }
    if(side == PaddingSide::ALL || side == PaddingSide::BOTTOM) {
      margins.m_bottom = value;
    }
    if(side == PaddingSide::ALL || side == PaddingSide::LEFT) {
      margins.m_left = value;
    }
  }
}

SetStackCommand::SetStackCommand(std::vector<NodeId> nodes,
  std::optional<Orientation> orientation, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_orientation(orientation),
    m_measurer(std::move(measurer)) {}

std::string SetStackCommand::get_label() const {
  return "Stack";
}

void SetStackCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    auto found = find_group(document, id);
    if(!found) {
      continue;
    }
    auto group = *found;
    if(!m_orientation) {
      if(!group.m_stack) {
        continue;
      }
      auto stack = *group.m_stack;
      group.m_stack = std::nullopt;
      if(stack.m_has_background) {
        group.m_padding = Padding(stack.m_padding, true, true);
      }
      transaction.replace_payload(id, std::move(group));
    } else if(group.m_stack) {
      if(group.m_stack->m_orientation == *m_orientation) {
        continue;
      }
      group.m_stack->m_orientation = *m_orientation;
      transaction.replace_payload(id, std::move(group));
      layout_stack(document, transaction, id, m_measurer);
    } else {
      enable_stack(document, transaction, id, *m_orientation, m_measurer);
    }
  }
}

SetStackSpacingCommand::SetStackSpacingCommand(std::vector<NodeId> nodes,
  double spacing, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_spacing(spacing),
    m_measurer(std::move(measurer)) {}

SetStackSpacingCommand::SetStackSpacingCommand(const NodeId& stack,
  const NodeId& item, double spacing, TextMeasurer measurer)
  : m_nodes(std::vector{stack}),
    m_item(item),
    m_spacing(spacing),
    m_measurer(std::move(measurer)) {}

std::string SetStackSpacingCommand::get_label() const {
  return "Spacing";
}

void SetStackSpacingCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    auto found = find_group(document, id);
    if(!found || !found->m_stack) {
      continue;
    }
    auto group = *found;
    auto& spacing = group.m_stack->m_spacing;
    if(!m_item.is_nil()) {
      auto& children = document.get(id).m_children;
      if(std::find(children.begin(), children.end(), m_item) ==
          children.end()) {
        continue;
      }
      spacing[m_item] = m_spacing;
    } else {
      auto items = get_stack_items(document, id);
      auto last = std::find_if(items.rbegin(), items.rend(),
        [&] (const auto& item) { return is_visible(document, item); });
      for(auto& item : items) {
        if(last == items.rend() || item != *last) {
          spacing[item] = m_spacing;
        }
      }
    }
    if(group == *found) {
      continue;
    }
    transaction.replace_payload(id, std::move(group));
    layout_stack(document, transaction, id, m_measurer);
  }
}

bool SetStackSpacingCommand::merge(const Command& command) {
  auto spacing = dynamic_cast<const SetStackSpacingCommand*>(&command);
  if(!spacing || spacing->m_nodes != m_nodes || spacing->m_item != m_item) {
    return false;
  }
  m_spacing = spacing->m_spacing;
  return true;
}

SetPaddingCommand::SetPaddingCommand(std::vector<NodeId> nodes,
  bool is_enabled, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_is_enabled(is_enabled),
    m_measurer(std::move(measurer)) {}

std::string SetPaddingCommand::get_label() const {
  return "Padding";
}

void SetPaddingCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    auto found = find_group(document, id);
    if(!found ||
        static_cast<bool>(get_padding(document, id)) == m_is_enabled) {
      continue;
    }
    auto group = *found;
    if(!m_is_enabled) {
      if(group.m_stack) {
        group.m_stack->m_has_background = false;
        group.m_stack->m_padding = Margins();
      } else {
        group.m_padding = std::nullopt;
      }
      transaction.replace_payload(id, std::move(group));
      relayout(document, transaction, id, m_measurer);
      continue;
    }
    if(document.get(id).m_children.size() < 2) {
      continue;
    }
    auto padding = measure_padding(document, id, m_measurer);
    if(group.m_stack) {
      group.m_stack->m_has_background = true;
      group.m_stack->m_padding = padding;
    } else {
      group.m_padding = Padding(padding, true, true);
    }
    transaction.replace_payload(id, std::move(group));
    relayout(document, transaction, id, m_measurer);
  }
}

SetPaddingValueCommand::SetPaddingValueCommand(std::vector<NodeId> nodes,
  PaddingSide side, double value, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_side(side),
    m_value(value),
    m_measurer(std::move(measurer)) {}

std::string SetPaddingValueCommand::get_label() const {
  return "Padding";
}

void SetPaddingValueCommand::apply(
    const Document& document, Transaction& transaction) {
  auto value = std::max(m_value, 0.0);
  for(auto& id : get_top_level(document, m_nodes)) {
    if(!get_padding(document, id)) {
      continue;
    }
    auto group = *find_group(document, id);
    if(group.m_stack) {
      set_side(group.m_stack->m_padding, m_side, value);
    } else {
      set_side(group.m_padding->m_padding, m_side, value);
    }
    if(group == *document.get(id).get_group()) {
      continue;
    }
    transaction.replace_payload(id, std::move(group));
    relayout(document, transaction, id, m_measurer);
  }
}

bool SetPaddingValueCommand::merge(const Command& command) {
  auto padding = dynamic_cast<const SetPaddingValueCommand*>(&command);
  if(!padding || padding->m_nodes != m_nodes || padding->m_side != m_side) {
    return false;
  }
  m_value = padding->m_value;
  return true;
}

Orientation Rellu::get_natural_orientation(const Document& document,
    const NodeId& id, const TextMeasurer& measurer) {
  auto node = document.find(id);
  if(!node) {
    return Orientation::VERTICAL;
  }
  auto horizontal = std::optional<std::pair<double, double>>();
  auto vertical = std::optional<std::pair<double, double>>();
  for(auto& child : node->m_children) {
    if(!is_visible(document, child)) {
      continue;
    }
    if(auto rect = measure(document, child, measurer)) {
      auto center = rect->get_center();
      horizontal = horizontal ? std::pair(std::min(horizontal->first,
        center.m_x), std::max(horizontal->second, center.m_x)) :
        std::pair(center.m_x, center.m_x);
      vertical = vertical ? std::pair(std::min(vertical->first, center.m_y),
        std::max(vertical->second, center.m_y)) :
        std::pair(center.m_y, center.m_y);
    }
  }
  if(horizontal && vertical && horizontal->second - horizontal->first >
      vertical->second - vertical->first) {
    return Orientation::HORIZONTAL;
  }
  return Orientation::VERTICAL;
}

std::optional<Margins> Rellu::get_padding(
    const Document& document, const NodeId& id) {
  auto group = find_group(document, id);
  if(!group) {
    return std::nullopt;
  }
  if(group->m_stack) {
    if(group->m_stack->m_has_background) {
      return group->m_stack->m_padding;
    }
    return std::nullopt;
  } else if(group->m_padding && group->m_padding->m_has_background) {
    return group->m_padding->m_padding;
  }
  return std::nullopt;
}

MoveStackItemsCommand::MoveStackItemsCommand(
  std::vector<NodeId> nodes, bool is_forward) noexcept
  : m_nodes(std::move(nodes)),
    m_is_forward(is_forward) {}

std::string MoveStackItemsCommand::get_label() const {
  return "Reorder";
}

void MoveStackItemsCommand::apply(
    const Document& document, Transaction& transaction) {
  if(m_nodes.empty()) {
    throw std::invalid_argument("There is nothing to reorder.");
  }
  auto stack = document.get(m_nodes.front()).m_parent;
  auto group = stack.is_nil() ? nullptr : find_group(document, stack);
  if(!group || !group->m_stack) {
    throw std::invalid_argument("The nodes are not stack items.");
  }
  auto items = get_stack_items(document, stack);
  auto selected = std::unordered_set<NodeId>(m_nodes.begin(), m_nodes.end());
  for(auto& id : m_nodes) {
    if(std::find(items.begin(), items.end(), id) == items.end()) {
      throw std::invalid_argument("The nodes are not items of one stack.");
    }
  }
  if(m_is_forward) {
    for(auto i = items.size(); i-- > 1;) {
      if(selected.contains(items[i - 1]) && !selected.contains(items[i])) {
        std::swap(items[i - 1], items[i]);
      }
    }
  } else {
    for(auto i = std::size_t(1); i < items.size(); ++i) {
      if(selected.contains(items[i]) && !selected.contains(items[i - 1])) {
        std::swap(items[i - 1], items[i]);
      }
    }
  }
  auto& children = document.get(stack).m_children;
  auto order = std::vector<NodeId>();
  if(group->m_stack->m_has_background && !children.empty()) {
    order.push_back(children.front());
  }
  order.insert(order.end(), items.rbegin(), items.rend());
  reorder_children(document, transaction, stack, order);
}

std::unique_ptr<Command> Rellu::make_stack_nudge(const Document& document,
    const std::vector<NodeId>& nodes, Point delta) {
  if(nodes.empty()) {
    return nullptr;
  }
  auto node = document.find(nodes.front());
  if(!node || node->m_parent.is_nil()) {
    return nullptr;
  }
  auto stack = node->m_parent;
  auto group = find_group(document, stack);
  if(!group || !group->m_stack) {
    return nullptr;
  }
  auto items = get_stack_items(document, stack);
  for(auto& id : nodes) {
    if(std::find(items.begin(), items.end(), id) == items.end()) {
      return nullptr;
    }
  }
  auto along = delta.m_y;
  auto across = delta.m_x;
  if(group->m_stack->m_orientation == Orientation::HORIZONTAL) {
    std::swap(along, across);
  }
  if(along == 0 || across != 0) {
    return nullptr;
  }
  return std::make_unique<MoveStackItemsCommand>(nodes, along > 0);
}

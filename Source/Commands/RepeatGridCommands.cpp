#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/RepeatGridCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_CELL = 1.0;

  bool is_tree_root(const Document& document, const NodeId& id) {
    return document.get(id).m_parent.is_nil() && !document.is_on_canvas(id);
  }

  bool is_plain_group(const Node& node) {
    auto group = node.get_group();
    return group && !group->m_mask && !group->m_repeat_grid &&
      !group->m_scroll_group && !group->m_stack && !group->m_padding &&
      !group->m_component_instance;
  }

  std::vector<NodeId> get_repeatable(
      const Document& document, const std::vector<NodeId>& targets) {
    auto nodes = get_top_level(document, targets);
    std::erase_if(nodes, [&] (const auto& id) {
      return is_tree_root(document, id);
    });
    return nodes;
  }

  void continue_series(const Document& document, Transaction& transaction,
      const NodeId& cell, std::size_t index) {
    auto pending = std::vector<NodeId>({cell});
    while(!pending.empty()) {
      auto id = pending.back();
      pending.pop_back();
      auto& node = document.get(id);
      pending.insert(
        pending.end(), node.m_children.begin(), node.m_children.end());
      auto text = node.get_text();
      if(!text || text->m_repeat_data.empty()) {
        continue;
      }
      auto& value = text->m_repeat_data[index % text->m_repeat_data.size()];
      if(*text->m_content == value) {
        continue;
      }
      auto copy = *text;
      copy.m_content = Shared<std::u16string>(value);
      if(!copy.m_runs.empty()) {
        auto run = copy.m_runs.front();
        run.m_length = value.size();
        copy.m_runs = std::vector{run};
      }
      copy.m_layout = std::nullopt;
      transaction.replace_payload(id, Box<Text>(std::move(copy)));
    }
  }

  const RepeatGrid& get_grid(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node || !node->get_group() || !node->get_group()->m_repeat_grid) {
      throw std::invalid_argument("The node is not a repeat grid.");
    }
    return *node->get_group()->m_repeat_grid;
  }
}

bool Rellu::is_repeat_grid(const Document& document, const NodeId& id) {
  auto node = document.find(id);
  return node && node->get_group() && node->get_group()->m_repeat_grid;
}

RepeatGridCommand::RepeatGridCommand(
  std::vector<NodeId> nodes, TextMeasurer measurer) noexcept
  : m_nodes(std::move(nodes)),
    m_measurer(std::move(measurer)) {}

const NodeId& RepeatGridCommand::get_grid() const {
  return m_grid;
}

std::string RepeatGridCommand::get_label() const {
  return "Repeat Grid";
}

void RepeatGridCommand::apply(
    const Document& document, Transaction& transaction) {
  auto nodes = get_repeatable(document, m_nodes);
  if(nodes.empty()) {
    throw std::invalid_argument("There is nothing to repeat.");
  }
  for(auto& id : nodes) {
    if(document.get(id).get_artboard()) {
      throw std::invalid_argument("An artboard can not be repeated.");
    }
  }
  sort_by_z_order(document, nodes);
  auto& topmost = nodes.back();
  auto parent = document.get(topmost).m_parent;
  auto index = get_index(document, topmost) + 1;
  auto inverse = invert(get_world_transform(document, parent));
  if(!inverse) {
    throw std::invalid_argument("The container's transform is singular.");
  }
  auto bounds = std::optional<Rect>();
  for(auto& id : nodes) {
    if(auto rect = get_bounds(document, id,
        *inverse * get_world_transform(document, id), BoundsType::GEOMETRY,
        m_measurer)) {
      bounds = bounds ? unite(*bounds, *rect) : *rect;
    }
  }
  if(!bounds) {
    throw std::invalid_argument("There is nothing to repeat.");
  }
  auto header = NodeHeader();
  header.m_name = make_node_name(document, "Repeat Grid ");
  header.m_transform = make_translation(bounds->m_x, bounds->m_y);
  auto group = Group();
  group.m_repeat_grid = RepeatGrid(bounds->get_size(), DEFAULT_REPEAT_SPACING,
    DEFAULT_REPEAT_SPACING, 1, 1, bounds->get_size());
  m_grid = transaction.insert(parent, index, std::move(header), group);
  if(nodes.size() == 1 && is_plain_group(document.get(nodes.front()))) {
    ReparentCommand(nodes, m_grid, 0).apply(document, transaction);
    return;
  }
  auto cell = NodeHeader();
  cell.m_transform = make_translation(-bounds->m_x, -bounds->m_y);
  auto cell_id = transaction.append(m_grid, std::move(cell), Group());
  ReparentCommand(std::move(nodes), cell_id, 0).apply(document, transaction);
}

std::optional<std::vector<NodeId>> RepeatGridCommand::get_selection() const {
  return std::vector{m_grid};
}

UngroupGridCommand::UngroupGridCommand(std::vector<NodeId> nodes) noexcept
  : m_nodes(std::move(nodes)) {}

std::string UngroupGridCommand::get_label() const {
  return "Ungroup Grid";
}

void UngroupGridCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    if(!is_repeat_grid(document, id)) {
      continue;
    }
    auto group = *document.get(id).get_group();
    group.m_repeat_grid = std::nullopt;
    transaction.replace_payload(id, std::move(group));
  }
}

ResizeRepeatGridCommand::ResizeRepeatGridCommand(
  const NodeId& grid, Size size) noexcept
  : m_grid(grid),
    m_size(size) {}

std::string ResizeRepeatGridCommand::get_label() const {
  return "Resize";
}

void ResizeRepeatGridCommand::apply(
    const Document& document, Transaction& transaction) {
  auto grid = get_grid(document, m_grid);
  auto cells = document.get(m_grid).m_children;
  if(cells.empty()) {
    throw std::invalid_argument("The repeat grid has no cells.");
  }
  auto size = Size(std::max(m_size.m_width, MINIMUM_CELL),
    std::max(m_size.m_height, MINIMUM_CELL));
  auto columns = count_repeat_cells(
    size.m_width, grid.m_cell_size.m_width, grid.m_column_spacing);
  auto rows = count_repeat_cells(
    size.m_height, grid.m_cell_size.m_height, grid.m_row_spacing);
  auto old_columns = std::max(grid.m_column_count, 1);
  auto old_rows = std::max(1, (static_cast<int>(cells.size()) +
    old_columns - 1) / old_columns);
  auto find = [&] (int row, int column) {
    auto index = static_cast<std::size_t>(row * old_columns + column);
    if(row < old_rows && column < old_columns && index < cells.size()) {
      return cells[index];
    }
    return NodeId();
  };
  auto order = std::vector<NodeId>();
  auto kept = std::unordered_set<NodeId>();
  for(auto row = 0; row != rows; ++row) {
    for(auto column = 0; column != columns; ++column) {
      auto cell = find(row, column);
      if(!cell.is_nil()) {
        order.push_back(cell);
        kept.insert(cell);
        continue;
      }
      auto source = find(
        std::min(row, old_rows - 1), std::min(column, old_columns - 1));
      if(source.is_nil()) {
        source = cells.back();
      }
      auto copy = copy_into(document, transaction, source, m_grid);
      if(!copy.is_nil()) {
        continue_series(document, transaction, copy,
          static_cast<std::size_t>(row * columns + column));
        order.push_back(copy);
      }
    }
  }
  for(auto& cell : cells) {
    if(!kept.contains(cell)) {
      transaction.remove(cell);
    }
  }
  reorder_children(document, transaction, m_grid, order);
  auto group = *document.get(m_grid).get_group();
  group.m_repeat_grid->m_column_count = columns;
  group.m_repeat_grid->m_row_count = rows;
  group.m_repeat_grid->m_size = size;
  transaction.replace_payload(m_grid, std::move(group));
  layout_repeat_grid(document, transaction, m_grid);
}

bool ResizeRepeatGridCommand::merge(const Command& command) {
  auto resize = dynamic_cast<const ResizeRepeatGridCommand*>(&command);
  if(!resize || resize->m_grid != m_grid) {
    return false;
  }
  m_size = resize->m_size;
  return true;
}

SetRepeatGridSpacingCommand::SetRepeatGridSpacingCommand(
  std::vector<NodeId> nodes, Orientation orientation, double spacing) noexcept
  : m_nodes(std::move(nodes)),
    m_orientation(orientation),
    m_spacing(spacing) {}

std::string SetRepeatGridSpacingCommand::get_label() const {
  return "Spacing";
}

void SetRepeatGridSpacingCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& id : get_top_level(document, m_nodes)) {
    if(!is_repeat_grid(document, id)) {
      continue;
    }
    auto group = *document.get(id).get_group();
    auto& grid = *group.m_repeat_grid;
    if(m_orientation == Orientation::HORIZONTAL) {
      auto spacing =
        std::max(m_spacing, MINIMUM_CELL - grid.m_cell_size.m_width);
      auto old_extent = get_repeat_extent(
        grid.m_column_count, grid.m_cell_size.m_width, grid.m_column_spacing);
      auto extent = get_repeat_extent(
        grid.m_column_count, grid.m_cell_size.m_width, spacing);
      grid.m_size.m_width += extent - old_extent;
      grid.m_column_spacing = spacing;
    } else {
      auto spacing =
        std::max(m_spacing, MINIMUM_CELL - grid.m_cell_size.m_height);
      auto old_extent = get_repeat_extent(
        grid.m_row_count, grid.m_cell_size.m_height, grid.m_row_spacing);
      auto extent = get_repeat_extent(
        grid.m_row_count, grid.m_cell_size.m_height, spacing);
      grid.m_size.m_height += extent - old_extent;
      grid.m_row_spacing = spacing;
    }
    if(group == *document.get(id).get_group()) {
      continue;
    }
    transaction.replace_payload(id, std::move(group));
    layout_repeat_grid(document, transaction, id);
  }
}

bool SetRepeatGridSpacingCommand::merge(const Command& command) {
  auto spacing = dynamic_cast<const SetRepeatGridSpacingCommand*>(&command);
  if(!spacing || spacing->m_nodes != m_nodes ||
      spacing->m_orientation != m_orientation) {
    return false;
  }
  m_spacing = spacing->m_spacing;
  return true;
}

std::unique_ptr<Command> Rellu::make_ungroup_grid_command(
    const Document& document, const std::vector<NodeId>& targets) {
  auto nodes = get_top_level(document, targets);
  if(nodes.empty() || !std::all_of(nodes.begin(), nodes.end(),
      [&] (const auto& id) { return is_repeat_grid(document, id); })) {
    return nullptr;
  }
  return std::make_unique<UngroupGridCommand>(std::move(nodes));
}

void Rellu::add_repeat_grid_commands(
    CommandRegistry& registry, TextMeasurer measurer) {
  registry.add("object.repeat_grid", [=] (const auto& document,
      const auto& targets) -> std::unique_ptr<Command> {
    auto nodes = get_repeatable(document, targets);
    if(nodes.empty() || std::any_of(nodes.begin(), nodes.end(),
        [&] (const auto& id) { return document.get(id).get_artboard(); }) ||
        std::all_of(nodes.begin(), nodes.end(),
          [&] (const auto& id) { return is_repeat_grid(document, id); })) {
      return nullptr;
    }
    return std::make_unique<RepeatGridCommand>(std::move(nodes), measurer);
  });
}

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"

using namespace Rellu;

namespace {
  constexpr auto DEFAULT_SQUARE = 8.0;
  constexpr auto COLUMN_ALPHA = 64 / 255.0;
  constexpr auto SQUARE_ALPHA = 128 / 255.0;
  constexpr auto SQUARE_GREY = 199 / 255.0;

  constexpr auto FALLBACK_COLUMNS = 12;
  constexpr auto FALLBACK_GUTTER = 16.0;
  constexpr auto FALLBACK_MARGIN = 140.0;

  GridOverride make_override(
      double width, int columns, double gutter, double margin) {
    auto color = Color(0, 1, 1, COLUMN_ALPHA);
    return GridOverride(width, columns, gutter, Margins(0, margin, 0, margin),
      color, color);
  }

  const GridOverride* find_override(
      const std::vector<GridOverride>& overrides, double width) {
    auto found = static_cast<const GridOverride*>(nullptr);
    for(auto& entry : overrides) {
      if(entry.m_maximum_width && std::isfinite(*entry.m_maximum_width) &&
          width <= *entry.m_maximum_width && (!found ||
            *entry.m_maximum_width < *found->m_maximum_width)) {
        found = &entry;
      }
    }
    return found;
  }

  double round_column(double width, int count, double margins) {
    auto whole = std::floor(width);
    auto fraction = width - whole;
    if(fraction <= 0.5 || (1 - fraction) * count > margins) {
      return whole;
    }
    return whole + 1;
  }

  void fit_margins(LayoutGrid& grid, double width) {
    auto& margins = grid.m_margins;
    auto count = std::max(grid.m_column_count, 1);
    auto space = width - margins.m_left - margins.m_right;
    if(count > 1) {
      space -= grid.m_gutter * (count - 1);
    }
    auto excess = space - std::max(1.0, round_column(space / count, count,
      margins.m_left + margins.m_right)) * count;
    auto is_even = margins.m_left == margins.m_right && count > 1;
    if(excess == 0 || (excess == 1 && is_even)) {
      return;
    }
    auto half = std::floor(excess / 2);
    margins.m_left += half;
    margins.m_right += is_even ? half : excess - half;
    if(margins.m_left < 0) {
      margins.m_right += margins.m_left;
      margins.m_left = 0;
    }
    if(margins.m_right < 0) {
      margins.m_left = std::max(0.0, margins.m_left + margins.m_right);
      margins.m_right = 0;
    }
  }

  void set_default_layout(
      LayoutGrid& grid, double width, const GridDefaults& defaults) {
    auto built_in = get_built_in_overrides();
    auto& overrides = defaults.m_overrides ? *defaults.m_overrides : built_in;
    auto fallback = make_override(std::numeric_limits<double>::infinity(),
      FALLBACK_COLUMNS, FALLBACK_GUTTER, FALLBACK_MARGIN);
    auto rule = find_override(overrides, width);
    if(!rule) {
      rule = &fallback;
    }
    grid.m_column_count = std::max(rule->m_column_count, 1);
    grid.m_gutter = std::max(rule->m_gutter, 0.0);
    grid.m_default_width = width;
    grid.m_margins = rule->m_margins;
    grid.m_column_color = rule->m_column_color;
    grid.m_row_color = rule->m_row_color;
    fit_margins(grid, width);
  }

  template<typename T>
  void assign(T& target, const std::optional<T>& source) {
    if(source) {
      target = *source;
    }
  }

  template<typename T>
  void assign(std::optional<T>& target, const std::optional<T>& source) {
    if(source) {
      target = source;
    }
  }

  void edit_grid(LayoutGrid& grid, const Artboard& artboard,
      const GridDefaults& defaults, const GridEdit& edit) {
    auto width = artboard.m_frame.m_width;
    if(edit.m_is_default.value_or(false)) {
      auto replacement = make_default_grid(artboard, defaults);
      replacement.m_is_visible = grid.m_is_visible;
      replacement.m_type = grid.m_type;
      grid = std::move(replacement);
    }
    assign(grid.m_is_visible, edit.m_is_visible);
    assign(grid.m_type, edit.m_type);
    if(edit.m_column_count) {
      grid.m_column_count = std::max(*edit.m_column_count, 1);
    }
    if(edit.m_gutter) {
      grid.m_gutter = std::max(*edit.m_gutter, 0.0);
    }
    if(edit.m_top) {
      grid.m_margins.m_top = std::max(*edit.m_top, 0.0);
    }
    if(edit.m_right) {
      grid.m_margins.m_right = std::max(*edit.m_right, 0.0);
    }
    if(edit.m_bottom) {
      grid.m_margins.m_bottom = std::max(*edit.m_bottom, 0.0);
    }
    if(edit.m_left) {
      grid.m_margins.m_left = std::max(*edit.m_left, 0.0);
    }
    if(edit.m_column_width) {
      auto count = std::max(grid.m_column_count, 1);
      auto columns = std::max(*edit.m_column_width, 1.0) * count +
        grid.m_gutter * (count - 1);
      auto margin = std::max((width - columns) / 2, 0.0);
      grid.m_margins.m_left = margin;
      grid.m_margins.m_right = margin;
    }
    assign(grid.m_column_color, edit.m_column_color);
    if(edit.m_square_size) {
      auto size = std::max(*edit.m_square_size, 1.0);
      grid.m_square_size = Size(size, size);
    }
    if(edit.m_square_color) {
      grid.m_square_column_color = *edit.m_square_color;
      grid.m_square_row_color = *edit.m_square_color;
    }
  }

  CommandFactory make_grid_toggle(GridType type) {
    return [=] (const auto& document, const auto& nodes) ->
        std::unique_ptr<Command> {
      auto artboards = get_target_artboards(document, nodes);
      if(artboards.empty()) {
        return nullptr;
      }
      auto is_shown = std::all_of(artboards.begin(), artboards.end(),
        [&] (const auto& id) {
          auto& grid = document.get(id).get_artboard()->m_layout_grid;
          return grid && grid->m_is_visible && grid->m_type == type;
        });
      auto edit = GridEdit();
      edit.m_is_visible = !is_shown;
      if(!is_shown) {
        edit.m_type = type;
      }
      return std::make_unique<SetGridCommand>(std::move(artboards), edit);
    };
  }
}

std::vector<GridOverride> Rellu::get_built_in_overrides() {
  return {make_override(312, 1, 0, 28), make_override(599, 4, 10, 36),
    make_override(834, 8, 16, 80), make_override(1024, 12, 16, 120)};
}

LayoutGrid Rellu::make_default_grid(
    const Artboard& artboard, const GridDefaults& defaults) {
  auto grid = LayoutGrid();
  grid.m_type = GridType::LAYOUT;
  if(auto& base = defaults.m_grid) {
    grid.m_square_size = base->m_square_size;
    grid.m_square_column_color = base->m_square_column_color;
    grid.m_square_row_color = base->m_square_row_color;
  } else {
    grid.m_square_size = Size(DEFAULT_SQUARE, DEFAULT_SQUARE);
    grid.m_square_column_color =
      Color(SQUARE_GREY, SQUARE_GREY, SQUARE_GREY, SQUARE_ALPHA);
    grid.m_square_row_color = grid.m_square_column_color;
  }
  set_default_layout(grid, artboard.m_frame.m_width, defaults);
  return grid;
}

GridDefaults Rellu::make_grid_defaults(const LayoutGrid& grid, double width,
    const GridDefaults& defaults) {
  auto result = GridDefaults();
  result.m_grid = grid;
  result.m_grid->m_default_width = width;
  result.m_grid->m_type = GridType::LAYOUT;
  result.m_grid->m_is_visible = false;
  auto overrides = defaults.m_overrides.value_or(get_built_in_overrides());
  auto entry = std::find_if(overrides.begin(), overrides.end(),
    [&] (const auto& entry) {
      return entry.m_maximum_width && *entry.m_maximum_width == width;
    });
  if(entry == overrides.end()) {
    overrides.push_back(GridOverride());
    entry = overrides.end() - 1;
    entry->m_maximum_width = width;
  }
  entry->m_column_count = grid.m_column_count;
  entry->m_gutter = grid.m_gutter;
  entry->m_margins = grid.m_margins;
  entry->m_row_color = grid.m_row_color;
  entry->m_column_color = grid.m_column_color;
  result.m_overrides = std::move(overrides);
  return result;
}

double Rellu::get_column_width(const LayoutGrid& grid, double width) {
  auto count = std::max(grid.m_column_count, 1);
  return std::floor((width - grid.m_margins.m_left - grid.m_margins.m_right -
    grid.m_gutter * (count - 1)) / count);
}

SetGridCommand::SetGridCommand(
  std::vector<NodeId> artboards, GridEdit edit) noexcept
  : m_artboards(std::move(artboards)),
    m_edit(std::move(edit)) {}

std::string SetGridCommand::get_label() const {
  return "Change Grid";
}

void SetGridCommand::apply(
    const Document& document, Transaction& transaction) {
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : m_artboards) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      continue;
    }
    auto artboard = node->get_artboard();
    if(!artboard) {
      continue;
    }
    auto payload = *artboard;
    auto& defaults = document.get_resources().m_grid_defaults;
    auto grid =
      payload.m_layout_grid.value_or(make_default_grid(payload, defaults));
    edit_grid(grid, payload, defaults, m_edit);
    payload.m_layout_grid = std::move(grid);
    if(payload != *artboard) {
      transaction.replace_payload(id, std::move(payload));
    }
  }
}

bool SetGridCommand::merge(const Command& command) {
  auto grid = dynamic_cast<const SetGridCommand*>(&command);
  if(!grid || grid->m_artboards != m_artboards) {
    return false;
  }
  auto& edit = grid->m_edit;
  if(edit.m_is_default.value_or(false)) {
    m_edit.m_column_count = std::nullopt;
    m_edit.m_gutter = std::nullopt;
    m_edit.m_column_width = std::nullopt;
    m_edit.m_top = std::nullopt;
    m_edit.m_right = std::nullopt;
    m_edit.m_bottom = std::nullopt;
    m_edit.m_left = std::nullopt;
  }
  assign(m_edit.m_is_default, edit.m_is_default);
  assign(m_edit.m_is_visible, edit.m_is_visible);
  assign(m_edit.m_type, edit.m_type);
  assign(m_edit.m_column_count, edit.m_column_count);
  assign(m_edit.m_gutter, edit.m_gutter);
  assign(m_edit.m_column_width, edit.m_column_width);
  assign(m_edit.m_top, edit.m_top);
  assign(m_edit.m_right, edit.m_right);
  assign(m_edit.m_bottom, edit.m_bottom);
  assign(m_edit.m_left, edit.m_left);
  assign(m_edit.m_column_color, edit.m_column_color);
  assign(m_edit.m_square_size, edit.m_square_size);
  assign(m_edit.m_square_color, edit.m_square_color);
  return true;
}

SetViewportCommand::SetViewportCommand(std::vector<NodeId> artboards,
  std::optional<double> height, bool is_scrolling) noexcept
  : m_artboards(std::move(artboards)),
    m_height(height),
    m_is_scrolling(is_scrolling) {}

std::string SetViewportCommand::get_label() const {
  return "Change Scrolling";
}

void SetViewportCommand::apply(
    const Document& document, Transaction& transaction) {
  auto visited = std::unordered_set<NodeId>();
  for(auto& id : m_artboards) {
    auto node = document.find(id);
    if(!node || !visited.insert(id).second) {
      continue;
    }
    auto artboard = node->get_artboard();
    if(!artboard) {
      continue;
    }
    auto payload = *artboard;
    if(!m_is_scrolling) {
      payload.m_viewport_height = std::nullopt;
    } else if(m_height) {
      payload.m_viewport_height = std::max(*m_height, 1.0);
    } else if(!payload.m_viewport_height) {
      payload.m_viewport_height = payload.m_frame.m_height;
    }
    if(payload != *artboard) {
      transaction.replace_payload(id, std::move(payload));
    }
  }
}

bool SetViewportCommand::merge(const Command& command) {
  auto viewport = dynamic_cast<const SetViewportCommand*>(&command);
  if(!viewport || viewport->m_artboards != m_artboards) {
    return false;
  }
  m_is_scrolling = viewport->m_is_scrolling;
  if(viewport->m_height || !m_is_scrolling) {
    m_height = viewport->m_height;
  }
  return true;
}

std::vector<NodeId> Rellu::get_target_artboards(
    const Document& document, const std::vector<NodeId>& nodes) {
  auto artboards = std::vector<NodeId>();
  for(auto& id : nodes) {
    if(!document.find(id)) {
      continue;
    }
    auto artboard = document.get(id).get_artboard() ? id :
      find_artboard(document, id);
    if(!artboard.is_nil() && std::find(artboards.begin(), artboards.end(),
        artboard) == artboards.end()) {
      artboards.push_back(artboard);
    }
  }
  if(!artboards.empty()) {
    return artboards;
  }
  for(auto& id : document.get_canvas()) {
    if(document.get(id).get_artboard()) {
      artboards.push_back(id);
    }
  }
  return artboards;
}

MakeGridDefaultCommand::MakeGridDefaultCommand(const NodeId& artboard) noexcept
  : m_artboard(artboard) {}

std::string MakeGridDefaultCommand::get_label() const {
  return "Make Default";
}

void MakeGridDefaultCommand::apply(
    const Document& document, Transaction& transaction) {
  auto node = document.find(m_artboard);
  auto artboard = node ? node->get_artboard() : nullptr;
  if(!artboard) {
    throw std::invalid_argument("Unknown artboard.");
  }
  auto& defaults = document.get_resources().m_grid_defaults;
  auto grid = artboard->m_layout_grid.value_or(
    make_default_grid(*artboard, defaults));
  auto replacement =
    make_grid_defaults(grid, artboard->m_frame.m_width, defaults);
  if(replacement != defaults) {
    transaction.set_grid_defaults(std::move(replacement));
  }
}

void Rellu::add_artboard_commands(CommandRegistry& registry) {
  registry.add("view.layout_grid", make_grid_toggle(GridType::LAYOUT));
  registry.add("view.square_grid", make_grid_toggle(GridType::SQUARE));
}

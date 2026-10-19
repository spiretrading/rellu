#include <algorithm>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"

using namespace Rellu;

namespace {
  constexpr auto DEFAULT_COLUMNS = 4;
  constexpr auto DEFAULT_GUTTER = 10.0;
  constexpr auto DEFAULT_MARGIN = 37.0;
  constexpr auto DEFAULT_SQUARE = 8.0;
  constexpr auto COLUMN_ALPHA = 64 / 255.0;
  constexpr auto SQUARE_ALPHA = 128 / 255.0;
  constexpr auto SQUARE_GREY = 199 / 255.0;

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

  void edit_grid(LayoutGrid& grid, double width, const GridEdit& edit) {
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

LayoutGrid Rellu::make_default_grid(const Artboard& artboard) {
  auto grid = LayoutGrid();
  grid.m_type = GridType::LAYOUT;
  grid.m_column_count = DEFAULT_COLUMNS;
  grid.m_gutter = DEFAULT_GUTTER;
  grid.m_default_width = artboard.m_frame.m_width;
  grid.m_margins.m_left = DEFAULT_MARGIN;
  grid.m_margins.m_right = DEFAULT_MARGIN;
  grid.m_column_color = Color(0, 1, 1, COLUMN_ALPHA);
  grid.m_row_color = Color(0, 1, 1, COLUMN_ALPHA);
  grid.m_square_size = Size(DEFAULT_SQUARE, DEFAULT_SQUARE);
  grid.m_square_column_color =
    Color(SQUARE_GREY, SQUARE_GREY, SQUARE_GREY, SQUARE_ALPHA);
  grid.m_square_row_color = grid.m_square_column_color;
  return grid;
}

double Rellu::get_column_width(const LayoutGrid& grid, double width) {
  auto count = std::max(grid.m_column_count, 1);
  return (width - grid.m_margins.m_left - grid.m_margins.m_right -
    grid.m_gutter * (count - 1)) / count;
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
    auto grid = payload.m_layout_grid.value_or(make_default_grid(payload));
    edit_grid(grid, payload.m_frame.m_width, m_edit);
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

void Rellu::add_artboard_commands(CommandRegistry& registry) {
  registry.add("view.layout_grid", make_grid_toggle(GridType::LAYOUT));
  registry.add("view.square_grid", make_grid_toggle(GridType::SQUARE));
}

#include <algorithm>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Tools/ArtboardGrids.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_CELL = 4.0;
  constexpr auto MAXIMUM_LINES = 2000;

  void draw_columns(const LayoutGrid& grid, const Rect& frame,
      const Matrix& view, CanvasOverlay& overlay) {
    auto count = std::max(grid.m_column_count, 1);
    auto& margins = grid.m_margins;
    auto width = (frame.m_width - margins.m_left - margins.m_right -
      grid.m_gutter * (count - 1)) / count;
    if(!(width > 0)) {
      return;
    }
    auto top = frame.m_y + margins.m_top;
    auto height = frame.m_height - margins.m_top - margins.m_bottom;
    for(auto i = 0; i != count; ++i) {
      auto x = frame.m_x + margins.m_left + i * (width + grid.m_gutter);
      auto column = view.map(Rect(x, top, width, height));
      auto shape = OverlayShape();
      shape.m_center = column.get_center();
      shape.m_size = column.get_size();
      shape.m_fill = grid.m_column_color;
      shape.m_is_snapped = false;
      overlay.m_shapes.push_back(shape);
    }
  }

  void draw_squares(const LayoutGrid& grid, const Rect& frame,
      const Matrix& view, CanvasOverlay& overlay) {
    auto cell = grid.m_square_size;
    if(!(cell.m_width > 0) || !(cell.m_height > 0)) {
      return;
    }
    auto screen = view.map(frame);
    auto scale = frame.m_width > 0 ? screen.m_width / frame.m_width : 0;
    if(cell.m_width * scale < MINIMUM_CELL ||
        cell.m_height * scale < MINIMUM_CELL ||
        frame.m_width / cell.m_width + frame.m_height / cell.m_height >
          MAXIMUM_LINES) {
      return;
    }
    for(auto x = cell.m_width; x < frame.m_width; x += cell.m_width) {
      auto start = view.map(Point(frame.m_x + x, frame.m_y));
      auto end = view.map(Point(frame.m_x + x, frame.m_y + frame.m_height));
      overlay.m_shapes.push_back(
        make_overlay_line(start, end, 1, grid.m_square_column_color));
    }
    for(auto y = cell.m_height; y < frame.m_height; y += cell.m_height) {
      auto start = view.map(Point(frame.m_x, frame.m_y + y));
      auto end = view.map(Point(frame.m_x + frame.m_width, frame.m_y + y));
      overlay.m_shapes.push_back(
        make_overlay_line(start, end, 1, grid.m_square_row_color));
    }
  }
}

void Rellu::draw_artboard_grids(const Document& document,
    const GridSettings& settings, const Matrix& view, Size size,
    CanvasOverlay& overlay) {
  if(!is_grid_visible(settings)) {
    return;
  }
  auto bounds = Rect(0, 0, size.m_width, size.m_height);
  for(auto& id : document.get_canvas()) {
    auto node = document.find(id);
    if(!node || !node->m_header.m_is_visible) {
      continue;
    }
    auto artboard = node->get_artboard();
    if(!artboard || !view.map(artboard->m_frame).intersects(bounds)) {
      continue;
    }
    auto grid = artboard->m_layout_grid.value_or(make_initial_grid(
      *artboard, document.get_resources().m_grid_defaults));
    if(settings.m_is_layout_visible) {
      draw_columns(grid, artboard->m_frame, view, overlay);
    }
    if(settings.m_is_square_visible) {
      draw_squares(grid, artboard->m_frame, view, overlay);
    }
  }
}

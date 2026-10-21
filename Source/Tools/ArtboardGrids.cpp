#include <algorithm>
#include <vector>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Tools/ArtboardGrids.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_CELL = 4.0;
  constexpr auto MAXIMUM_LINES = 2000;
  constexpr auto BAND_DEPTH = 1.0;
  constexpr auto BAND_WIDTH = 4.0 / 9;
  constexpr auto CAP_HEIGHT = 1.0;

  void add_fill(const Rect& rect, Color color, CanvasOverlay& overlay) {
    auto shape = OverlayShape();
    shape.m_center = rect.get_center();
    shape.m_size = rect.get_size();
    shape.m_fill = color;
    shape.m_is_snapped = false;
    overlay.m_shapes.push_back(shape);
  }

  void add_band(const Rect& column, Color color, CanvasOverlay& overlay) {
    if(!(column.m_width > 2 * BAND_DEPTH) ||
        !(column.m_height > 2 * BAND_DEPTH)) {
      return;
    }
    auto inset = BAND_DEPTH - BAND_WIDTH;
    auto height = column.m_height - 2 * inset;
    auto width = column.m_width - 2 * BAND_DEPTH;
    add_fill(Rect(column.m_x + inset, column.m_y + inset, BAND_WIDTH, height),
      color, overlay);
    add_fill(Rect(column.get_right() - BAND_DEPTH, column.m_y + inset,
      BAND_WIDTH, height), color, overlay);
    add_fill(Rect(column.m_x + BAND_DEPTH, column.m_y + inset, width,
      BAND_WIDTH), color, overlay);
    add_fill(Rect(column.m_x + BAND_DEPTH,
      column.get_bottom() - BAND_DEPTH, width, BAND_WIDTH), color, overlay);
  }

  void add_caps(const Rect& left, const Rect& right, Color color,
      CanvasOverlay& overlay) {
    auto width = right.m_x - left.get_right();
    if(!(width > 0) || !(left.m_height > 2 * CAP_HEIGHT)) {
      return;
    }
    add_fill(Rect(left.get_right(), left.m_y, width, CAP_HEIGHT), color,
      overlay);
    add_fill(Rect(left.get_right(), left.get_bottom() - CAP_HEIGHT, width,
      CAP_HEIGHT), color, overlay);
  }

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
    auto columns = std::vector<Rect>();
    for(auto i = 0; i != count; ++i) {
      auto x = frame.m_x + margins.m_left + i * (width + grid.m_gutter);
      columns.push_back(view.map(Rect(x, top, width, height)));
      add_fill(columns.back(), grid.m_column_color, overlay);
    }
    for(auto& column : columns) {
      add_band(column, grid.m_column_color, overlay);
    }
    for(auto i = std::size_t(1); i < columns.size(); ++i) {
      add_caps(columns[i - 1], columns[i], grid.m_column_color, overlay);
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

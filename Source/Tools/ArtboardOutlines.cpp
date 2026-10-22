#include <cmath>
#include <unordered_set>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Tools/ArtboardOutlines.hpp"

using namespace Rellu;

namespace {
  constexpr auto SNAP_TOLERANCE = 1e-6;

  void add_outline(Point top_left, Point bottom_right, double ratio,
      Color color, CanvasOverlay& overlay) {
    auto add = [&] (double left, double top, double right, double bottom) {
      auto shape = OverlayShape();
      shape.m_center = Point((left + right) / 2, (top + bottom) / 2);
      shape.m_size = Size(right - left, bottom - top);
      shape.m_fill = color;
      shape.m_is_snapped = false;
      overlay.m_shapes.push_back(shape);
    };
    auto lower = [&] (double position) {
      return std::floor(position * ratio + SNAP_TOLERANCE) / ratio;
    };
    auto upper = [&] (double position) {
      return std::ceil(position * ratio - SNAP_TOLERANCE) / ratio;
    };
    auto width = ARTBOARD_OUTLINE_WIDTH;
    auto left = top_left.m_x - width;
    auto right = bottom_right.m_x + width;
    auto inner_left = lower(top_left.m_x);
    auto inner_top = lower(top_left.m_y);
    auto inner_right = upper(bottom_right.m_x);
    auto inner_bottom = upper(bottom_right.m_y);
    add(left, top_left.m_y - width, right, inner_top);
    add(left, inner_bottom, right, bottom_right.m_y + width);
    add(left, inner_top, inner_left, inner_bottom);
    add(inner_right, inner_top, right, inner_bottom);
  }
}

void Rellu::draw_artboard_outlines(const Document& document,
    const Matrix& view, double ratio, const std::vector<NodeId>& selection,
    CanvasOverlay& overlay) {
  auto focused = std::unordered_set<NodeId>();
  for(auto& id : selection) {
    if(auto artboard = find_artboard(document, id); !artboard.is_nil()) {
      focused.insert(artboard);
    }
  }
  for(auto& id : document.get_canvas()) {
    auto& node = document.get(id);
    auto artboard = node.get_artboard();
    if(!artboard || !node.m_header.m_is_visible) {
      continue;
    }
    auto frame = view.map(artboard->m_frame);
    auto color = ARTBOARD_OUTLINE_COLOR;
    if(focused.contains(id)) {
      color = FOCUSED_ARTBOARD_OUTLINE_COLOR;
    }
    add_outline(frame.get_top_left(), frame.get_bottom_right(), ratio, color,
      overlay);
  }
}

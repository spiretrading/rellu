#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Polyline.hpp"
#include "Rellu/Tools/PathPoints.hpp"

using namespace Rellu;

namespace {
  constexpr auto PATH_COLOR = make_color(38, 128, 235);
  constexpr auto WHITE = make_color(255, 255, 255);
  constexpr auto LINE_WIDTH = 1.0;
  constexpr auto ANCHOR_SIZE = 7.0;
  constexpr auto HANDLE_SIZE = 7.0;
  constexpr auto FLATNESS = 0.25;

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

  Point get_handle(const Anchor& anchor, HandleSide side) {
    if(side == HandleSide::IN) {
      return anchor.m_in;
    }
    return anchor.m_out;
  }

  Matrix get_view_transform(
      const ToolContext& context, const EditablePath& path) {
    return context.get_view() * path.m_transform;
  }

  void draw_polyline(const Path& path, CanvasOverlay& overlay) {
    for(auto& polyline : flatten(path, FLATNESS)) {
      auto& points = polyline.m_points;
      for(auto i = std::size_t(1); i < points.size(); ++i) {
        draw_path_line(points[i - 1], points[i], overlay);
      }
      if(polyline.m_is_closed && points.size() > 1) {
        draw_path_line(points.back(), points.front(), overlay);
      }
    }
  }

  void draw_handle(Point anchor, Point handle, CanvasOverlay& overlay) {
    if(anchor == handle) {
      return;
    }
    draw_path_line(anchor, handle, overlay);
    overlay.m_shapes.push_back(OverlayShape(handle,
      Size(HANDLE_SIZE, HANDLE_SIZE), 0, HANDLE_SIZE / 2, WHITE, PATH_COLOR,
      LINE_WIDTH));
  }
}

bool Rellu::is_point_editable(const Document& document, const NodeId& id) {
  auto node = document.find(id);
  if(!node || !node->m_header.m_is_visible || is_locked(document, id)) {
    return false;
  }
  auto shape = node->get_shape();
  return shape && !std::holds_alternative<CompoundShape>(shape->m_geometry);
}

std::optional<EditablePath> Rellu::load_path(
    const Document& document, const NodeId& id) {
  if(!is_point_editable(document, id)) {
    return std::nullopt;
  }
  auto& geometry = document.get(id).get_shape()->m_geometry;
  auto path = EditablePath();
  path.m_node = id;
  path.m_contours = to_contours(to_path(geometry));
  if(auto shape = std::get_if<PathShape>(&geometry)) {
    path.m_fill_rule = shape->m_fill_rule;
  }
  path.m_transform = get_world_transform(document, id);
  return path;
}

std::unique_ptr<SetPathCommand> Rellu::make_path_command(
    std::string label, const EditablePath& path) {
  return std::make_unique<SetPathCommand>(std::move(label), path.m_node,
    PathShape(Shared(to_path(path.m_contours, path.m_fill_rule)),
      path.m_fill_rule));
}

Anchor Rellu::get_anchor(const EditablePath& path, const AnchorIndex& index) {
  auto& anchor =
    path.m_contours[index.m_contour].m_anchors[index.m_anchor];
  return Anchor(path.m_transform.map(anchor.m_point),
    path.m_transform.map(anchor.m_in), path.m_transform.map(anchor.m_out));
}

std::optional<AnchorIndex> Rellu::find_anchor(const ToolContext& context,
    const EditablePath& path, Point position, double reach) {
  auto view = get_view_transform(context, path);
  auto nearest = std::optional<AnchorIndex>();
  auto nearest_distance = reach;
  for(auto i = std::size_t(0); i != path.m_contours.size(); ++i) {
    auto& anchors = path.m_contours[i].m_anchors;
    for(auto j = std::size_t(0); j != anchors.size(); ++j) {
      auto d = distance(view.map(anchors[j].m_point), position);
      if(d <= nearest_distance) {
        nearest = AnchorIndex(i, j);
        nearest_distance = d;
      }
    }
  }
  return nearest;
}

std::optional<HandleIndex> Rellu::find_handle(const ToolContext& context,
    const EditablePath& path, const std::vector<AnchorIndex>& anchors,
    Point position, double reach) {
  auto view = get_view_transform(context, path);
  auto nearest = std::optional<HandleIndex>();
  auto nearest_distance = reach;
  for(auto& index : anchors) {
    if(index.m_contour >= path.m_contours.size() || index.m_anchor >=
        path.m_contours[index.m_contour].m_anchors.size()) {
      continue;
    }
    auto& anchor =
      path.m_contours[index.m_contour].m_anchors[index.m_anchor];
    for(auto side : {HandleSide::IN, HandleSide::OUT}) {
      auto handle = get_handle(anchor, side);
      if(handle == anchor.m_point) {
        continue;
      }
      auto d = distance(view.map(handle), position);
      if(d <= nearest_distance) {
        nearest = HandleIndex(index, side);
        nearest_distance = d;
      }
    }
  }
  return nearest;
}

std::optional<SegmentPosition> Rellu::find_segment(const ToolContext& context,
    const EditablePath& path, Point position, double reach) {
  auto view = get_view_transform(context, path);
  auto nearest = std::optional<SegmentPosition>();
  auto nearest_distance = reach;
  for(auto i = std::size_t(0); i != path.m_contours.size(); ++i) {
    auto contour = transform(path.m_contours[i], view);
    if(auto found = find_nearest(contour, position)) {
      auto d = distance(found->m_point, position);
      if(d <= nearest_distance) {
        nearest = SegmentPosition(i, *found);
        nearest_distance = d;
      }
    }
  }
  return nearest;
}

Point Rellu::constrain_to_45_degrees(Point delta) {
  if(delta == Point(0, 0)) {
    return delta;
  }
  auto step = std::numbers::pi / 4;
  auto index = std::round(std::atan2(delta.m_y, delta.m_x) / step);
  auto direction = Point(std::cos(index * step), std::sin(index * step));
  if(std::abs(direction.m_x) < 1e-12) {
    direction.m_x = 0;
  }
  if(std::abs(direction.m_y) < 1e-12) {
    direction.m_y = 0;
  }
  return direction * dot(delta, direction);
}

void Rellu::draw_path(const ToolContext& context, const EditablePath& path,
    const std::vector<AnchorIndex>& selection,
    const std::vector<AnchorIndex>& handles, CanvasOverlay& overlay) {
  auto view = get_view_transform(context, path);
  auto contours = std::vector<Contour>();
  for(auto& contour : path.m_contours) {
    contours.push_back(transform(contour, view));
  }
  draw_polyline(to_path(contours, path.m_fill_rule), overlay);
  for(auto& index : handles) {
    if(index.m_contour >= contours.size() ||
        index.m_anchor >= contours[index.m_contour].m_anchors.size()) {
      continue;
    }
    auto& anchor = contours[index.m_contour].m_anchors[index.m_anchor];
    draw_handle(anchor.m_point, anchor.m_in, overlay);
    draw_handle(anchor.m_point, anchor.m_out, overlay);
  }
  for(auto i = std::size_t(0); i != contours.size(); ++i) {
    auto& anchors = contours[i].m_anchors;
    for(auto j = std::size_t(0); j != anchors.size(); ++j) {
      auto is_selected = std::find(selection.begin(), selection.end(),
        AnchorIndex(i, j)) != selection.end();
      overlay.m_shapes.push_back(OverlayShape(anchors[j].m_point,
        Size(ANCHOR_SIZE, ANCHOR_SIZE), 0, 0,
        is_selected ? PATH_COLOR : WHITE, PATH_COLOR, LINE_WIDTH));
    }
  }
}

void Rellu::draw_path_line(Point start, Point end, CanvasOverlay& overlay) {
  auto length = distance(start, end);
  if(length == 0) {
    return;
  }
  overlay.m_shapes.push_back(
    make_overlay_line(start, end, LINE_WIDTH, PATH_COLOR));
}

void Rellu::draw_path_curve(
    const Anchor& anchor, const Anchor& end, CanvasOverlay& overlay) {
  auto contour = Contour();
  contour.m_anchors.push_back(anchor);
  contour.m_anchors.push_back(end);
  draw_polyline(to_path(std::span(&contour, 1), FillRule::NON_ZERO), overlay);
}

#include <algorithm>
#include <cmath>
#include <variant>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto LINE_SPACING = 1.2;
  constexpr auto ASCENT = 0.8;
  constexpr auto CHARACTER_WIDTH = 0.5;

  struct Accumulator {
    std::optional<Rect> m_bounds;

    void add(const Rect& rect) {
      if(!m_bounds) {
        m_bounds = rect;
        return;
      }
      auto left = std::min(m_bounds->get_left(), rect.get_left());
      auto top = std::min(m_bounds->get_top(), rect.get_top());
      auto right = std::max(m_bounds->get_right(), rect.get_right());
      auto bottom = std::max(m_bounds->get_bottom(), rect.get_bottom());
      m_bounds = Rect(left, top, right - left, bottom - top);
    }
  };

  struct Placement {
    const Node* m_source;
    const std::vector<NodeId>* m_children;
    Matrix m_transform;
  };

  struct Measurement {
    const Document* m_document;
    BoundsType m_type;
    const TextMeasurer* m_measurer;
    Accumulator m_accumulator;
  };

  double get_scale(const Matrix& matrix) {
    auto sum = (matrix.m_a * matrix.m_a + matrix.m_b * matrix.m_b +
      matrix.m_c * matrix.m_c + matrix.m_d * matrix.m_d) / 2;
    auto determinant = matrix.get_determinant();
    return std::sqrt(
      sum + std::sqrt(std::max(0.0, sum * sum - determinant * determinant)));
  }

  bool is_open(const ShapeGeometry& geometry) {
    if(std::holds_alternative<LineShape>(geometry)) {
      return true;
    } else if(auto path = std::get_if<PathShape>(&geometry)) {
      auto& segments = path->m_path->get_segments();
      return std::none_of(segments.begin(), segments.end(),
        [] (const auto& segment) {
          return segment.m_type == SegmentType::CLOSE;
        });
    }
    return false;
  }

  double get_stroke_extent(const std::optional<Stroke>& stroke, bool is_open) {
    if(!stroke || !stroke->m_is_enabled || !(stroke->m_width > 0)) {
      return 0;
    }
    if(is_open || stroke->m_alignment == StrokeAlignment::CENTER) {
      return stroke->m_width / 2;
    } else if(stroke->m_alignment == StrokeAlignment::OUTSIDE) {
      return stroke->m_width;
    }
    return 0;
  }

  std::optional<Placement> place(const Document& document, const Node& node) {
    if(node.get_reference()) {
      auto resolution = resolve_reference(document.get_nodes(), node.get_id());
      if(!resolution) {
        return std::nullopt;
      }
      return Placement(&document.get(resolution->m_source),
        &document.get(resolution->m_child_source).m_children,
        resolution->m_transform);
    } else if(auto artboard = node.get_artboard()) {
      return Placement(&node, &node.m_children,
        make_translation(artboard->m_frame.m_x, artboard->m_frame.m_y));
    }
    return Placement(&node, &node.m_children, node.m_header.m_transform);
  }

  void add_styled(Measurement& measurement, const Rect& bounds,
      const Style& style, bool is_open, const Matrix& matrix) {
    auto extent = 0.0;
    if(measurement.m_type == BoundsType::VISUAL) {
      extent = get_stroke_extent(style.m_stroke, is_open) * get_scale(matrix);
    }
    measurement.m_accumulator.add(inflate(bounds, extent));
  }

  void add_content(Measurement& measurement, const Node& source,
    const std::vector<NodeId>& children, const Matrix& matrix, int depth);

  void add_child(Measurement& measurement, const NodeId& id,
      const Matrix& parent, int depth) {
    if(depth > MAXIMUM_DEPTH) {
      return;
    }
    auto node = measurement.m_document->find(id);
    if(!node) {
      return;
    }
    auto placement = place(*measurement.m_document, *node);
    if(!placement || !placement->m_source->m_header.m_is_visible) {
      return;
    }
    add_content(measurement, *placement->m_source, *placement->m_children,
      parent * placement->m_transform, depth + 1);
  }

  void add_content(Measurement& measurement, const Node& source,
      const std::vector<NodeId>& children, const Matrix& matrix, int depth) {
    if(auto artboard = source.get_artboard()) {
      measurement.m_accumulator.add(matrix.map(
        Rect(0, 0, artboard->m_frame.m_width, artboard->m_frame.m_height)));
    } else if(auto shape = source.get_shape()) {
      auto path = to_path(shape->m_geometry);
      if(!path.is_empty()) {
        add_styled(measurement, get_bounds(path, matrix), shape->m_style,
          is_open(shape->m_geometry), matrix);
      }
    } else if(auto text = source.get_text()) {
      add_styled(measurement, matrix.map((*measurement.m_measurer)(*text)),
        text->m_style, false, matrix);
    } else if(auto group = source.get_group();
        group && group->m_mask && !group->m_mask->m_shapes.empty()) {
      for(auto& shape : group->m_mask->m_shapes) {
        auto path = to_path(shape.m_geometry);
        if(!path.is_empty()) {
          measurement.m_accumulator.add(
            get_bounds(path, matrix * shape.m_transform));
        }
      }
    } else {
      for(auto& child : children) {
        add_child(measurement, child, matrix, depth);
      }
    }
  }
}

Rect Rellu::estimate_text_bounds(const Text& text) {
  auto size = text.m_base_style.m_font.m_size;
  if(!text.m_runs.empty()) {
    size = 0;
    for(auto& run : text.m_runs) {
      size = std::max(size, run.m_style.m_font.m_size);
    }
  }
  auto line_height =
    text.m_paragraph_style.m_line_height.value_or(LINE_SPACING * size);
  auto line_count = std::size_t(1);
  auto longest = std::size_t(0);
  auto length = std::size_t(0);
  for(auto character : *text.m_content) {
    if(character == u'\n') {
      ++line_count;
      longest = std::max(longest, length);
      length = 0;
    } else {
      ++length;
    }
  }
  longest = std::max(longest, length);
  if(text.m_layout) {
    auto stored_count = std::size_t(0);
    for(auto& paragraph : (*text.m_layout)->m_paragraphs) {
      stored_count += paragraph.m_lines.size();
    }
    if(stored_count != 0) {
      line_count = stored_count;
    }
  }
  auto height = static_cast<double>(line_count) * line_height;
  if(auto area = std::get_if<AreaFrame>(&text.m_frame)) {
    return Rect(0, 0, area->m_size.m_width, area->m_size.m_height);
  } else if(auto frame = std::get_if<AutoHeightFrame>(&text.m_frame)) {
    return Rect(0, 0, frame->m_width, height);
  }
  auto width = static_cast<double>(longest) * CHARACTER_WIDTH * size;
  auto left = 0.0;
  if(text.m_paragraph_style.m_alignment == TextAlignment::CENTER) {
    left = -width / 2;
  } else if(text.m_paragraph_style.m_alignment == TextAlignment::RIGHT) {
    left = -width;
  }
  auto top = -ASCENT * size;
  return Rect(left, top, width, height);
}

Matrix Rellu::get_transform(const Document& document, const NodeId& id) {
  auto& node = document.get(id);
  if(auto placement = place(document, node)) {
    return placement->m_transform;
  }
  return node.m_header.m_transform;
}

Matrix Rellu::get_world_transform(const Document& document, const NodeId& id) {
  auto transform = Matrix();
  auto current = id;
  for(auto depth = 0; !current.is_nil() && depth <= MAXIMUM_DEPTH; ++depth) {
    auto node = document.find(current);
    if(!node) {
      break;
    }
    transform = get_transform(document, current) * transform;
    current = node->m_parent;
  }
  return transform;
}

Matrix Rellu::get_parent_transform(
    const Document& document, const NodeId& id) {
  return get_world_transform(document, document.get(id).m_parent);
}

std::optional<Rect> Rellu::get_bounds(const Document& document,
    const NodeId& id, const Matrix& matrix, BoundsType type,
    const TextMeasurer& measurer) {
  auto node = document.find(id);
  if(!node) {
    return std::nullopt;
  }
  auto placement = place(document, *node);
  if(!placement) {
    return std::nullopt;
  }
  auto measurement = Measurement(&document, type, &measurer, Accumulator());
  add_content(
    measurement, *placement->m_source, *placement->m_children, matrix, 0);
  return measurement.m_accumulator.m_bounds;
}

std::optional<Rect> Rellu::get_local_bounds(const Document& document,
    const NodeId& id, BoundsType type, const TextMeasurer& measurer) {
  return get_bounds(document, id, Matrix(), type, measurer);
}

std::optional<Rect> Rellu::get_world_bounds(const Document& document,
    const NodeId& id, BoundsType type, const TextMeasurer& measurer) {
  if(!document.find(id)) {
    return std::nullopt;
  }
  return get_bounds(
    document, id, get_world_transform(document, id), type, measurer);
}

std::optional<Rect> Rellu::get_world_bounds(const Document& document,
    const std::vector<NodeId>& nodes, BoundsType type,
    const TextMeasurer& measurer) {
  auto accumulator = Accumulator();
  for(auto& id : nodes) {
    if(auto bounds = get_world_bounds(document, id, type, measurer)) {
      accumulator.add(*bounds);
    }
  }
  return accumulator.m_bounds;
}

Matrix Rellu::make_rect_mapping(const Rect& from, const Rect& to) {
  auto x_scale = 1.0;
  if(from.m_width != 0) {
    x_scale = to.m_width / from.m_width;
  }
  auto y_scale = 1.0;
  if(from.m_height != 0) {
    y_scale = to.m_height / from.m_height;
  }
  return Matrix(x_scale, 0, 0, y_scale, to.m_x - x_scale * from.m_x,
    to.m_y - y_scale * from.m_y);
}

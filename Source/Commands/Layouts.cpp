#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include "Rellu/Commands/Layouts.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"

using namespace Rellu;

namespace {
  constexpr auto TOLERANCE = 1e-6;
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto MAXIMUM_CELLS = 100000;
  constexpr auto LAYOUT_CHANGES = ChangeMask::TRANSFORM |
    ChangeMask::GEOMETRY | ChangeMask::TEXT | ChangeMask::CHILDREN |
    ChangeMask::VISIBILITY | ChangeMask::ROLES | ChangeMask::REFERENCE;
  constexpr auto CONTENT_CHANGES = ChangeMask::GEOMETRY | ChangeMask::TEXT |
    ChangeMask::CHILDREN | ChangeMask::ROLES | ChangeMask::REFERENCE;

  struct Context {
    const Document* m_document;
    Transaction* m_transaction;
    const TextMeasurer* m_measurer;
    bool m_is_changed;
  };

  struct Baseline {
    std::unordered_map<NodeId, Rect> m_bounds;
    std::vector<NodeId> m_order;
    std::map<NodeId, double> m_spacing;
    std::optional<Rect> m_background;
    std::optional<Rect> m_content;
    const Node* m_background_record = nullptr;
  };

  struct StackPlan {
    std::optional<Baseline> m_baseline;
    std::unordered_set<NodeId> m_moved;
    std::unordered_set<NodeId> m_floating;
  };

  struct Entry {
    int m_depth = 0;
    bool m_is_changed = false;
    std::vector<NodeId> m_cells;
    std::unordered_set<NodeId> m_translated;
    std::unordered_set<NodeId> m_reshaped;
  };

  struct Analysis {
    std::unordered_map<NodeId, ChangeMask> m_masks;
    std::unordered_set<NodeId> m_arrived;
    std::unordered_set<NodeId> m_containing;
    std::unordered_map<NodeId, Entry> m_entries;
  };

  struct Originals {
    std::unordered_map<NodeId, const Node*> m_records;
    std::unordered_set<NodeId> m_dirty;
  };

  bool is_near(double left, double right) {
    return std::abs(left - right) <= TOLERANCE;
  }

  bool is_near(const Rect& left, const Rect& right) {
    return is_near(left.m_x, right.m_x) && is_near(left.m_y, right.m_y) &&
      is_near(left.m_width, right.m_width) &&
      is_near(left.m_height, right.m_height);
  }

  bool is_near(const Matrix& left, const Matrix& right) {
    return is_near(left.m_a, right.m_a) && is_near(left.m_b, right.m_b) &&
      is_near(left.m_c, right.m_c) && is_near(left.m_d, right.m_d) &&
      is_near(left.m_tx, right.m_tx) && is_near(left.m_ty, right.m_ty);
  }

  bool has_layout(const Node& node) {
    auto group = node.get_group();
    return group &&
      (group->m_stack || group->m_padding || group->m_repeat_grid);
  }

  bool is_shown(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    if(!node) {
      return false;
    }
    if(node->get_reference()) {
      auto resolution = resolve_reference(document.get_nodes(), id);
      if(!resolution) {
        return false;
      }
      auto source = document.find(resolution->m_source);
      return source && source->m_header.m_is_visible;
    }
    return node->m_header.m_is_visible;
  }

  std::optional<Rect> measure_child(const Document& document,
      const NodeId& id, const TextMeasurer& measurer) {
    if(!document.find(id)) {
      return std::nullopt;
    }
    return get_bounds(document, id, get_transform(document, id),
      BoundsType::GEOMETRY, measurer);
  }

  std::optional<Rect> unite(
      const std::optional<Rect>& left, const std::optional<Rect>& right) {
    if(!left) {
      return right;
    } else if(!right) {
      return left;
    }
    auto x = std::min(left->get_left(), right->get_left());
    auto y = std::min(left->get_top(), right->get_top());
    return Rect(x, y, std::max(left->get_right(), right->get_right()) - x,
      std::max(left->get_bottom(), right->get_bottom()) - y);
  }

  void translate(Context& context, const NodeId& id, Point delta) {
    auto& document = *context.m_document;
    set_transform(document, *context.m_transaction, id,
      make_translation(delta.m_x, delta.m_y) * get_transform(document, id));
    context.m_is_changed = true;
  }

  void place(Context& context, const NodeId& id, const Matrix& transform) {
    if(is_near(get_transform(*context.m_document, id), transform)) {
      return;
    }
    set_transform(*context.m_document, *context.m_transaction, id, transform);
    context.m_is_changed = true;
  }

  void reorder(Context& context, const NodeId& parent,
      const std::vector<NodeId>& order) {
    if(reorder_children(
        *context.m_document, *context.m_transaction, parent, order)) {
      context.m_is_changed = true;
    }
  }

  void fit_background(Context& context, const NodeId& group,
      const NodeId& background, const Rect& content, const Margins& padding,
      const Baseline* baseline) {
    auto& document = *context.m_document;
    auto target = Rect(content.m_x - padding.m_left,
      content.m_y - padding.m_top,
      content.m_width + padding.m_left + padding.m_right,
      content.m_height + padding.m_top + padding.m_bottom);
    if(baseline && baseline->m_background && baseline->m_content) {
      auto& before = *baseline->m_background;
      auto& old = *baseline->m_content;
      auto left = before.get_left() + content.get_left() - old.get_left();
      auto top = before.get_top() + content.get_top() - old.get_top();
      auto right = before.get_right() + content.get_right() - old.get_right();
      auto bottom =
        before.get_bottom() + content.get_bottom() - old.get_bottom();
      target = Rect(left, top, right - left, bottom - top);
    }
    auto current = measure_child(document, background, *context.m_measurer);
    if(!current || is_near(*current, target)) {
      return;
    }
    if(baseline && baseline->m_background_record &&
        baseline->m_background && is_near(*baseline->m_background, target)) {
      auto& original = *baseline->m_background_record;
      auto& node = document.get(background);
      if(original.get_kind() == node.get_kind() &&
          (original.get_shape() || original.get_text())) {
        auto header = node.m_header;
        header.m_transform = original.m_header.m_transform;
        context.m_transaction->replace_header(std::move(header));
        auto payload = node.m_payload;
        if(auto shape = original.get_shape()) {
          auto copy = *node.get_shape();
          copy.m_geometry = shape->m_geometry;
          payload = Payload(Box<Shape>(std::move(copy)));
        } else if(auto text = original.get_text()) {
          auto copy = *node.get_text();
          copy.m_frame = text->m_frame;
          payload = Payload(Box<Text>(std::move(copy)));
        }
        context.m_transaction->replace_payload(background, std::move(payload));
        context.m_is_changed = true;
        auto restored =
          measure_child(document, background, *context.m_measurer);
        if(restored && is_near(*restored, target)) {
          return;
        }
        current = restored;
        if(!current) {
          return;
        }
      }
    }
    auto world = get_world_transform(document, group);
    auto inverse = invert(world);
    if(!inverse) {
      return;
    }
    transform_node(document, *context.m_transaction, background,
      world * make_rect_mapping(*current, target) * *inverse);
    context.m_is_changed = true;
  }

  double get_common_margin(const std::map<NodeId, double>& spacing) {
    auto counts = std::map<double, int>();
    for(auto& entry : spacing) {
      ++counts[entry.second];
    }
    auto margin = 0.0;
    auto best = 0;
    for(auto& entry : counts) {
      if(entry.second > best) {
        best = entry.second;
        margin = entry.first;
      }
    }
    return margin;
  }

  void arrange_stack(Context& context, const NodeId& id,
      const StackPlan& plan) {
    auto& document = *context.m_document;
    auto node = document.find(id);
    if(!node || !node->get_group() || !node->get_group()->m_stack) {
      return;
    }
    auto stack = *node->get_group()->m_stack;
    auto is_vertical = stack.m_orientation == Orientation::VERTICAL;
    auto children = node->m_children;
    auto background = NodeId();
    auto entries = std::vector<NodeId>(children.rbegin(), children.rend());
    if(stack.m_has_background && !entries.empty()) {
      background = children.front();
      entries.pop_back();
    }
    auto bounds = std::unordered_map<NodeId, Rect>();
    for(auto& entry : entries) {
      if(is_shown(document, entry)) {
        if(auto rect = measure_child(document, entry, *context.m_measurer)) {
          bounds.emplace(entry, *rect);
        }
      }
    }
    auto get_start = [&] (const Rect& rect) {
      return is_vertical ? rect.m_y : rect.m_x;
    };
    auto get_extent = [&] (const Rect& rect) {
      return is_vertical ? rect.m_height : rect.m_width;
    };
    auto get_center = [&] (const Rect& rect) {
      return get_start(rect) + get_extent(rect) / 2;
    };
    if(!plan.m_moved.empty()) {
      auto order = std::vector<NodeId>();
      auto moved = std::vector<NodeId>();
      for(auto& entry : entries) {
        if(plan.m_moved.contains(entry) && bounds.contains(entry)) {
          moved.push_back(entry);
        } else {
          order.push_back(entry);
        }
      }
      for(auto& item : moved) {
        auto key = get_center(bounds.at(item));
        auto position = std::find_if(order.begin(), order.end(),
          [&] (const auto& other) {
            auto rect = bounds.find(other);
            return rect != bounds.end() && !plan.m_moved.contains(other) &&
              get_center(rect->second) > key;
          });
        order.insert(position, item);
      }
      entries = std::move(order);
    }
    auto desired = std::vector<NodeId>();
    if(!background.is_nil()) {
      desired.push_back(background);
    }
    desired.insert(desired.end(), entries.rbegin(), entries.rend());
    if(desired != children) {
      reorder(context, id, desired);
    }
    auto items = std::vector<NodeId>();
    for(auto& entry : entries) {
      if(bounds.contains(entry)) {
        items.push_back(entry);
      }
    }
    auto spacing = stack.m_spacing;
    std::erase_if(spacing, [&] (const auto& entry) {
      return std::find(children.begin(), children.end(), entry.first) ==
        children.end();
    });
    auto common = get_common_margin(spacing);
    for(auto i = std::size_t(0); i != items.size(); ++i) {
      if(i + 1 == items.size()) {
        spacing.erase(items[i]);
      } else if(!spacing.contains(items[i]) &&
          !document.get(items[i]).get_reference()) {
        spacing.emplace(items[i], common);
      }
    }
    auto is_spacing_changed = spacing != stack.m_spacing;
    stack.m_spacing = std::move(spacing);
    auto baseline = plan.m_baseline ? &*plan.m_baseline : nullptr;
    if(!items.empty()) {
      auto start = std::optional<double>();
      if(baseline) {
        for(auto& item : baseline->m_order) {
          auto value = get_start(baseline->m_bounds.at(item));
          start = start ? std::min(*start, value) : value;
        }
      }
      if(!start) {
        for(auto& item : items) {
          if(!plan.m_floating.contains(item)) {
            auto item_start = get_start(bounds.at(item));
            start = start ? std::min(*start, item_start) : item_start;
          }
        }
      }
      auto old_indexes = std::unordered_map<NodeId, std::size_t>();
      if(baseline) {
        for(auto i = std::size_t(0); i != baseline->m_order.size(); ++i) {
          old_indexes.emplace(baseline->m_order[i], i);
        }
      }
      auto get_gap = [&] (const NodeId& previous, const NodeId& item) {
        auto margin = get_stack_margin(document, stack, previous);
        if(!baseline) {
          return margin;
        }
        auto before = old_indexes.find(previous);
        auto after = old_indexes.find(item);
        if(before == old_indexes.end() || after == old_indexes.end() ||
            after->second != before->second + 1) {
          return margin;
        }
        auto old_margin = baseline->m_spacing.find(previous);
        auto new_margin = stack.m_spacing.find(previous);
        if((old_margin == baseline->m_spacing.end()) !=
            (new_margin == stack.m_spacing.end()) ||
            (new_margin != stack.m_spacing.end() &&
              !is_near(old_margin->second, new_margin->second))) {
          return margin;
        }
        auto& old_previous = baseline->m_bounds.at(previous);
        return get_start(baseline->m_bounds.at(item)) -
          (get_start(old_previous) + get_extent(old_previous));
      };
      auto position = start.value_or(get_start(bounds.at(items.front())));
      for(auto i = std::size_t(0); i != items.size(); ++i) {
        auto& item = items[i];
        auto& rect = bounds.at(item);
        if(i != 0) {
          auto& previous = bounds.at(items[i - 1]);
          position = get_start(previous) + get_extent(previous) +
            get_gap(items[i - 1], item);
        }
        if(!plan.m_floating.contains(item)) {
          auto delta = position - get_start(rect);
          if(std::abs(delta) > TOLERANCE) {
            if(is_vertical) {
              translate(context, item, Point(0, delta));
              rect.m_y += delta;
            } else {
              translate(context, item, Point(delta, 0));
              rect.m_x += delta;
            }
          }
        } else if(is_vertical) {
          rect.m_y = position;
        } else {
          rect.m_x = position;
        }
      }
    }
    if(is_spacing_changed) {
      auto payload = *document.get(id).get_group();
      payload.m_stack->m_spacing = stack.m_spacing;
      context.m_transaction->replace_payload(id, std::move(payload));
      context.m_is_changed = true;
    }
    if(!background.is_nil()) {
      auto content = std::optional<Rect>();
      for(auto& item : items) {
        content = unite(content, bounds.at(item));
      }
      if(content) {
        fit_background(
          context, id, background, *content, stack.m_padding, baseline);
      }
    }
  }

  void arrange_padding(
      Context& context, const NodeId& id, const Baseline* baseline) {
    auto& document = *context.m_document;
    auto node = document.find(id);
    if(!node || !node->get_group() || !node->get_group()->m_padding) {
      return;
    }
    auto padding = *node->get_group()->m_padding;
    auto children = node->m_children;
    if(!padding.m_has_background || children.size() < 2) {
      return;
    }
    auto content = std::optional<Rect>();
    for(auto i = std::size_t(1); i != children.size(); ++i) {
      if(is_shown(document, children[i])) {
        content = unite(content,
          measure_child(document, children[i], *context.m_measurer));
      }
    }
    if(content) {
      fit_background(context, id, children.front(), *content,
        padding.m_padding, baseline);
    }
  }

  void arrange_grid(Context& context, const NodeId& id, bool is_measured,
      const std::optional<Rect>& old_first) {
    auto& document = *context.m_document;
    auto node = document.find(id);
    if(!node || !node->get_group() || !node->get_group()->m_repeat_grid) {
      return;
    }
    auto grid = *node->get_group()->m_repeat_grid;
    auto cells = node->m_children;
    if(cells.empty()) {
      return;
    }
    auto cell_size = grid.m_cell_size;
    if(is_measured) {
      auto first =
        measure_child(document, cells.front(), *context.m_measurer);
      if(!first) {
        return;
      }
      auto offset = first->get_top_left();
      cell_size = first->get_size();
      if(old_first) {
        offset = Point(first->m_x - old_first->m_x,
          first->m_y - old_first->m_y);
        cell_size = Size(
          grid.m_cell_size.m_width + first->m_width - old_first->m_width,
          grid.m_cell_size.m_height + first->m_height - old_first->m_height);
      }
      if(std::abs(offset.m_x) > TOLERANCE ||
          std::abs(offset.m_y) > TOLERANCE) {
        place(context, id, get_transform(document, id) *
          make_translation(offset.m_x, offset.m_y));
        for(auto& cell : cells) {
          place(context, cell, make_translation(-offset.m_x, -offset.m_y) *
            get_transform(document, cell));
        }
      }
    }
    auto base = get_transform(document, cells.front());
    auto columns = std::max(grid.m_column_count, 1);
    for(auto i = std::size_t(1); i < cells.size(); ++i) {
      auto row = static_cast<int>(i) / columns;
      auto column = static_cast<int>(i) % columns;
      place(context, cells[i], make_translation(
        column * (cell_size.m_width + grid.m_column_spacing),
        row * (cell_size.m_height + grid.m_row_spacing)) * base);
    }
    auto old_width = get_repeat_extent(
      grid.m_column_count, grid.m_cell_size.m_width, grid.m_column_spacing);
    auto old_height = get_repeat_extent(
      grid.m_row_count, grid.m_cell_size.m_height, grid.m_row_spacing);
    auto width = get_repeat_extent(
      grid.m_column_count, cell_size.m_width, grid.m_column_spacing);
    auto height = get_repeat_extent(
      grid.m_row_count, cell_size.m_height, grid.m_row_spacing);
    auto size = grid.m_size;
    if(is_near(size.m_width, old_width)) {
      size.m_width = width;
    } else {
      size.m_width += width - old_width;
    }
    if(is_near(size.m_height, old_height)) {
      size.m_height = height;
    } else {
      size.m_height += height - old_height;
    }
    if(!is_near(cell_size.m_width, grid.m_cell_size.m_width) ||
        !is_near(cell_size.m_height, grid.m_cell_size.m_height) ||
        !is_near(size.m_width, grid.m_size.m_width) ||
        !is_near(size.m_height, grid.m_size.m_height)) {
      auto payload = *document.get(id).get_group();
      payload.m_repeat_grid->m_cell_size = cell_size;
      payload.m_repeat_grid->m_size = size;
      context.m_transaction->replace_payload(id, std::move(payload));
      context.m_is_changed = true;
    }
  }

  Originals make_originals(const Document& document) {
    auto originals = Originals();
    for(auto& entry : document.get_original_records()) {
      originals.m_records.emplace(entry.first, entry.second.get());
      auto mark = [&] (NodeId id) {
        for(auto depth = 0; !id.is_nil() && depth != MAXIMUM_DEPTH; ++depth) {
          if(!originals.m_dirty.insert(id).second) {
            return;
          }
          auto record = originals.m_records.find(id);
          if(record != originals.m_records.end() && record->second) {
            id = record->second->m_parent;
          } else if(auto node = document.find(id)) {
            id = node->m_parent;
          } else {
            return;
          }
        }
      };
      mark(entry.first);
      if(auto node = document.find(entry.first)) {
        mark(node->m_parent);
      }
    }
    return originals;
  }

  std::optional<Rect> get_original_bounds(const Context& context,
      const Originals& originals, const NodeId& id, const Matrix& parent,
      int depth) {
    auto& document = *context.m_document;
    if(depth > MAXIMUM_DEPTH) {
      return std::nullopt;
    }
    auto entry = originals.m_records.find(id);
    if(entry == originals.m_records.end() && !originals.m_dirty.contains(id)) {
      if(!is_shown(document, id)) {
        return std::nullopt;
      }
      return get_bounds(document, id, parent * get_transform(document, id),
        BoundsType::GEOMETRY, *context.m_measurer);
    }
    auto record = entry != originals.m_records.end() ? entry->second :
      document.find(id);
    if(!record || !record->m_header.m_is_visible) {
      return std::nullopt;
    }
    if(auto reference = record->get_reference()) {
      if(!document.find(id)) {
        return std::nullopt;
      }
      auto transform = reference->m_transform.value_or(
        get_transform(document, id));
      return get_bounds(document, id, parent * transform, BoundsType::GEOMETRY,
        *context.m_measurer);
    }
    auto matrix = parent * record->m_header.m_transform;
    if(auto shape = record->get_shape()) {
      auto path = to_path(shape->m_geometry);
      if(path.is_empty()) {
        return std::nullopt;
      }
      return get_bounds(path, matrix);
    } else if(auto text = record->get_text()) {
      return matrix.map((*context.m_measurer)(*text));
    } else if(auto group = record->get_group();
        group && group->m_mask && !group->m_mask->m_shapes.empty()) {
      auto bounds = std::optional<Rect>();
      for(auto& shape : group->m_mask->m_shapes) {
        auto path = to_path(shape.m_geometry);
        if(!path.is_empty()) {
          bounds = unite(bounds, get_bounds(path, matrix * shape.m_transform));
        }
      }
      return bounds;
    }
    auto bounds = std::optional<Rect>();
    for(auto& child : record->m_children) {
      bounds = unite(bounds,
        get_original_bounds(context, originals, child, matrix, depth + 1));
    }
    return bounds;
  }

  const Node* find_original(const Context& context,
      const Originals& originals, const NodeId& id) {
    auto entry = originals.m_records.find(id);
    if(entry != originals.m_records.end()) {
      return entry->second;
    }
    return context.m_document->find(id);
  }

  std::optional<Rect> get_original_child_bounds(const Context& context,
      const Originals& originals, const NodeId& parent, const NodeId& id) {
    auto record = find_original(context, originals, id);
    if(!record || record->m_parent != parent) {
      return std::nullopt;
    }
    return get_original_bounds(context, originals, id, Matrix(), 0);
  }

  std::optional<Baseline> make_baseline(const Context& context,
      const Originals& originals, const NodeId& id) {
    auto record = find_original(context, originals, id);
    auto group = record ? record->get_group() : nullptr;
    if(!group || (!group->m_stack && !group->m_padding)) {
      return std::nullopt;
    }
    auto baseline = Baseline();
    auto children = record->m_children;
    auto background = NodeId();
    if(group->m_stack) {
      baseline.m_spacing = group->m_stack->m_spacing;
      if(group->m_stack->m_has_background && !children.empty()) {
        background = children.front();
        children.erase(children.begin());
      }
    } else if(group->m_padding->m_has_background && !children.empty()) {
      background = children.front();
      children.erase(children.begin());
    }
    for(auto i = children.rbegin(); i != children.rend(); ++i) {
      if(auto bounds =
          get_original_child_bounds(context, originals, id, *i)) {
        baseline.m_bounds.emplace(*i, *bounds);
        baseline.m_order.push_back(*i);
        baseline.m_content = unite(baseline.m_content, *bounds);
      }
    }
    if(!background.is_nil()) {
      baseline.m_background =
        get_original_child_bounds(context, originals, id, background);
      if(auto entry = originals.m_records.find(background);
          entry != originals.m_records.end()) {
        baseline.m_background_record = entry->second;
      }
    }
    return baseline;
  }

  std::optional<Rect> get_original_first(const Context& context,
      const Originals& originals, const NodeId& id) {
    auto record = find_original(context, originals, id);
    auto& cells = context.m_document->get(id).m_children;
    if(!record || record->m_children.empty() || cells.empty() ||
        record->m_children.front() != cells.front()) {
      return std::nullopt;
    }
    return get_original_child_bounds(context, originals, id, cells.front());
  }

  bool is_new_layout(
      const Originals& originals, const NodeId& id, const Group& group) {
    auto entry = originals.m_records.find(id);
    if(entry == originals.m_records.end()) {
      return false;
    }
    auto record = entry->second ? entry->second->get_group() : nullptr;
    return !record || static_cast<bool>(record->m_stack) !=
      static_cast<bool>(group.m_stack) ||
      static_cast<bool>(record->m_padding) !=
        static_cast<bool>(group.m_padding);
  }

  NodeId clone(Context& context, const NodeId& source, const NodeId& parent) {
    auto copy =
      copy_into(*context.m_document, *context.m_transaction, source, parent);
    if(!copy.is_nil()) {
      context.m_is_changed = true;
    }
    return copy;
  }

  bool is_image(const Fill& fill) {
    return std::holds_alternative<ImageFill>(fill);
  }

  bool has_same_styles(
      const std::vector<TextRun>& left, const std::vector<TextRun>& right) {
    auto collect = [] (const std::vector<TextRun>& runs) {
      auto styles = std::vector<std::pair<TextStyle, CharacterStyleId>>();
      for(auto& run : runs) {
        auto style = std::pair(run.m_style, run.m_character_style);
        if(std::find(styles.begin(), styles.end(), style) == styles.end()) {
          styles.push_back(std::move(style));
        }
      }
      return styles;
    };
    return collect(left) == collect(right);
  }

  std::vector<TextRun> restyle(const Text& source, std::size_t length) {
    if(source.m_runs.empty()) {
      return {};
    }
    auto total = std::size_t(0);
    for(auto& run : source.m_runs) {
      total += run.m_length;
    }
    if(total == length) {
      return source.m_runs;
    }
    auto run = source.m_runs.front();
    run.m_length = length;
    return std::vector{run};
  }

  void copy_changes(Context& context, const NodeId& source_id,
      const NodeId& target_id, ChangeMask mask) {
    auto& document = *context.m_document;
    auto& source = document.get(source_id);
    auto& target = document.get(target_id);
    if(source.get_kind() != target.get_kind()) {
      return;
    }
    auto header = target.m_header;
    auto& from = source.m_header;
    if(is_set(mask, ChangeMask::NAME) &&
        !(source.get_text() && is_set(mask, ChangeMask::TEXT))) {
      header.m_name = from.m_name;
      header.m_has_custom_name = from.m_has_custom_name;
    }
    if(is_set(mask, ChangeMask::VISIBILITY)) {
      header.m_is_visible = from.m_is_visible;
    }
    if(is_set(mask, ChangeMask::LOCK)) {
      header.m_is_locked = from.m_is_locked;
    }
    if(is_set(mask, ChangeMask::TRANSFORM)) {
      header.m_transform = from.m_transform;
      header.m_transform_3d = from.m_transform_3d;
    }
    if(is_set(mask, ChangeMask::OPACITY)) {
      header.m_opacity = from.m_opacity;
    }
    if(is_set(mask, ChangeMask::BLEND_MODE)) {
      header.m_blend_mode = from.m_blend_mode;
    }
    if(is_set(mask, ChangeMask::ISOLATION)) {
      header.m_is_isolated = from.m_is_isolated;
    }
    if(is_set(mask, ChangeMask::CONSTRAINTS)) {
      header.m_constraints = from.m_constraints;
    }
    if(is_set(mask, ChangeMask::EXPORT)) {
      header.m_is_marked_for_export = from.m_is_marked_for_export;
    }
    if(!(header == target.m_header)) {
      context.m_transaction->replace_header(std::move(header));
      context.m_is_changed = true;
    }
    auto& current = document.get(target_id);
    auto& updated = document.get(source_id);
    auto payload = std::optional<Payload>();
    if(auto shape = updated.get_shape()) {
      auto copy = *current.get_shape();
      if(is_set(mask, ChangeMask::GEOMETRY)) {
        copy.m_geometry = shape->m_geometry;
      }
      if(is_set(mask, ChangeMask::STYLE)) {
        auto fill = copy.m_style.m_fill;
        copy.m_style = shape->m_style;
        if(is_image(fill) || is_image(shape->m_style.m_fill)) {
          copy.m_style.m_fill = fill;
        }
      }
      if(!(copy == *current.get_shape())) {
        payload = Payload(Box<Shape>(std::move(copy)));
      }
    } else if(auto text = updated.get_text()) {
      auto copy = *current.get_text();
      if(is_set(mask, ChangeMask::GEOMETRY)) {
        copy.m_frame = text->m_frame;
      }
      if(is_set(mask, ChangeMask::STYLE)) {
        copy.m_style = text->m_style;
      }
      if(is_set(mask, ChangeMask::TEXT)) {
        copy.m_base_style = text->m_base_style;
        copy.m_paragraph_style = text->m_paragraph_style;
        if(!has_same_styles(text->m_runs, copy.m_runs)) {
          copy.m_runs = restyle(*text, copy.m_content->size());
        }
      }
      if(!(copy == *current.get_text())) {
        payload = Payload(Box<Text>(std::move(copy)));
      }
    } else if(auto group = updated.get_group();
        group && is_set(mask, ChangeMask::ROLES)) {
      auto copy = *current.get_group();
      copy.m_mask = group->m_mask;
      copy.m_scroll_group = group->m_scroll_group;
      copy.m_padding = group->m_padding;
      copy.m_stack = group->m_stack;
      if(copy.m_stack) {
        auto spacing = std::map<NodeId, double>();
        auto& from_children = updated.m_children;
        auto& to_children = current.m_children;
        for(auto& entry : group->m_stack->m_spacing) {
          auto position =
            std::find(from_children.begin(), from_children.end(), entry.first);
          auto index =
            static_cast<std::size_t>(position - from_children.begin());
          if(index < to_children.size()) {
            spacing.emplace(to_children[index], entry.second);
          }
        }
        copy.m_stack->m_spacing = std::move(spacing);
      }
      if(!(copy == *current.get_group())) {
        payload = Payload(std::move(copy));
      }
    } else if(auto reference = updated.get_reference();
        reference && is_set(mask, ChangeMask::TRANSFORM)) {
      auto copy = *current.get_reference();
      copy.m_transform = reference->m_transform;
      if(!(copy == *current.get_reference())) {
        payload = Payload(std::move(copy));
      }
    }
    if(payload) {
      context.m_transaction->replace_payload(target_id, std::move(*payload));
      context.m_is_changed = true;
    }
  }

  void synchronize_node(Context& context, const Analysis& analysis,
    const Originals& originals, const NodeId& source, const NodeId& target,
    int depth);

  void synchronize_children(Context& context, const Analysis& analysis,
      const Originals& originals, const NodeId& source, const NodeId& target,
      int depth) {
    auto& document = *context.m_document;
    auto source_node = document.find(source);
    auto target_node = document.find(target);
    if(!source_node || !target_node || depth > MAXIMUM_DEPTH) {
      return;
    }
    auto children = source_node->m_children;
    auto previous = children;
    auto mask = analysis.m_masks.find(source);
    if(mask != analysis.m_masks.end() &&
        is_set(mask->second, ChangeMask::CHILDREN)) {
      auto record = originals.m_records.find(source);
      if(record != originals.m_records.end() && record->second) {
        previous = record->second->m_children;
      }
    }
    auto counterparts = target_node->m_children;
    if(counterparts.size() != previous.size()) {
      return;
    }
    auto indexes = std::unordered_map<NodeId, std::size_t>();
    for(auto i = std::size_t(0); i != previous.size(); ++i) {
      indexes.emplace(previous[i], i);
    }
    auto is_kept = std::vector<bool>(previous.size(), false);
    auto pairs = std::vector<std::pair<NodeId, NodeId>>();
    auto order = std::vector<NodeId>();
    for(auto& child : children) {
      if(auto index = indexes.find(child); index != indexes.end()) {
        is_kept[index->second] = true;
        order.push_back(counterparts[index->second]);
        pairs.emplace_back(child, counterparts[index->second]);
      } else {
        auto copy = clone(context, child, target);
        if(!copy.is_nil()) {
          order.push_back(copy);
        }
      }
    }
    for(auto i = std::size_t(0); i != previous.size(); ++i) {
      if(!is_kept[i] && document.find(counterparts[i])) {
        context.m_transaction->remove(counterparts[i]);
        context.m_is_changed = true;
      }
    }
    if(document.get(target).m_children != order) {
      reorder(context, target, order);
    }
    for(auto& pair : pairs) {
      synchronize_node(
        context, analysis, originals, pair.first, pair.second, depth + 1);
    }
  }

  void synchronize_node(Context& context, const Analysis& analysis,
      const Originals& originals, const NodeId& source, const NodeId& target,
      int depth) {
    if(!analysis.m_containing.contains(source)) {
      return;
    }
    synchronize_children(context, analysis, originals, source, target, depth);
    if(auto mask = analysis.m_masks.find(source);
        mask != analysis.m_masks.end()) {
      copy_changes(context, source, target, mask->second);
    }
  }

  void propagate(Context& context, const Analysis& analysis,
      const Originals& originals, const NodeId& grid, const NodeId& source) {
    auto cells = context.m_document->get(grid).m_children;
    for(auto& cell : cells) {
      if(cell != source) {
        synchronize_children(context, analysis, originals, source, cell, 0);
      }
    }
  }

  Analysis analyze(const Document& document, const ChangeSet& changes) {
    auto analysis = Analysis();
    for(auto& change : changes.m_modified) {
      analysis.m_masks[change.m_id] |= change.m_mask;
    }
    analysis.m_arrived.insert(
      changes.m_inserted.begin(), changes.m_inserted.end());
    analysis.m_arrived.insert(changes.m_moved.begin(), changes.m_moved.end());
    auto touched = std::vector<NodeId>();
    touched.insert(
      touched.end(), changes.m_inserted.begin(), changes.m_inserted.end());
    touched.insert(
      touched.end(), changes.m_moved.begin(), changes.m_moved.end());
    for(auto& change : changes.m_modified) {
      touched.push_back(change.m_id);
    }
    for(auto& id : touched) {
      auto node = document.find(id);
      if(!node) {
        continue;
      }
      auto mask = ChangeMask::NONE;
      if(auto entry = analysis.m_masks.find(id);
          entry != analysis.m_masks.end()) {
        mask = entry->second;
      }
      auto is_arrived = analysis.m_arrived.contains(id);
      auto is_layout = is_arrived || is_set(mask, LAYOUT_CHANGES);
      if(has_layout(*node) &&
          is_set(mask, ChangeMask::ROLES | ChangeMask::CHILDREN)) {
        analysis.m_entries[id].m_is_changed = true;
      }
      analysis.m_containing.insert(id);
      auto child = id;
      auto parent_id = node->m_parent;
      auto is_inside_arrival = is_arrived;
      for(auto depth = 0; !parent_id.is_nil() && depth != MAXIMUM_DEPTH;
          ++depth) {
        auto parent = document.find(parent_id);
        if(!parent) {
          break;
        }
        analysis.m_containing.insert(parent_id);
        if(has_layout(*parent)) {
          auto& group = *parent->get_group();
          auto& entry = analysis.m_entries[parent_id];
          if(is_layout) {
            entry.m_is_changed = true;
          }
          auto is_cell_changed = child == id ? !is_arrived &&
            is_set(mask, CONTENT_CHANGES) : !is_inside_arrival;
          if(group.m_repeat_grid && is_cell_changed &&
              std::find(entry.m_cells.begin(), entry.m_cells.end(), child) ==
                entry.m_cells.end()) {
            entry.m_cells.push_back(child);
          }
          if(group.m_stack) {
            if(child == id) {
              if(!is_arrived && is_set(mask, ChangeMask::TRANSFORM) &&
                  !is_set(mask, CONTENT_CHANGES)) {
                entry.m_translated.insert(id);
              } else if(is_set(mask, CONTENT_CHANGES)) {
                entry.m_reshaped.insert(id);
              }
            } else if(is_layout) {
              entry.m_reshaped.insert(child);
            }
          }
        }
        child = parent_id;
        parent_id = parent->m_parent;
        is_inside_arrival =
          is_inside_arrival || analysis.m_arrived.contains(child);
      }
    }
    for(auto& entry : analysis.m_entries) {
      auto depth = 0;
      auto node = document.find(entry.first);
      while(node && !node->m_parent.is_nil() && depth != MAXIMUM_DEPTH) {
        node = document.find(node->m_parent);
        ++depth;
      }
      entry.second.m_depth = depth;
    }
    return analysis;
  }
}

int Rellu::count_repeat_cells(double size, double cell, double spacing) {
  auto pitch = cell + spacing;
  if(!(pitch > 0) || !(size > 0)) {
    return 1;
  }
  auto count = std::floor((size - TOLERANCE) / pitch) + 1;
  return static_cast<int>(std::clamp(count, 1.0, double(MAXIMUM_CELLS)));
}

double Rellu::get_repeat_extent(int count, double cell, double spacing) {
  return count * cell + (count - 1) * spacing;
}

bool Rellu::reorder_children(const Document& document,
    Transaction& transaction, const NodeId& parent,
    const std::vector<NodeId>& order) {
  auto current = document.get(parent).m_children;
  auto is_moved = false;
  for(auto i = std::size_t(0); i != order.size(); ++i) {
    if(i < current.size() && current[i] == order[i]) {
      continue;
    }
    auto position = std::find(current.begin(), current.end(), order[i]);
    if(position == current.end()) {
      continue;
    }
    current.erase(position);
    current.insert(current.begin() + static_cast<std::ptrdiff_t>(i), order[i]);
    transaction.move(order[i], parent, i);
    is_moved = true;
  }
  return is_moved;
}

NodeId Rellu::copy_into(const Document& document, Transaction& transaction,
    const NodeId& source, const NodeId& parent) {
  auto duplicate =
    DuplicateCommand(std::vector{source}, Point(0, 0), DuplicateMode::CLONE);
  duplicate.apply(document, transaction);
  if(duplicate.get_copies().empty()) {
    return NodeId();
  }
  auto copy = duplicate.get_copies().front();
  auto count = document.get(parent).m_children.size();
  if(document.get(copy).m_parent == parent) {
    --count;
  }
  transaction.move(copy, parent, count);
  return copy;
}

StackLimits Rellu::get_stack_limits(
    const Document& document, const std::vector<NodeId>& nodes) {
  auto limits = StackLimits();
  auto node = nodes.empty() ? nullptr : document.find(nodes.front());
  if(!node) {
    return limits;
  }
  if(nodes.size() != 1) {
    node = node->m_parent.is_nil() ? nullptr : document.find(node->m_parent);
  }
  if(node && node->get_reference()) {
    auto resolution =
      resolve_reference(document.get_nodes(), node->get_id());
    node = resolution ? document.find(resolution->m_source) : nullptr;
  }
  auto group = node ? node->get_group() : nullptr;
  if(!group || !group->m_stack) {
    return limits;
  }
  limits.m_is_width_fixed =
    group->m_stack->m_orientation == Orientation::HORIZONTAL;
  limits.m_is_height_fixed =
    group->m_stack->m_orientation == Orientation::VERTICAL;
  limits.m_is_rotation_fixed = nodes.size() != 1;
  return limits;
}

std::vector<NodeId> Rellu::get_stack_items(
    const Document& document, const NodeId& id) {
  auto& node = document.get(id);
  auto items =
    std::vector<NodeId>(node.m_children.rbegin(), node.m_children.rend());
  if(auto group = node.get_group(); group && group->m_stack &&
      group->m_stack->m_has_background && !items.empty()) {
    items.pop_back();
  }
  return items;
}

double Rellu::get_stack_margin(
    const Document& document, const Stack& stack, const NodeId& item) {
  if(auto entry = stack.m_spacing.find(item); entry != stack.m_spacing.end()) {
    return entry->second;
  }
  auto node = document.find(item);
  for(auto depth = 0; node && depth != MAXIMUM_DEPTH; ++depth) {
    auto reference = node->get_reference();
    if(!reference) {
      break;
    }
    auto source = document.find(reference->m_source);
    if(!source) {
      break;
    }
    if(auto parent = document.find(source->m_parent)) {
      if(auto group = parent->get_group(); group && group->m_stack) {
        auto& spacing = group->m_stack->m_spacing;
        if(auto entry = spacing.find(source->get_id());
            entry != spacing.end()) {
          return entry->second;
        }
      }
    }
    node = source;
  }
  return 0;
}

void Rellu::layout_stack(const Document& document, Transaction& transaction,
    const NodeId& id, const TextMeasurer& measurer) {
  auto context = Context(&document, &transaction, &measurer, false);
  arrange_stack(context, id, StackPlan());
}

void Rellu::layout_padding(const Document& document, Transaction& transaction,
    const NodeId& id, const TextMeasurer& measurer) {
  auto context = Context(&document, &transaction, &measurer, false);
  arrange_padding(context, id, nullptr);
}

void Rellu::layout_repeat_grid(
    const Document& document, Transaction& transaction, const NodeId& id) {
  auto measurer = TextMeasurer(estimate_text_bounds);
  auto context = Context(&document, &transaction, &measurer, false);
  arrange_grid(context, id, false, std::nullopt);
}

bool Rellu::refresh_layouts(Document& document, const ChangeSet& changes,
    const LayoutOptions& options) {
  if(changes.m_inserted.empty() && changes.m_moved.empty() &&
      changes.m_modified.empty()) {
    return false;
  }
  auto analysis = analyze(document, changes);
  if(analysis.m_entries.empty()) {
    return false;
  }
  auto groups = std::vector<std::pair<int, NodeId>>();
  for(auto& entry : analysis.m_entries) {
    groups.emplace_back(entry.second.m_depth, entry.first);
  }
  std::sort(groups.begin(), groups.end(),
    [] (const auto& left, const auto& right) {
      if(left.first != right.first) {
        return left.first > right.first;
      }
      return left.second < right.second;
    });
  auto originals = make_originals(document);
  auto floating = std::unordered_set<NodeId>(
    options.m_floating.begin(), options.m_floating.end());
  auto transaction = Transaction(document);
  auto context = Context(&document, &transaction, &options.m_measurer, false);
  try {
    auto baselines = std::unordered_map<NodeId, Baseline>();
    auto firsts = std::unordered_map<NodeId, Rect>();
    for(auto& group : groups) {
      auto& id = group.second;
      auto node = document.find(id);
      if(!node || !node->get_group()) {
        continue;
      }
      auto& payload = *node->get_group();
      if(payload.m_repeat_grid) {
        if(auto first = get_original_first(context, originals, id)) {
          firsts.emplace(id, *first);
        }
        continue;
      }
      auto mask = ChangeMask::NONE;
      if(auto entry = analysis.m_masks.find(id);
          entry != analysis.m_masks.end()) {
        mask = entry->second;
      }
      if(!is_set(mask, ChangeMask::ROLES) &&
          !is_new_layout(originals, id, payload)) {
        if(auto baseline = make_baseline(context, originals, id)) {
          baselines.emplace(id, std::move(*baseline));
        }
      }
    }
    for(auto& group : groups) {
      auto& id = group.second;
      auto& entry = analysis.m_entries.at(id);
      auto node = document.find(id);
      if(!node || !node->get_group()) {
        continue;
      }
      auto& payload = *node->get_group();
      auto baseline = baselines.find(id);
      if(payload.m_repeat_grid) {
        auto old_first = std::optional<Rect>();
        if(auto first = firsts.find(id); first != firsts.end()) {
          old_first = first->second;
        }
        auto is_propagated = false;
        if(!entry.m_cells.empty()) {
          auto& cells = node->m_children;
          auto source = std::find_first_of(cells.begin(), cells.end(),
            entry.m_cells.begin(), entry.m_cells.end());
          if(source != cells.end()) {
            propagate(context, analysis, originals, id, *source);
            is_propagated = true;
          }
        }
        if(entry.m_is_changed || is_propagated) {
          arrange_grid(context, id, true, old_first);
        }
      } else if(payload.m_stack && entry.m_is_changed) {
        auto plan = StackPlan();
        if(!is_new_layout(originals, id, payload)) {
          if(baseline != baselines.end()) {
            plan.m_baseline = baseline->second;
          }
          for(auto& item : entry.m_translated) {
            if(!entry.m_reshaped.contains(item)) {
              plan.m_moved.insert(item);
            }
          }
          for(auto& child : node->m_children) {
            if(floating.contains(child)) {
              plan.m_floating.insert(child);
              plan.m_moved.insert(child);
            }
          }
        }
        arrange_stack(context, id, plan);
      } else if(payload.m_padding && entry.m_is_changed) {
        arrange_padding(context, id,
          baseline != baselines.end() ? &baseline->second : nullptr);
      }
    }
  } catch(const std::exception&) {
    return false;
  }
  if(!context.m_is_changed) {
    return false;
  }
  transaction.amend();
  return true;
}

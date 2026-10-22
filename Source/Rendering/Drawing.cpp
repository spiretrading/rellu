#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <variant>
#include "Rellu/Rendering/Drawing.hpp"

using namespace Rellu;

namespace {
  constexpr auto MARGIN = 8.0;
  constexpr auto BLUR_MARGIN = 66.0;
  constexpr auto LEAF_SIZE = std::size_t(16);
  constexpr auto MAXIMUM_DAMAGE = std::size_t(64);
  constexpr auto MAXIMUM_COORDINATE = double(1 << 30);

  struct Extent {
    Rect m_bounds;
    double m_margin;
  };

  struct Item {
    std::uint32_t m_first;
    std::uint32_t m_end;
    Rect m_bounds;
    double m_margin;
    bool m_is_reading;
    bool m_is_blurring;
  };

  Rect translate(const Rect& rect, Point offset) {
    return Rect(rect.m_x + offset.m_x, rect.m_y + offset.m_y, rect.m_width,
      rect.m_height);
  }

  Rect grow(const Rect& rect, double amount) {
    if(rect.is_empty()) {
      return rect;
    }
    return inflate(rect, amount);
  }

  bool is_touching(const Rect& bounds, double margin, double scale,
      const Rect& region) {
    return grow(bounds, margin / scale).intersects(region);
  }

  Rect map_bounds(const Rect& bounds, const Matrix& transform) {
    auto first = transform.map(bounds.get_top_left());
    auto left = first.m_x;
    auto top = first.m_y;
    auto right = left;
    auto bottom = top;
    for(auto& corner : {Point(bounds.get_right(), bounds.get_top()),
        Point(bounds.get_left(), bounds.get_bottom()),
        Point(bounds.get_right(), bounds.get_bottom())}) {
      auto point = transform.map(corner);
      left = std::min(left, point.m_x);
      top = std::min(top, point.m_y);
      right = std::max(right, point.m_x);
      bottom = std::max(bottom, point.m_y);
    }
    return Rect(left, top, right - left, bottom - top);
  }

  Rect get_bounds(const std::shared_ptr<const Path>& path,
      const Matrix& transform, const std::optional<Rect>& clip) {
    auto left = std::numeric_limits<double>::infinity();
    auto top = left;
    auto right = -left;
    auto bottom = -left;
    for(auto& segment : path->get_segments()) {
      auto count = 1;
      if(segment.m_type == SegmentType::QUADRATIC) {
        count = 2;
      } else if(segment.m_type == SegmentType::CUBIC) {
        count = 3;
      } else if(segment.m_type == SegmentType::CLOSE) {
        count = 0;
      }
      for(auto i = 0; i != count; ++i) {
        auto point = transform.map(segment.m_points[i]);
        left = std::min(left, point.m_x);
        top = std::min(top, point.m_y);
        right = std::max(right, point.m_x);
        bottom = std::max(bottom, point.m_y);
      }
    }
    if(!(left <= right) || !(top <= bottom)) {
      return Rect(0, 0, 0, 0);
    }
    auto bounds = Rect(left, top, right - left, bottom - top);
    if(clip) {
      return intersect(bounds, *clip);
    }
    return bounds;
  }

  Rect get_fills(const DrawList& list) {
    auto fills = Rect(0, 0, 0, 0);
    for(auto& command : list) {
      if(auto fill = std::get_if<FillCommand>(&command)) {
        auto bounds = Rect(0, 0, 0, 0);
        if(fill->m_bounds && fill->m_transform.m_b == 0 &&
            fill->m_transform.m_c == 0) {
          bounds = map_bounds(*fill->m_bounds, fill->m_transform);
        } else {
          bounds = Rellu::get_bounds(*fill->m_path, fill->m_transform);
        }
        if(fill->m_clip) {
          bounds = intersect(bounds, *fill->m_clip);
        }
        fills = unite(fills, bounds);
      }
    }
    return fills;
  }

  Extent apply(const BeginLayerCommand& layer, const Extent& content) {
    auto bounds = content.m_bounds;
    auto margin = content.m_margin;
    auto is_blurred = false;
    for(auto& shadow : layer.m_drop_shadows) {
      bounds = unite(bounds, grow(translate(content.m_bounds,
        shadow.m_offset), 3 * std::abs(shadow.m_radius)));
      is_blurred = true;
    }
    if(layer.m_blur > 0) {
      bounds = grow(bounds, 3 * layer.m_blur);
      is_blurred = true;
    }
    if(!layer.m_inner_shadows.empty()) {
      is_blurred = true;
    }
    if(is_blurred) {
      margin += BLUR_MARGIN;
    }
    if(layer.m_clip) {
      bounds = intersect(bounds, *layer.m_clip);
    }
    if(layer.m_mask) {
      bounds = intersect(bounds,
        get_bounds(layer.m_mask->m_path, layer.m_mask->m_transform, {}));
    }
    return Extent(bounds, margin);
  }

  void analyze(const DrawList& list, std::vector<Item>& items) {
    struct Frame {
      std::uint32_t m_first;
      Extent m_extent;
    };
    auto stack = std::vector<Frame>();
    auto add = [&] (const Extent& extent, std::uint32_t first,
        std::uint32_t end, bool is_reading) {
      if(stack.empty()) {
        items.push_back(Item(first, end, extent.m_bounds, extent.m_margin,
          is_reading, std::holds_alternative<BackdropBlurCommand>(
            list[first])));
      } else {
        auto& top = stack.back().m_extent;
        top.m_bounds = unite(top.m_bounds, extent.m_bounds);
        top.m_margin = std::max(top.m_margin, extent.m_margin);
      }
    };
    auto close = [&] (std::uint32_t end) {
      auto frame = stack.back();
      stack.pop_back();
      auto& layer = std::get<BeginLayerCommand>(list[frame.m_first]);
      add(apply(layer, frame.m_extent), frame.m_first, end,
        layer.m_blend_mode != BlendMode::NORMAL);
    };
    for(auto i = std::uint32_t(0); i != list.size(); ++i) {
      auto& command = list[i];
      if(auto fill = std::get_if<FillCommand>(&command)) {
        auto bounds = Rect(0, 0, 0, 0);
        if(fill->m_bounds) {
          bounds = map_bounds(*fill->m_bounds, fill->m_transform);
          if(fill->m_clip) {
            bounds = intersect(bounds, *fill->m_clip);
          }
        } else {
          bounds = get_bounds(fill->m_path, fill->m_transform, fill->m_clip);
        }
        add(Extent(bounds, MARGIN), i, i + 1, false);
      } else if(auto blur = std::get_if<BackdropBlurCommand>(&command)) {
        auto bounds = grow(get_bounds(blur->m_path, blur->m_transform,
          blur->m_clip), 3 * std::abs(blur->m_radius));
        add(Extent(bounds, MARGIN + BLUR_MARGIN), i, i + 1, true);
      } else if(std::holds_alternative<BeginLayerCommand>(command)) {
        stack.push_back(Frame(i, Extent(Rect(0, 0, 0, 0), MARGIN)));
      } else if(!stack.empty()) {
        close(i + 1);
      }
    }
    while(!stack.empty()) {
      close(static_cast<std::uint32_t>(list.size()));
    }
  }

  bool is_same(const Paint& left, const Paint& right) {
    if(left.index() != right.index()) {
      return false;
    } else if(auto solid = std::get_if<SolidPaint>(&left)) {
      return solid->m_color == std::get<SolidPaint>(right).m_color;
    } else if(auto gradient = std::get_if<GradientPaint>(&left)) {
      auto& other = std::get<GradientPaint>(right);
      return gradient->m_geometry == other.m_geometry &&
        gradient->m_stops == other.m_stops &&
        gradient->m_transform == other.m_transform &&
        gradient->m_projection == other.m_projection;
    }
    auto& image = std::get<ImagePaint>(left);
    auto& other = std::get<ImagePaint>(right);
    return image.m_image == other.m_image &&
      image.m_transform == other.m_transform &&
      image.m_opacity == other.m_opacity &&
      image.m_projection == other.m_projection;
  }

  bool is_same(const Shadow& left, const Shadow& right) {
    return left.m_offset == right.m_offset &&
      left.m_radius == right.m_radius && left.m_color == right.m_color;
  }

  bool is_same(const std::vector<Shadow>& left,
      const std::vector<Shadow>& right) {
    return std::equal(left.begin(), left.end(), right.begin(), right.end(),
      [] (const auto& first, const auto& second) {
        return is_same(first, second);
      });
  }

  bool is_same(const DrawCommand& left, const DrawCommand& right) {
    if(left.index() != right.index()) {
      return false;
    } else if(auto fill = std::get_if<FillCommand>(&left)) {
      auto& other = std::get<FillCommand>(right);
      return fill->m_path == other.m_path &&
        fill->m_transform == other.m_transform &&
        is_same(fill->m_paint, other.m_paint) && fill->m_clip == other.m_clip;
    } else if(auto layer = std::get_if<BeginLayerCommand>(&left)) {
      auto& other = std::get<BeginLayerCommand>(right);
      if(static_cast<bool>(layer->m_mask) != static_cast<bool>(other.m_mask) ||
          (layer->m_mask && (layer->m_mask->m_path != other.m_mask->m_path ||
            layer->m_mask->m_transform != other.m_mask->m_transform))) {
        return false;
      }
      return layer->m_opacity == other.m_opacity &&
        layer->m_blend_mode == other.m_blend_mode &&
        is_same(layer->m_inner_shadows, other.m_inner_shadows) &&
        is_same(layer->m_drop_shadows, other.m_drop_shadows) &&
        layer->m_blur == other.m_blur && layer->m_clip == other.m_clip;
    } else if(auto blur = std::get_if<BackdropBlurCommand>(&left)) {
      auto& other = std::get<BackdropBlurCommand>(right);
      return blur->m_path == other.m_path &&
        blur->m_transform == other.m_transform &&
        blur->m_radius == other.m_radius &&
        blur->m_brightness == other.m_brightness &&
        blur->m_clip == other.m_clip;
    }
    return true;
  }

  class Index {
    public:
      void build(const std::vector<Item>& items);
      void query(const Rect& region, double scale,
        const std::vector<Item>& items,
        std::vector<std::uint32_t>& result) const;
      bool contains(const Rect& region, double scale,
        const std::vector<Item>& items) const;

    private:
      struct Node {
        Rect m_bounds;
        double m_margin;
        std::uint32_t m_first;
        std::uint32_t m_count;
        bool m_is_leaf;
      };
      struct Element {
        Rect m_bounds;
        double m_margin;
        std::uint32_t m_index;
      };
      std::vector<Node> m_nodes;
      std::vector<std::uint32_t> m_children;
  };

  void Index::build(const std::vector<Item>& items) {
    m_nodes.clear();
    m_children.clear();
    auto elements = std::vector<Element>();
    for(auto i = std::uint32_t(0); i != items.size(); ++i) {
      if(!items[i].m_bounds.is_empty()) {
        elements.push_back(
          Element(items[i].m_bounds, items[i].m_margin, i));
      }
    }
    if(elements.empty()) {
      return;
    }
    auto is_leaf = true;
    while(true) {
      auto count = elements.size();
      auto pages = (count + LEAF_SIZE - 1) / LEAF_SIZE;
      auto slices = static_cast<std::size_t>(
        std::ceil(std::sqrt(static_cast<double>(pages))));
      auto slice_size = slices * LEAF_SIZE;
      auto by_x = [] (const Element& left, const Element& right) {
        return left.m_bounds.get_center().m_x <
          right.m_bounds.get_center().m_x;
      };
      auto by_y = [] (const Element& left, const Element& right) {
        return left.m_bounds.get_center().m_y <
          right.m_bounds.get_center().m_y;
      };
      std::sort(elements.begin(), elements.end(), by_x);
      for(auto first = std::size_t(0); first < count; first += slice_size) {
        auto last = std::min(count, first + slice_size);
        std::sort(elements.begin() + first, elements.begin() + last, by_y);
      }
      auto parents = std::vector<Element>();
      for(auto first = std::size_t(0); first < count; first += LEAF_SIZE) {
        auto last = std::min(count, first + LEAF_SIZE);
        auto node = Node(Rect(0, 0, 0, 0), 0,
          static_cast<std::uint32_t>(m_children.size()),
          static_cast<std::uint32_t>(last - first), is_leaf);
        for(auto i = first; i != last; ++i) {
          node.m_bounds = unite(node.m_bounds, elements[i].m_bounds);
          node.m_margin = std::max(node.m_margin, elements[i].m_margin);
          m_children.push_back(elements[i].m_index);
        }
        parents.push_back(Element(node.m_bounds, node.m_margin,
          static_cast<std::uint32_t>(m_nodes.size())));
        m_nodes.push_back(node);
      }
      if(parents.size() == 1) {
        return;
      }
      elements = std::move(parents);
      is_leaf = false;
    }
  }

  void Index::query(const Rect& region, double scale,
      const std::vector<Item>& items,
      std::vector<std::uint32_t>& result) const {
    if(m_nodes.empty()) {
      return;
    }
    auto stack = std::vector<std::uint32_t>(
      {static_cast<std::uint32_t>(m_nodes.size() - 1)});
    while(!stack.empty()) {
      auto& node = m_nodes[stack.back()];
      stack.pop_back();
      if(!is_touching(node.m_bounds, node.m_margin, scale, region)) {
        continue;
      }
      for(auto i = node.m_first; i != node.m_first + node.m_count; ++i) {
        auto child = m_children[i];
        if(!node.m_is_leaf) {
          stack.push_back(child);
        } else if(is_touching(
            items[child].m_bounds, items[child].m_margin, scale, region)) {
          result.push_back(child);
        }
      }
    }
  }

  bool Index::contains(const Rect& region, double scale,
      const std::vector<Item>& items) const {
    if(m_nodes.empty()) {
      return false;
    }
    auto stack = std::vector<std::uint32_t>(
      {static_cast<std::uint32_t>(m_nodes.size() - 1)});
    while(!stack.empty()) {
      auto& node = m_nodes[stack.back()];
      stack.pop_back();
      if(!is_touching(node.m_bounds, node.m_margin, scale, region)) {
        continue;
      }
      for(auto i = node.m_first; i != node.m_first + node.m_count; ++i) {
        auto child = m_children[i];
        if(!node.m_is_leaf) {
          stack.push_back(child);
        } else if(is_touching(
            items[child].m_bounds, items[child].m_margin, scale, region)) {
          return true;
        }
      }
    }
    return false;
  }

  int to_coordinate(double value) {
    return static_cast<int>(
      std::clamp(value, -MAXIMUM_COORDINATE, MAXIMUM_COORDINATE));
  }
}

struct Drawing::Entry {
  std::shared_ptr<const DrawList> m_list;
  std::vector<Item> m_items;
  std::vector<std::uint32_t> m_readers;
  std::vector<std::uint32_t> m_blurs;
  Index m_index;
  Rect m_bounds;
  double m_margin;
  mutable std::optional<Rect> m_fills;

  explicit Entry(std::shared_ptr<const DrawList> list);
  DrawRegion get_region() const;
  bool is_same(std::size_t item, const Entry& entry,
    std::size_t other) const;
};

Drawing::Entry::Entry(std::shared_ptr<const DrawList> list)
    : m_list(std::move(list)),
      m_bounds(0, 0, 0, 0),
      m_margin(0) {
  analyze(*m_list, m_items);
  for(auto i = std::uint32_t(0); i != m_items.size(); ++i) {
    auto& item = m_items[i];
    if(item.m_is_reading) {
      m_readers.push_back(i);
    }
    if(item.m_is_blurring) {
      m_blurs.push_back(i);
    }
    m_bounds = unite(m_bounds, item.m_bounds);
    m_margin = std::max(m_margin, item.m_margin);
  }
  m_index.build(m_items);
}

DrawRegion Drawing::Entry::get_region() const {
  return DrawRegion(m_bounds, m_margin);
}

bool Drawing::Entry::is_same(
    std::size_t item, const Entry& entry, std::size_t other) const {
  auto& left = m_items[item];
  auto& right = entry.m_items[other];
  if(left.m_end - left.m_first != right.m_end - right.m_first) {
    return false;
  }
  for(auto i = std::uint32_t(0); i != left.m_end - left.m_first; ++i) {
    if(!::is_same((*m_list)[left.m_first + i],
        (*entry.m_list)[right.m_first + i])) {
      return false;
    }
  }
  return true;
}

QRect Rellu::to_pixels(const DrawRegion& region, const Matrix& view) {
  if(region.m_bounds.is_empty()) {
    return QRect();
  }
  auto bounds = view.map(region.m_bounds);
  auto margin = std::ceil(std::max(0.0, region.m_margin));
  auto left = to_coordinate(std::floor(bounds.get_left() - margin));
  auto top = to_coordinate(std::floor(bounds.get_top() - margin));
  auto right = to_coordinate(std::ceil(bounds.get_right() + margin));
  auto bottom = to_coordinate(std::ceil(bounds.get_bottom() + margin));
  return QRect(left, top, right - left, bottom - top);
}

Drawing::Drawing() = default;

Drawing::~Drawing() = default;

const std::vector<std::shared_ptr<const DrawList>>&
    Drawing::get_lists() const {
  return m_lists;
}

const Rect& Drawing::get_bounds() const {
  if(!m_bounds) {
    m_bounds.emplace(0, 0, 0, 0);
    for(auto& entry : m_entries) {
      if(!entry->m_fills) {
        entry->m_fills = get_fills(*entry->m_list);
      }
      *m_bounds = unite(*m_bounds, *entry->m_fills);
    }
  }
  return *m_bounds;
}

DrawRegion Drawing::get_region(std::size_t index) const {
  return m_entries[index]->get_region();
}

std::vector<DrawRegion> Drawing::set_lists(
    std::vector<std::shared_ptr<const DrawList>> lists) {
  auto previous = std::move(m_entries);
  auto none = std::numeric_limits<std::size_t>::max();
  auto matches = std::vector<std::size_t>(lists.size(), none);
  auto is_kept = std::vector<bool>(previous.size(), false);
  auto entries = std::vector<std::unique_ptr<Entry>>(lists.size());
  auto common = std::min(lists.size(), previous.size());
  auto head = std::size_t(0);
  while(head != common && lists[head] == previous[head]->m_list) {
    matches[head] = head;
    is_kept[head] = true;
    ++head;
  }
  auto tail = std::size_t(0);
  while(tail != common - head && lists[lists.size() - 1 - tail] ==
      previous[previous.size() - 1 - tail]->m_list) {
    matches[lists.size() - 1 - tail] = previous.size() - 1 - tail;
    is_kept[previous.size() - 1 - tail] = true;
    ++tail;
  }
  auto indices = std::unordered_map<const DrawList*, std::size_t>();
  for(auto i = head; i != previous.size() - tail; ++i) {
    indices.emplace(previous[i]->m_list.get(), i);
  }
  for(auto i = head; i != lists.size() - tail; ++i) {
    auto index = indices.find(lists[i].get());
    if(index != indices.end() && !is_kept[index->second]) {
      matches[i] = index->second;
      is_kept[index->second] = true;
    }
  }
  auto damage = std::vector<DrawRegion>();
  auto is_ordered = true;
  auto last = none;
  for(auto match : matches) {
    if(match != none) {
      if(last != none && match < last) {
        is_ordered = false;
      }
      last = match;
    }
  }
  auto pairs = std::vector<std::size_t>(lists.size(), none);
  if(is_ordered) {
    auto start = std::size_t(0);
    auto previous_start = std::size_t(0);
    for(auto i = std::size_t(0); i <= lists.size(); ++i) {
      if(i != lists.size() && matches[i] == none) {
        continue;
      }
      auto previous_end = i == lists.size() ? previous.size() : matches[i];
      if(i - start == previous_end - previous_start) {
        for(auto j = start; j != i; ++j) {
          pairs[j] = previous_start + (j - start);
        }
      }
      start = i + 1;
      previous_start = previous_end + 1;
    }
  }
  for(auto i = std::size_t(0); i != lists.size(); ++i) {
    if(matches[i] != none) {
      entries[i] = std::move(previous[matches[i]]);
      if(!is_ordered) {
        damage.push_back(entries[i]->get_region());
      }
      continue;
    }
    entries[i] = std::make_unique<Entry>(lists[i]);
    if(pairs[i] == none) {
      damage.push_back(entries[i]->get_region());
      continue;
    }
    auto& entry = *entries[i];
    auto& old = *previous[pairs[i]];
    auto count = std::min(entry.m_items.size(), old.m_items.size());
    auto front = std::size_t(0);
    while(front != count && old.is_same(front, entry, front)) {
      ++front;
    }
    auto back = std::size_t(0);
    while(back != count - front && old.is_same(old.m_items.size() - 1 - back,
        entry, entry.m_items.size() - 1 - back)) {
      ++back;
    }
    auto add = [&] (const Entry& source) {
      auto end = source.m_items.size() - back;
      if(end - front > MAXIMUM_DAMAGE) {
        auto region = DrawRegion(Rect(0, 0, 0, 0), 0);
        for(auto j = front; j != end; ++j) {
          region.m_bounds = unite(region.m_bounds, source.m_items[j].m_bounds);
          region.m_margin =
            std::max(region.m_margin, source.m_items[j].m_margin);
        }
        damage.push_back(region);
        return;
      }
      for(auto j = front; j != end; ++j) {
        auto& item = source.m_items[j];
        if(!item.m_bounds.is_empty()) {
          damage.push_back(DrawRegion(item.m_bounds, item.m_margin));
        }
      }
    };
    add(old);
    add(entry);
    previous[pairs[i]].reset();
  }
  for(auto& entry : previous) {
    if(entry) {
      damage.push_back(entry->get_region());
    }
  }
  std::erase_if(damage, [] (const auto& region) {
    return region.m_bounds.is_empty();
  });
  m_entries = std::move(entries);
  m_lists = std::move(lists);
  m_bounds.reset();
  return damage;
}

Rect Drawing::query(
    const Rect& region, double scale, DrawSequence& commands) const {
  commands.clear();
  auto area = region;
  if(area.is_empty() || !(scale > 0)) {
    return area;
  }
  auto is_grown = true;
  while(is_grown) {
    is_grown = false;
    for(auto& entry : m_entries) {
      for(auto reader : entry->m_readers) {
        auto& item = entry->m_items[reader];
        auto bounds = grow(item.m_bounds, item.m_margin / scale);
        if(bounds.intersects(area) && !area.contains(bounds)) {
          area = unite(area, bounds);
          is_grown = true;
        }
      }
    }
  }
  auto indices = std::vector<std::uint32_t>();
  for(auto& entry : m_entries) {
    if(!is_touching(entry->m_bounds, entry->m_margin, scale, area)) {
      continue;
    }
    indices.clear();
    entry->m_index.query(area, scale, entry->m_items, indices);
    std::sort(indices.begin(), indices.end());
    auto& list = *entry->m_list;
    for(auto index : indices) {
      auto& item = entry->m_items[index];
      for(auto i = item.m_first; i != item.m_end; ++i) {
        commands.push_back(&list[i]);
      }
    }
  }
  return area;
}

std::vector<DrawRegion> Drawing::get_readers(
    const std::vector<DrawRegion>& regions, double scale) const {
  auto readers = std::vector<DrawRegion>();
  if(!(scale > 0)) {
    return readers;
  }
  auto areas = std::vector<Rect>();
  for(auto& region : regions) {
    if(!region.m_bounds.is_empty()) {
      areas.push_back(grow(region.m_bounds, region.m_margin / scale));
    }
  }
  auto is_found = std::vector<std::vector<bool>>();
  for(auto& entry : m_entries) {
    is_found.emplace_back(entry->m_blurs.size(), false);
  }
  auto checked = std::size_t(0);
  while(checked != areas.size()) {
    auto area = areas[checked];
    ++checked;
    for(auto i = std::size_t(0); i != m_entries.size(); ++i) {
      auto& entry = *m_entries[i];
      for(auto j = std::size_t(0); j != entry.m_blurs.size(); ++j) {
        auto& item = entry.m_items[entry.m_blurs[j]];
        if(is_found[i][j] ||
            !is_touching(item.m_bounds, item.m_margin, scale, area)) {
          continue;
        }
        is_found[i][j] = true;
        readers.push_back(DrawRegion(item.m_bounds, item.m_margin));
        areas.push_back(grow(item.m_bounds, item.m_margin / scale));
      }
    }
  }
  return readers;
}

bool Drawing::is_empty(const Rect& region, double scale) const {
  if(region.is_empty() || !(scale > 0)) {
    return true;
  }
  return std::none_of(m_entries.begin(), m_entries.end(),
    [&] (const auto& entry) {
      return is_touching(entry->m_bounds, entry->m_margin, scale, region) &&
        entry->m_index.contains(region, scale, entry->m_items);
    });
}

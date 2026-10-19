#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <utility>
#include "Rellu/Commands/ArtboardPlacement.hpp"

using namespace Rellu;

namespace {
  constexpr auto DASH = std::string_view("\xE2\x80\x93");
  constexpr auto VIEW_TOLERANCE = 0.3;
  constexpr auto TITLE_HEIGHT = 22.0;

  bool is_digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
      [] (const auto& c) { return c >= '0' && c <= '9'; });
  }

  std::optional<std::pair<std::string, std::uint64_t>> split_number(
      std::string_view name) {
    for(auto position = std::size_t(1); position < name.size(); ++position) {
      if(!name.substr(position).starts_with(DASH)) {
        continue;
      }
      auto rest = name.substr(position + DASH.size());
      if(rest.starts_with(' ') && is_digits(rest.substr(1))) {
        rest.remove_prefix(1);
      } else if(!is_digits(rest)) {
        continue;
      }
      auto number = std::uint64_t(0);
      auto result =
        std::from_chars(rest.data(), rest.data() + rest.size(), number);
      if(result.ec != std::errc()) {
        continue;
      }
      return std::pair(std::string(name.substr(0, position)), number);
    }
    return std::nullopt;
  }

  std::string format_name(std::string base, std::uint64_t number) {
    if(!base.ends_with(' ')) {
      base += ' ';
    }
    return base + std::string(DASH) + ' ' + std::to_string(number);
  }

  std::vector<Rect> get_frames(const Document& document) {
    auto frames = std::vector<Rect>();
    for(auto& id : document.get_canvas()) {
      if(auto artboard = document.get(id).get_artboard()) {
        frames.push_back(artboard->m_frame);
      }
    }
    return frames;
  }

  bool precedes(const std::optional<Rect>& best, const Rect& frame) {
    return !best || frame.m_y < best->m_y ||
      (frame.m_y == best->m_y && frame.m_x < best->m_x);
  }

  std::optional<Rect> pick_row(const std::vector<Rect>& selected,
      const std::vector<Rect>& frames, const Rect& view) {
    auto pick = [&] (const std::vector<Rect>& candidates, auto is_eligible) {
      auto best = std::optional<Rect>();
      for(auto& frame : candidates) {
        if(is_eligible(frame) && precedes(best, frame)) {
          best = frame;
        }
      }
      return best;
    };
    auto is_contained = [&] (const Rect& frame) {
      return view.contains(frame);
    };
    if(auto row = pick(selected, is_contained)) {
      return row;
    } else if(auto row = pick(frames, is_contained)) {
      return row;
    }
    return pick(frames, [&] (const Rect& frame) {
      return view.intersects(frame);
    });
  }

  Rect push_right(Rect& bounds, Rect row, std::vector<Rect> frames) {
    bounds.m_x = row.get_right() + ARTBOARD_SPACING;
    bounds.m_y = row.m_y;
    std::stable_sort(frames.begin(), frames.end(),
      [] (const auto& left, const auto& right) {
        return left.m_x < right.m_x ||
          (left.m_x == right.m_x && left.m_y < right.m_y);
      });
    for(auto& frame : frames) {
      if(frame.m_x > bounds.get_right()) {
        break;
      }
      if(frame.intersects(bounds)) {
        bounds.m_x = frame.get_right() + ARTBOARD_SPACING;
        row = frame;
      }
    }
    return row;
  }

  void push_down(Rect& bounds, const Rect& row, std::vector<Rect> frames) {
    bounds.m_x = row.m_x;
    bounds.m_y = row.get_bottom() + ARTBOARD_ROW_SPACING;
    std::stable_sort(frames.begin(), frames.end(),
      [] (const auto& left, const auto& right) {
        return left.m_y < right.m_y ||
          (left.m_y == right.m_y && left.m_x < right.m_x);
      });
    for(auto& frame : frames) {
      if(frame.m_y > bounds.get_bottom()) {
        break;
      }
      if(frame.intersects(bounds)) {
        bounds.m_y = frame.get_bottom() + ARTBOARD_ROW_SPACING;
      }
    }
  }

  bool is_off_view(const Rect& view, const Rect& bounds, const Rect& last) {
    return !(last.m_x + last.m_width / 2 < view.get_right()) &&
      bounds.m_x > view.m_x + view.m_width * (1 + VIEW_TOLERANCE);
  }

  double snap(double value) {
    return std::floor(value + 0.5);
  }
}

std::string Rellu::make_artboard_name(
    const Document& document, std::string_view name, bool is_kept) {
  auto base = std::string(name);
  auto number = std::uint64_t(1);
  if(auto split = split_number(name)) {
    base = std::move(split->first);
    number = split->second;
  }
  auto names = std::unordered_set<std::string>();
  for(auto& id : document.get_canvas()) {
    auto& candidate = document.get(id).m_header.m_name;
    if(candidate.starts_with(base)) {
      names.insert(candidate);
    }
  }
  if(is_kept && !names.contains(std::string(name))) {
    return std::string(name);
  }
  auto result = format_name(base, number);
  while(names.contains(result)) {
    ++number;
    result = format_name(base, number);
  }
  return result;
}

Rect Rellu::place_artboards(const Document& document,
    const std::vector<NodeId>& selection, const Rect& view,
    const Rect& bounds) {
  auto frames = get_frames(document);
  auto selected = std::vector<Rect>();
  for(auto& id : selection) {
    if(auto node = document.find(id)) {
      if(auto artboard = node->get_artboard()) {
        selected.push_back(artboard->m_frame);
      }
    }
  }
  auto placed = bounds;
  if(auto row = pick_row(selected, frames, view)) {
    auto last = push_right(placed, *row, frames);
    if(is_off_view(view, placed, last)) {
      push_down(placed, *row, frames);
    }
  } else {
    auto center = Rect(view.m_x + (view.m_width - bounds.m_width) / 2,
      view.m_y + (view.m_height - bounds.m_height) / 2 - ARTBOARD_ROW_SPACING,
      0, 0);
    push_down(placed, center, frames);
  }
  placed.m_x = snap(placed.m_x);
  placed.m_y = snap(placed.m_y);
  return placed;
}

Rect Rellu::place_artboard_at(
    const Document& document, Size size, Point point, const Rect& view) {
  auto frames = get_frames(document);
  auto place = [&] (double x, double y) {
    return Rect(snap(x), snap(y), size.m_width, size.m_height);
  };
  if(frames.empty()) {
    return place(view.m_x + (view.m_width - size.m_width) / 2,
      view.m_y + (view.m_height - size.m_height) / 2);
  }
  auto top = frames.front();
  auto bottom = frames.front();
  auto nearest = frames.front();
  auto vertical = std::abs(nearest.get_center().m_y - point.m_y);
  auto horizontal = std::abs(nearest.get_center().m_x - point.m_x);
  for(auto i = std::size_t(1); i != frames.size(); ++i) {
    auto& frame = frames[i];
    if(frame.get_bottom() > bottom.get_bottom() ||
        (frame.get_bottom() == bottom.get_bottom() && frame.m_x < bottom.m_x)) {
      bottom = frame;
    }
    if(frame.m_y < top.m_y || (frame.m_y == top.m_y && frame.m_x < top.m_x)) {
      top = frame;
    }
    auto distance = std::abs(frame.get_center().m_y - point.m_y);
    auto offset = std::abs(frame.get_center().m_x - point.m_x);
    if(vertical > distance || (vertical == distance && horizontal > offset)) {
      nearest = frame;
      vertical = distance;
      horizontal = offset;
    }
  }
  if(point.m_y < top.m_y - TITLE_HEIGHT) {
    return place(top.m_x, top.m_y - ARTBOARD_ROW_SPACING - size.m_height);
  } else if(point.m_y > bottom.get_bottom()) {
    return place(bottom.m_x, bottom.get_bottom() + ARTBOARD_ROW_SPACING);
  }
  auto row = Rect(nearest.m_x, nearest.m_y, size.m_width, size.m_height);
  auto left = nearest;
  auto right = nearest;
  for(auto& frame : frames) {
    if(frame.m_y > row.get_bottom() || frame.get_bottom() < row.m_y) {
      continue;
    }
    if(frame.m_x < left.m_x) {
      left = frame;
    }
    if(frame.get_right() > right.get_right()) {
      right = frame;
    }
  }
  if(std::abs(point.m_x - left.m_x) < std::abs(right.get_right() - point.m_x)) {
    return place(left.m_x - size.m_width - ARTBOARD_SPACING, nearest.m_y);
  }
  return place(right.get_right() + ARTBOARD_SPACING, nearest.m_y);
}

std::size_t Rellu::get_artboard_index(const Document& document) {
  auto& canvas = document.get_canvas();
  for(auto i = canvas.size(); i != 0; --i) {
    if(document.get(canvas[i - 1]).get_artboard()) {
      return i;
    }
  }
  return 0;
}

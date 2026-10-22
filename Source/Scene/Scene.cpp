#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Scene/Scene.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto GHOST_EXTENT = 1e6;

  void add_rect(Path& path, const Rect& rect) {
    path.move_to(Point(rect.get_left(), rect.get_top()));
    path.line_to(Point(rect.get_right(), rect.get_top()));
    path.line_to(Point(rect.get_right(), rect.get_bottom()));
    path.line_to(Point(rect.get_left(), rect.get_bottom()));
    path.close();
  }
  constexpr auto DRAWN = ChangeMask::VISIBILITY | ChangeMask::TRANSFORM |
    ChangeMask::OPACITY | ChangeMask::BLEND_MODE | ChangeMask::ISOLATION |
    ChangeMask::GEOMETRY | ChangeMask::STYLE | ChangeMask::TEXT |
    ChangeMask::CHILDREN | ChangeMask::ROLES | ChangeMask::REFERENCE;
}

Scene::Scene(Document& document, double tolerance)
  : m_document(&document),
    m_builder(document, tolerance),
    m_drawing(std::make_shared<Drawing>()),
    m_view(m_drawing),
    m_is_ghost_dirty(false),
    m_is_changed(true),
    m_index_rate(0),
    m_subscription(document.subscribe([this] (const auto& changes) {
      on_change(changes);
    })) {
  m_builder.set_text_deferred(true);
}

Scene::~Scene() = default;

double Scene::get_tolerance() const {
  return m_builder.get_tolerance();
}

void Scene::set_tolerance(double tolerance) {
  if(tolerance == m_builder.get_tolerance()) {
    return;
  }
  m_builder.set_tolerance(tolerance);
  for(auto& entry : m_fragments) {
    auto& dependencies = entry.second.m_dependencies;
    if(tolerance < dependencies.m_minimum_tolerance ||
        tolerance > dependencies.m_maximum_tolerance) {
      entry.second.m_is_dirty = true;
      m_is_changed = true;
    }
  }
}

const std::optional<Matrix>& Scene::get_text_transform() const {
  return m_text_transform;
}

void Scene::set_text_transform(const std::optional<Matrix>& transform) {
  if(transform == m_text_transform) {
    return;
  }
  m_text_transform = transform;
  mark_text();
}

const std::optional<Rect>& Scene::get_text_region() const {
  return m_text_region;
}

void Scene::set_text_region(const std::optional<Rect>& region) {
  if(region == m_text_region) {
    return;
  }
  m_text_region = region;
  mark_text();
}

void Scene::set_edited_text(const NodeId& id) {
  auto previous = m_builder.get_edited_text();
  if(id == previous) {
    return;
  }
  m_builder.set_edited_text(id);
  m_is_changed = true;
  for(auto& changed : {previous, id}) {
    if(!changed.is_nil()) {
      mark(changed, false);
    }
  }
}

const std::vector<NodeId>& Scene::get_ghosts() const {
  return m_ghosts;
}

void Scene::set_ghosts(std::vector<NodeId> ids) {
  if(ids == m_ghosts) {
    return;
  }
  m_ghosts = std::move(ids);
  m_is_ghost_dirty = true;
  m_is_changed = true;
}

void Scene::build_ghosts() {
  m_ghost_lists.clear();
  for(auto& id : m_ghosts) {
    auto node = m_document->find(id);
    if(!node || node->get_artboard()) {
      continue;
    }
    auto artboard = static_cast<const Node*>(nullptr);
    auto ancestor = m_document->find(node->m_parent);
    for(auto depth = 0; ancestor && depth != MAXIMUM_DEPTH; ++depth) {
      if(ancestor->get_artboard()) {
        artboard = ancestor;
        break;
      }
      ancestor = m_document->find(ancestor->m_parent);
    }
    if(!artboard || !artboard->m_parent.is_nil()) {
      continue;
    }
    auto content = m_builder.build_unclipped(id);
    if(content.empty()) {
      continue;
    }
    auto& frame = artboard->get_artboard()->m_frame;
    auto outline = Path();
    outline.set_fill_rule(FillRule::EVEN_ODD);
    add_rect(outline, inflate(frame, GHOST_EXTENT));
    add_rect(outline, frame);
    auto layer = BeginLayerCommand();
    layer.m_opacity = GHOST_OPACITY;
    layer.m_mask =
      LayerMask(std::make_shared<const Path>(std::move(outline)), Matrix());
    auto list = DrawList();
    list.reserve(content.size() + 2);
    list.push_back(std::move(layer));
    for(auto& command : content) {
      list.push_back(std::move(command));
    }
    list.push_back(EndLayerCommand());
    m_ghost_lists[artboard->get_id()].push_back(
      std::make_shared<const DrawList>(std::move(list)));
  }
  m_is_ghost_dirty = false;
}

const std::shared_ptr<const Drawing>& Scene::get_drawing() const {
  return m_view;
}

bool Scene::is_current() const {
  return !m_is_changed;
}

bool Scene::is_rasterized() const {
  return !m_builder.has_pending_text() && m_rasterized.empty();
}

std::vector<DrawRegion> Scene::update() {
  m_builder.set_text_deferred(false);
  auto regions = std::vector<DrawRegion>();
  do {
    mark_rasterized(std::chrono::nanoseconds::max());
    auto changed = update(nullptr, std::chrono::nanoseconds::max());
    regions.insert(regions.end(), changed.begin(), changed.end());
  } while(m_is_changed || !is_rasterized());
  m_builder.set_text_deferred(true);
  return regions;
}

std::vector<DrawRegion> Scene::update(
    const Rect& region, std::chrono::nanoseconds budget) {
  return update(&region, budget);
}

std::vector<DrawRegion> Scene::rasterize(
    const Rect& region, std::chrono::nanoseconds budget) {
  auto start = std::chrono::steady_clock::now();
  mark_rasterized(budget / 2);
  auto remaining = budget - (std::chrono::steady_clock::now() - start);
  return update(&region, std::max(remaining, std::chrono::nanoseconds(0)));
}

void Scene::mark_rasterized(std::chrono::nanoseconds budget) {
  auto start = std::chrono::steady_clock::now();
  auto ids = m_builder.rasterize_text(budget / 2);
  m_rasterized.insert(m_rasterized.end(), ids.begin(), ids.end());
  auto is_started = false;
  while(!m_rasterized.empty() && (!is_started ||
      std::chrono::steady_clock::now() - start < budget)) {
    mark(m_rasterized.front(), false);
    m_rasterized.pop_front();
    m_is_changed = true;
    is_started = true;
  }
}

void Scene::on_change(const ChangeSet& changes) {
  m_is_changed = true;
  for(auto& id : changes.m_inserted) {
    mark(id, false);
  }
  for(auto& id : changes.m_moved) {
    mark(id, false);
  }
  for(auto& change : changes.m_modified) {
    if(change.m_mask == ChangeMask::TRANSFORM) {
      mark(change.m_id, true);
    } else if(is_set(change.m_mask, DRAWN)) {
      mark(change.m_id, false);
    }
  }
  for(auto& id : changes.m_removed) {
    m_removed.push_back(id);
    mark_dependents(id);
    m_builder.invalidate(id);
  }
  for(auto& id : changes.m_images) {
    mark_dependents(id);
    m_builder.invalidate(id);
  }
}

void Scene::mark(const NodeId& id, bool is_moved) {
  if(is_moved) {
    m_builder.invalidate_transform(id);
  } else {
    m_builder.invalidate(id);
  }
  mark_dependents(id);
  auto node = m_document->find(id);
  if(!node) {
    return;
  }
  for(auto depth = 0; depth != MAXIMUM_DEPTH && !node->m_parent.is_nil();
      ++depth) {
    auto parent = m_document->find(node->m_parent);
    if(!parent) {
      break;
    }
    node = parent;
  }
  auto& root = node->get_id();
  if(auto fragment = m_fragments.find(root); fragment != m_fragments.end()) {
    fragment->second.m_is_dirty = true;
  }
  if(root != id) {
    mark_dependents(root);
  }
}

void Scene::mark_dependents(const NodeId& key) {
  auto dependents = m_dependents.find(key);
  if(dependents == m_dependents.end()) {
    return;
  }
  for(auto& id : dependents->second) {
    if(auto fragment = m_fragments.find(id); fragment != m_fragments.end()) {
      fragment->second.m_is_dirty = true;
    }
  }
}

void Scene::forget(const NodeId& id, Fragment& fragment) {
  for(auto& key : fragment.m_dependencies.m_keys) {
    if(auto dependents = m_dependents.find(key);
        dependents != m_dependents.end()) {
      dependents->second.erase(id);
      if(dependents->second.empty()) {
        m_dependents.erase(dependents);
      }
    }
  }
  fragment.m_dependencies.m_keys.clear();
}

const std::optional<Matrix>& Scene::get_text_transform(
    const Fragment& fragment) const {
  static const auto NONE = std::optional<Matrix>();
  if(!m_text_region) {
    return m_text_transform;
  } else if(!fragment.m_bounds) {
    return NONE;
  }
  auto region = *m_text_region;
  if(fragment.m_dependencies.m_device_transform) {
    region = inflate(region, region.m_width / 4, region.m_height / 4);
  }
  if(fragment.m_bounds->intersects(region)) {
    return m_text_transform;
  }
  return NONE;
}

bool Scene::is_text_stale(const Fragment& fragment) const {
  auto& dependencies = fragment.m_dependencies;
  return dependencies.m_is_device_dependent &&
    (dependencies.m_is_device_mixed ||
      dependencies.m_device_transform != get_text_transform(fragment));
}

void Scene::mark_text() {
  for(auto& entry : m_fragments) {
    if(!entry.second.m_is_dirty && is_text_stale(entry.second)) {
      entry.second.m_is_dirty = true;
      m_is_changed = true;
    }
  }
}

std::vector<DrawRegion> Scene::update(
    const Rect* region, std::chrono::nanoseconds budget) {
  for(auto& id : m_removed) {
    m_builder.discard(id);
  }
  m_removed.clear();
  auto& canvas = m_document->get_canvas();
  auto is_reordered = canvas != m_canvas;
  if(is_reordered) {
    auto present = std::unordered_set<NodeId>(canvas.begin(), canvas.end());
    for(auto entry = m_fragments.begin(); entry != m_fragments.end();) {
      if(present.contains(entry->first)) {
        ++entry;
      } else {
        forget(entry->first, entry->second);
        entry = m_fragments.erase(entry);
      }
    }
    for(auto& id : canvas) {
      if(!m_fragments.contains(id)) {
        m_fragments.emplace(id, Fragment(
          std::vector<std::shared_ptr<const DrawList>>(), SceneDependencies(),
          true));
      }
    }
  }
  auto dirty = std::vector<std::pair<int, NodeId>>();
  for(auto& id : canvas) {
    if(m_fragments.at(id).m_is_dirty) {
      dirty.emplace_back(0, id);
    }
  }
  if(region && dirty.size() > 1) {
    auto indices = std::unordered_map<const DrawList*, std::size_t>();
    auto& lists = m_drawing->get_lists();
    for(auto i = std::size_t(0); i != lists.size(); ++i) {
      indices.emplace(lists[i].get(), i);
    }
    for(auto& entry : dirty) {
      auto& fragment = m_fragments.at(entry.second);
      entry.first = fragment.m_lists.empty() ? 0 : 1;
      for(auto& list : fragment.m_lists) {
        auto index = indices.find(list.get());
        if(index == indices.end() ||
            m_drawing->get_region(index->second).m_bounds.intersects(
              *region)) {
          entry.first = 0;
          break;
        }
      }
    }
    std::stable_sort(dirty.begin(), dirty.end(),
      [] (const auto& left, const auto& right) {
        return left.first < right.first;
      });
  }
  auto start = std::chrono::steady_clock::now();
  auto is_bounded = budget != std::chrono::nanoseconds::max();
  auto deadline = start;
  if(is_bounded) {
    deadline += budget / 2;
  }
  auto count = std::size_t(0);
  auto commands = std::size_t(0);
  for(auto& entry : dirty) {
    if(count != 0 && is_bounded && std::chrono::steady_clock::now() +
        std::chrono::nanoseconds(static_cast<std::int64_t>(
          m_index_rate * static_cast<double>(commands))) >= deadline) {
      break;
    }
    auto& id = entry.second;
    auto& fragment = m_fragments.at(id);
    forget(id, fragment);
    m_builder.set_text_transform(get_text_transform(fragment));
    auto previous = std::move(fragment.m_lists);
    if(is_bounded) {
      fragment.m_lists =
        m_builder.build_lists(id, fragment.m_dependencies, deadline);
    } else {
      fragment.m_lists = m_builder.build_lists(id, fragment.m_dependencies);
    }
    for(auto& list : fragment.m_lists) {
      if(std::find(previous.begin(), previous.end(), list) ==
          previous.end()) {
        commands += list->size();
      }
    }
    fragment.m_is_dirty = false;
    for(auto& key : fragment.m_dependencies.m_keys) {
      m_dependents[key].insert(id);
    }
    ++count;
  }
  m_is_changed = count != dirty.size();
  if(count != 0 && !m_ghosts.empty()) {
    m_is_ghost_dirty = true;
  }
  auto is_ghosted = m_is_ghost_dirty;
  if(is_ghosted) {
    build_ghosts();
  }
  if(count == 0 && !is_reordered && !is_ghosted) {
    return {};
  }
  auto built = std::unordered_set<NodeId>();
  for(auto i = std::size_t(0); i != count; ++i) {
    built.insert(dirty[i].second);
  }
  auto lists = std::vector<std::shared_ptr<const DrawList>>();
  lists.reserve(m_drawing->get_lists().size() + count);
  auto ranges = std::vector<std::pair<NodeId, std::size_t>>();
  for(auto& id : canvas) {
    if(auto ghosts = m_ghost_lists.find(id); ghosts != m_ghost_lists.end()) {
      lists.insert(lists.end(), ghosts->second.begin(), ghosts->second.end());
    }
    auto& fragment = m_fragments.at(id).m_lists;
    if(built.contains(id)) {
      ranges.emplace_back(id, lists.size());
    }
    lists.insert(lists.end(), fragment.begin(), fragment.end());
  }
  m_canvas = canvas;
  auto indexing = std::chrono::steady_clock::now();
  auto regions = m_drawing->set_lists(std::move(lists));
  if(commands != 0) {
    auto rate = std::chrono::duration<double, std::nano>(
      std::chrono::steady_clock::now() - indexing).count() /
      static_cast<double>(commands);
    m_index_rate = (m_index_rate + rate) / 2;
  }
  for(auto& range : ranges) {
    auto& fragment = m_fragments.at(range.first);
    fragment.m_bounds = std::nullopt;
    for(auto i = range.second; i != range.second + fragment.m_lists.size();
        ++i) {
      auto bounds = m_drawing->get_region(i).m_bounds;
      if(fragment.m_bounds) {
        fragment.m_bounds = unite(*fragment.m_bounds, bounds);
      } else {
        fragment.m_bounds = bounds;
      }
    }
    if(is_text_stale(fragment)) {
      fragment.m_is_dirty = true;
      m_is_changed = true;
    }
  }
  return regions;
}

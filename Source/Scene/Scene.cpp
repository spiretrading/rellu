#include <algorithm>
#include <utility>
#include "Rellu/Scene/Scene.hpp"

using namespace Rellu;

namespace {
  constexpr auto MAXIMUM_DEPTH = 256;
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
    m_is_changed(true),
    m_subscription(document.subscribe([this] (const auto& changes) {
      on_change(changes);
    })) {}

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

const std::shared_ptr<const Drawing>& Scene::get_drawing() const {
  return m_view;
}

bool Scene::is_current() const {
  return !m_is_changed;
}

std::vector<DrawRegion> Scene::update() {
  return update(nullptr, std::chrono::nanoseconds::max());
}

std::vector<DrawRegion> Scene::update(
    const Rect& region, std::chrono::nanoseconds budget) {
  return update(&region, budget);
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
  auto count = std::size_t(0);
  for(auto& entry : dirty) {
    if(count != 0 && std::chrono::steady_clock::now() - start >= budget) {
      break;
    }
    auto& id = entry.second;
    auto& fragment = m_fragments.at(id);
    forget(id, fragment);
    fragment.m_lists = m_builder.build_lists(id, fragment.m_dependencies);
    fragment.m_is_dirty = false;
    for(auto& key : fragment.m_dependencies.m_keys) {
      m_dependents[key].insert(id);
    }
    ++count;
  }
  m_is_changed = count != dirty.size();
  if(count == 0 && !is_reordered) {
    return {};
  }
  auto lists = std::vector<std::shared_ptr<const DrawList>>();
  lists.reserve(m_drawing->get_lists().size() + count);
  for(auto& id : canvas) {
    auto& fragment = m_fragments.at(id).m_lists;
    lists.insert(lists.end(), fragment.begin(), fragment.end());
  }
  m_canvas = canvas;
  return m_drawing->set_lists(std::move(lists));
}

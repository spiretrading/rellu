#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Prototype/Easing.hpp"
#include "Rellu/Prototype/PreviewPlayer.hpp"
#include "Rellu/Prototype/StageDocument.hpp"

using namespace Rellu;

namespace {
  constexpr auto TAP = std::size_t(0);
  constexpr auto HOVER = std::size_t(1);
  constexpr auto DRAG = std::size_t(2);

  bool is_instant(const Animation& animation) {
    return animation.m_transition == Transition::NONE ||
      !(animation.m_duration > 0);
  }

  const ComponentInstance* get_instance(const Node& node) {
    if(auto group = node.get_group(); group && group->m_component_instance) {
      return &*group->m_component_instance;
    }
    return nullptr;
  }

  std::string describe(const NodeId& id) {
    return to_string(id);
  }
}

PreviewPlayer::PreviewPlayer(
    std::unique_ptr<Document> stage, TextMeasurer measurer)
    : m_stage(std::move(stage)),
      m_measurer(std::move(measurer)),
      m_time(0),
      m_is_dragging(false) {
  if(!m_stage) {
    throw std::invalid_argument("No stage.");
  }
  prepare_stage();
}

PreviewPlayer::~PreviewPlayer() = default;

Document& PreviewPlayer::get_stage() const {
  return *m_stage;
}

void PreviewPlayer::start(const NodeId& artboard) {
  auto node = m_stage->find(artboard);
  if(!node || !node->get_artboard() || !m_stage->is_on_canvas(artboard)) {
    throw std::invalid_argument("Not an artboard: " + describe(artboard));
  }
  finish_animations();
  for(auto& instance : m_instances) {
    if(instance.second.m_current != instance.second.m_original) {
      switch_state(instance.first, instance.second.m_original, Animation());
    }
  }
  m_hovers.clear();
  m_history.clear();
  m_overlays.clear();
  m_press.reset();
  m_is_dragging = false;
  cancel_timers();
  m_screen = Screen(artboard, 0, Animation());
  schedule(artboard);
}

void PreviewPlayer::start(const PreviewPosition& position) {
  auto is_artboard = [&] (const NodeId& id) {
    auto node = m_stage->find(id);
    return node && node->get_artboard() && m_stage->is_on_canvas(id);
  };
  auto artboard = position.m_artboard;
  if(!is_artboard(artboard)) {
    artboard = NodeId();
    for(auto& id : m_stage->get_canvas()) {
      if(is_artboard(id) && !std::ranges::any_of(m_fixed_layers,
          [&] (const auto& entry) { return entry.second == id; })) {
        artboard = id;
        break;
      }
    }
    if(artboard.is_nil()) {
      m_screen.reset();
      return;
    }
    start(artboard);
    return;
  }
  start(artboard);
  m_screen->m_scroll =
    std::clamp(position.m_scroll, 0.0, get_maximum_scroll(artboard));
  for(auto& id : position.m_history) {
    if(is_artboard(id)) {
      m_history.push_back(Screen(id, 0, Animation()));
    }
  }
}

PreviewPosition PreviewPlayer::get_position() const {
  auto position = PreviewPosition();
  if(m_screen) {
    position.m_artboard = m_screen->m_artboard;
    position.m_scroll = m_screen->m_scroll;
  }
  position.m_history = get_history();
  return position;
}

const NodeId& PreviewPlayer::get_artboard() const {
  static const auto NONE = NodeId();
  if(!m_screen) {
    return NONE;
  }
  return m_screen->m_artboard;
}

Size PreviewPlayer::get_viewport() const {
  if(!m_screen) {
    return Size(0, 0);
  }
  return get_size(m_screen->m_artboard);
}

double PreviewPlayer::get_scroll() const {
  if(!m_screen) {
    return 0;
  }
  return m_screen->m_scroll;
}

std::vector<NodeId> PreviewPlayer::get_history() const {
  auto history = std::vector<NodeId>();
  for(auto& screen : m_history) {
    history.push_back(screen.m_artboard);
  }
  return history;
}

const std::vector<PreviewOverlay>& PreviewPlayer::get_overlays() const {
  return m_overlays;
}

StateId PreviewPlayer::get_state(const NodeId& instance) const {
  auto entry = m_instances.find(instance);
  if(entry == m_instances.end() ||
      entry->second.m_current == entry->second.m_original) {
    return StateId();
  }
  return entry->second.m_current;
}

std::vector<PreviewLayer> PreviewPlayer::get_layers() const {
  auto layers = std::vector<PreviewLayer>();
  if(!m_screen) {
    return layers;
  }
  auto append = [&] (std::vector<PreviewLayer> added) {
    layers.insert(layers.end(), added.begin(), added.end());
  };
  auto screen = std::ranges::find_if(m_animations, [] (const auto& entry) {
    return entry.m_kind == Kind::SCREEN;
  });
  if(screen == m_animations.end() || screen->m_auto) {
    append(get_screen_layers(*m_screen, Point(0, 0), 1));
  } else {
    auto& animation = screen->m_animation;
    auto progress = ease(animation.m_easing,
      (m_time - screen->m_start) / animation.m_duration);
    if(screen->m_is_reversed) {
      progress = 1 - progress;
    }
    auto size = get_size(screen->m_to.m_artboard);
    auto width = size.m_width;
    auto height = size.m_height;
    auto from = Point(0, 0);
    auto to = Point(0, 0);
    auto opacity = 1.0;
    switch(animation.m_transition) {
      case Transition::DISSOLVE:
        opacity = std::clamp(progress, 0.0, 1.0);
        break;
      case Transition::SLIDE_LEFT:
        to = Point(width * (1 - progress), 0);
        break;
      case Transition::SLIDE_RIGHT:
        to = Point(-width * (1 - progress), 0);
        break;
      case Transition::SLIDE_UP:
        to = Point(0, height * (1 - progress));
        break;
      case Transition::SLIDE_DOWN:
        to = Point(0, -height * (1 - progress));
        break;
      case Transition::PUSH_LEFT:
        from = Point(-width * progress, 0);
        to = Point(width * (1 - progress), 0);
        break;
      case Transition::PUSH_RIGHT:
        from = Point(width * progress, 0);
        to = Point(-width * (1 - progress), 0);
        break;
      case Transition::PUSH_UP:
        from = Point(0, -height * progress);
        to = Point(0, height * (1 - progress));
        break;
      case Transition::PUSH_DOWN:
        from = Point(0, height * progress);
        to = Point(0, -height * (1 - progress));
        break;
      default:
        break;
    }
    if(animation.m_transition != Transition::NONE) {
      append(get_screen_layers(screen->m_from, from, 1));
    }
    append(get_screen_layers(screen->m_to, to, opacity));
  }
  auto viewport = get_viewport();
  auto add_overlay = [&] (const PreviewOverlay& overlay, double progress) {
    auto size = get_size(overlay.m_artboard);
    auto frame = m_stage->get(overlay.m_artboard).get_artboard()->m_frame;
    auto position = overlay.m_position;
    auto opacity = 1.0;
    auto start = position;
    switch(overlay.m_animation.m_transition) {
      case Transition::DISSOLVE:
        opacity = std::clamp(progress, 0.0, 1.0);
        break;
      case Transition::SLIDE_LEFT:
      case Transition::PUSH_LEFT:
        start.m_x = viewport.m_width;
        break;
      case Transition::SLIDE_RIGHT:
      case Transition::PUSH_RIGHT:
        start.m_x = -frame.m_width;
        break;
      case Transition::SLIDE_UP:
      case Transition::PUSH_UP:
        start.m_y = viewport.m_height;
        break;
      case Transition::SLIDE_DOWN:
      case Transition::PUSH_DOWN:
        start.m_y = -frame.m_height;
        break;
      default:
        break;
    }
    position = Point(start.m_x + (position.m_x - start.m_x) * progress,
      start.m_y + (position.m_y - start.m_y) * progress);
    layers.push_back(PreviewLayer(
      overlay.m_artboard, NodeId(), position, size, 0, opacity));
  };
  auto get_progress = [&] (const Animating& animating) {
    auto progress = ease(animating.m_animation.m_easing,
      (m_time - animating.m_start) / animating.m_animation.m_duration);
    if(animating.m_is_reversed) {
      return 1 - progress;
    }
    return progress;
  };
  for(auto& overlay : m_overlays) {
    auto progress = 1.0;
    for(auto& animating : m_animations) {
      if(animating.m_kind == Kind::OVERLAY && !animating.m_is_reversed &&
          animating.m_overlay && *animating.m_overlay == overlay) {
        progress = get_progress(animating);
      }
    }
    add_overlay(overlay, progress);
  }
  for(auto& animating : m_animations) {
    if(animating.m_kind == Kind::OVERLAY && animating.m_is_reversed &&
        animating.m_overlay) {
      add_overlay(*animating.m_overlay, get_progress(animating));
    }
  }
  return layers;
}

double PreviewPlayer::get_time() const {
  return m_time;
}

bool PreviewPlayer::is_animating() const {
  return !m_animations.empty();
}

std::optional<double> PreviewPlayer::get_next_time() const {
  auto next = std::optional<double>();
  for(auto& timer : m_timers) {
    if(!next || timer.m_time < *next) {
      next = timer.m_time;
    }
  }
  return next;
}

bool PreviewPlayer::advance(double seconds) {
  constexpr auto MAXIMUM_EVENTS = 10000;
  auto is_changed = !m_animations.empty();
  auto end = m_time + std::max(seconds, 0.0);
  for(auto count = 0; count != MAXIMUM_EVENTS; ++count) {
    auto next = get_next_time();
    auto finishing = m_animations.end();
    for(auto i = m_animations.begin(); i != m_animations.end(); ++i) {
      auto time = i->m_start + i->m_animation.m_duration;
      if(!next || time <= *next) {
        next = time;
        finishing = i;
      }
    }
    if(!next || *next > end) {
      break;
    }
    m_time = std::max(m_time, *next);
    if(finishing != m_animations.end()) {
      auto animating = std::move(*finishing);
      m_animations.erase(finishing);
      finish(animating);
      is_changed = true;
      continue;
    }
    auto due = std::ranges::min_element(m_timers, [] (const auto& left,
        const auto& right) {
      return left.m_time < right.m_time;
    });
    auto id = due->m_interaction;
    auto node = due->m_node;
    m_timers.erase(due);
    if(auto interaction = m_interactions.find(id);
        interaction != m_interactions.end() && is_live(*interaction->second)) {
      is_changed = perform(*interaction->second, node) || is_changed;
    }
  }
  m_time = std::max(m_time, end);
  for(auto& animating : m_animations) {
    if(animating.m_auto) {
      animating.m_auto->apply(ease(animating.m_animation.m_easing,
        (m_time - animating.m_start) / animating.m_animation.m_duration));
    }
  }
  return is_changed;
}

bool PreviewPlayer::press(Point position) {
  m_press = position;
  m_is_dragging = false;
  m_drag_point.reset();
  m_drag_group = NodeId();
  return update_hover(position);
}

bool PreviewPlayer::move(Point position) {
  if(m_press && m_is_dragging && m_drag_point) {
    auto delta = Point(
      m_drag_point->m_x - position.m_x, m_drag_point->m_y - position.m_y);
    m_drag_point = position;
    auto is_scrolled = m_stage->find(m_drag_group) &&
      scroll_group(m_drag_group, delta);
    return update_hover(position) || is_scrolled;
  }
  if(m_press && !m_is_dragging) {
    auto distance = std::hypot(
      position.m_x - m_press->m_x, position.m_y - m_press->m_y);
    if(distance >= DRAG_THRESHOLD) {
      m_is_dragging = true;
      auto matches = find_all(*m_press, DRAG);
      if(!matches.empty()) {
        return perform(*matches.front().second, matches.front().first);
      }
      auto groups = find_scroll_groups(*m_press);
      if(!groups.empty()) {
        m_drag_group = groups.front();
        m_drag_point = *m_press;
        return move(position);
      }
    }
  }
  return update_hover(position);
}

bool PreviewPlayer::release(Point position) {
  if(!m_press) {
    return false;
  }
  auto is_tap = !m_is_dragging;
  m_press.reset();
  m_is_dragging = false;
  m_drag_point.reset();
  m_drag_group = NodeId();
  if(!is_tap) {
    return false;
  }
  if(m_link_handler) {
    if(auto url = find_text_link(position); !url.empty()) {
      m_link_handler(url);
      return true;
    }
  }
  auto matches = find_all(position, TAP);
  if(!matches.empty()) {
    return perform(*matches.front().second, matches.front().first);
  }
  if(!m_overlays.empty()) {
    auto& top = m_overlays.back();
    auto size = get_size(top.m_artboard);
    if(position.m_x < top.m_position.m_x ||
        position.m_y < top.m_position.m_y ||
        position.m_x >= top.m_position.m_x + size.m_width ||
        position.m_y >= top.m_position.m_y + size.m_height) {
      return hide_overlay();
    }
  }
  return false;
}

bool PreviewPlayer::leave() {
  m_press.reset();
  m_is_dragging = false;
  m_drag_point.reset();
  m_drag_group = NodeId();
  auto is_changed = false;
  while(!m_hovers.empty()) {
    auto hover = m_hovers.back();
    m_hovers.pop_back();
    if(!hover.m_instance.is_nil()) {
      is_changed = switch_state(hover.m_instance, hover.m_state,
        hover.m_animation) || is_changed;
    }
  }
  return is_changed;
}

bool PreviewPlayer::scroll(double delta) {
  if(!m_screen) {
    return false;
  }
  auto scroll = std::clamp(m_screen->m_scroll + delta, 0.0,
    get_maximum_scroll(m_screen->m_artboard));
  if(scroll == m_screen->m_scroll) {
    return false;
  }
  m_screen->m_scroll = scroll;
  return true;
}

bool PreviewPlayer::scroll(Point position, Point delta) {
  for(auto& group : find_scroll_groups(position)) {
    auto shift = delta;
    if(m_stage->get(group).get_group()->m_scroll_group->m_direction ==
        ScrollDirection::HORIZONTAL && shift.m_x == 0) {
      shift = Point(shift.m_y, 0);
    }
    if(scroll_group(group, shift)) {
      return true;
    }
  }
  return scroll(delta.m_y);
}

Point PreviewPlayer::get_scroll(const NodeId& group) const {
  if(auto entry = m_scrolls.find(group); entry != m_scrolls.end()) {
    return entry->second;
  }
  return Point(0, 0);
}

void PreviewPlayer::set_link_handler(
    std::function<void (const std::string& url)> handler) {
  m_link_handler = std::move(handler);
}

void PreviewPlayer::set_text_link_finder(TextLinkFinder finder) {
  m_text_link_finder = std::move(finder);
}

bool PreviewPlayer::press_key(const KeyChord& chord) {
  if(!m_screen || chord.m_key.empty()) {
    return false;
  }
  auto roots = std::vector<NodeId>();
  if(!m_overlays.empty()) {
    roots.push_back(m_overlays.back().m_artboard);
  } else {
    roots.push_back(m_screen->m_artboard);
    if(auto fixed = m_fixed_layers.find(m_screen->m_artboard);
        fixed != m_fixed_layers.end()) {
      roots.push_back(fixed->second);
    }
  }
  auto match = static_cast<const Interaction*>(nullptr);
  auto source = NodeId();
  for(auto& root : roots) {
    auto nodes = std::vector<NodeId>();
    collect(root, nodes);
    for(auto& id : nodes) {
      for(auto interaction : get_effective(id)) {
        auto key = std::get_if<KeyTrigger>(&interaction->m_trigger);
        if(key && interaction->m_is_enabled &&
            key->m_device == KeyDevice::KEYBOARD &&
            parse_key_chord(key->m_keys) == chord) {
          match = interaction;
          source = id;
        }
      }
    }
  }
  if(!match) {
    return false;
  }
  return perform(*match, source);
}

bool PreviewPlayer::trigger(const InteractionId& id) {
  auto interaction = m_interactions.find(id);
  if(interaction == m_interactions.end()) {
    report("Interaction not found: " + describe(id));
    return false;
  }
  return perform(*interaction->second, interaction->second->m_owner);
}

const std::vector<std::string>& PreviewPlayer::get_reports() const {
  return m_reports;
}

const Interaction* PreviewPlayer::find_interaction(
    Point position, const Trigger& trigger) const {
  auto matches = find_all(position, trigger.index());
  if(matches.empty()) {
    return nullptr;
  }
  return matches.front().second;
}

void PreviewPlayer::prepare_stage() {
  for(auto& interaction : m_stage->get_prototype().m_interactions) {
    m_owners[interaction.m_owner].push_back(&interaction);
    m_interactions.emplace(interaction.m_id, &interaction);
  }
  m_stage->get_nodes().for_each([&] (const auto& node) {
    if(auto instance = get_instance(node)) {
      for(auto& state : instance->m_states) {
        m_state_roots.emplace(state.m_tree, node.get_id());
      }
    }
  });
  auto fixed = find_fixed_nodes(*m_stage);
  if(fixed.empty()) {
    return;
  }
  auto canvas = m_stage->get_canvas();
  auto transaction = Transaction(*m_stage);
  for(auto& id : canvas) {
    auto& node = m_stage->get(id);
    auto artboard = node.get_artboard();
    if(!artboard || !artboard->m_viewport_height ||
        !(*artboard->m_viewport_height < artboard->m_frame.m_height)) {
      continue;
    }
    auto nodes = std::vector<NodeId>();
    collect(id, nodes);
    auto moved = std::vector<NodeId>();
    for(auto& candidate : nodes) {
      if(!fixed.contains(candidate)) {
        continue;
      }
      auto ancestor = m_stage->get(candidate).m_parent;
      auto is_nested = false;
      while(!ancestor.is_nil() && ancestor != id) {
        if(fixed.contains(ancestor)) {
          is_nested = true;
          break;
        }
        ancestor = m_stage->get(ancestor).m_parent;
      }
      if(!is_nested) {
        moved.push_back(candidate);
      }
    }
    if(moved.empty()) {
      continue;
    }
    auto layer = **std::get_if<Box<Artboard>>(&node.m_payload);
    layer.m_background = NoFill();
    layer.m_layout_grid.reset();
    layer.m_guides.clear();
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = node.m_header.m_name;
    header.m_transform = node.m_header.m_transform;
    auto layer_id = transaction.append(NodeId(), std::move(header),
      Box<Artboard>(std::move(layer)));
    auto inverse = invert(get_world_transform(*m_stage, id));
    for(auto& candidate : moved) {
      auto child = m_stage->get(candidate).m_header;
      if(inverse) {
        child.m_transform =
          *inverse * get_world_transform(*m_stage, child.m_id);
      }
      transaction.move(candidate, layer_id,
        m_stage->get_children(layer_id).size());
      transaction.replace_header(std::move(child));
    }
    m_fixed_layers.emplace(id, layer_id);
  }
  transaction.commit();
  m_stage->clear_history();
}

void PreviewPlayer::report(std::string message) {
  m_reports.push_back(std::move(message));
}

Size PreviewPlayer::get_size(const NodeId& artboard) const {
  auto node = m_stage->find(artboard);
  if(!node || !node->get_artboard()) {
    return Size(0, 0);
  }
  auto& frame = node->get_artboard()->m_frame;
  auto height = frame.m_height;
  if(auto& viewport = node->get_artboard()->m_viewport_height;
      viewport && *viewport > 0 && *viewport < height) {
    height = *viewport;
  }
  return Size(frame.m_width, height);
}

double PreviewPlayer::get_maximum_scroll(const NodeId& artboard) const {
  auto node = m_stage->find(artboard);
  if(!node || !node->get_artboard()) {
    return 0;
  }
  return std::max(0.0,
    node->get_artboard()->m_frame.m_height - get_size(artboard).m_height);
}

std::vector<NodeId> PreviewPlayer::get_roots() const {
  auto roots = std::vector<NodeId>();
  if(!m_screen) {
    return roots;
  }
  roots.push_back(m_screen->m_artboard);
  if(auto fixed = m_fixed_layers.find(m_screen->m_artboard);
      fixed != m_fixed_layers.end()) {
    roots.push_back(fixed->second);
  }
  for(auto& overlay : m_overlays) {
    roots.push_back(overlay.m_artboard);
  }
  return roots;
}

std::vector<const Interaction*> PreviewPlayer::get_effective(
    const NodeId& id) const {
  auto owner = id;
  if(auto alias = m_aliases.find(id); alias != m_aliases.end()) {
    owner = alias->second;
  }
  if(auto instance = m_instances.find(id); instance != m_instances.end() &&
      instance->second.m_current != instance->second.m_original) {
    auto is_shared = false;
    owner = find_tree(id, instance->second.m_current, is_shared);
  }
  if(auto interactions = m_owners.find(owner);
      interactions != m_owners.end()) {
    return interactions->second;
  }
  return std::vector<const Interaction*>();
}

void PreviewPlayer::collect(
    const NodeId& root, std::vector<NodeId>& nodes) const {
  auto pending = std::vector<NodeId>({root});
  while(!pending.empty()) {
    auto id = pending.back();
    pending.pop_back();
    auto node = m_stage->find(id);
    if(!node || !node->m_header.m_is_visible) {
      continue;
    }
    nodes.push_back(id);
    for(auto i = node->m_children.rbegin(); i != node->m_children.rend();
        ++i) {
      pending.push_back(*i);
    }
  }
}

std::vector<std::pair<NodeId, Point>> PreviewPlayer::get_targets(
    Point position) const {
  auto targets = std::vector<std::pair<NodeId, Point>>();
  if(!m_screen) {
    return targets;
  }
  auto origin = [&] (const NodeId& artboard) {
    return m_stage->get(artboard).get_artboard()->m_frame.get_top_left();
  };
  if(!m_overlays.empty()) {
    auto& top = m_overlays.back();
    auto size = get_size(top.m_artboard);
    auto local = Point(position.m_x - top.m_position.m_x,
      position.m_y - top.m_position.m_y);
    if(local.m_x >= 0 && local.m_y >= 0 && local.m_x < size.m_width &&
        local.m_y < size.m_height) {
      auto corner = origin(top.m_artboard);
      targets.emplace_back(top.m_artboard,
        Point(corner.m_x + local.m_x, corner.m_y + local.m_y));
    }
    return targets;
  }
  auto size = get_viewport();
  if(position.m_x < 0 || position.m_y < 0 || position.m_x >= size.m_width ||
      position.m_y >= size.m_height) {
    return targets;
  }
  auto corner = origin(m_screen->m_artboard);
  targets.emplace_back(m_screen->m_artboard, Point(corner.m_x + position.m_x,
    corner.m_y + position.m_y + m_screen->m_scroll));
  if(auto fixed = m_fixed_layers.find(m_screen->m_artboard);
      fixed != m_fixed_layers.end()) {
    targets.emplace_back(fixed->second,
      Point(corner.m_x + position.m_x, corner.m_y + position.m_y));
  }
  return targets;
}

std::vector<std::pair<NodeId, const Interaction*>> PreviewPlayer::find_all(
    Point position, std::size_t trigger) const {
  auto best = std::vector<std::pair<NodeId, const Interaction*>>();
  for(auto& target : get_targets(position)) {
    auto nodes = std::vector<NodeId>();
    collect(target.first, nodes);
    for(auto& id : nodes) {
      auto found = std::vector<std::pair<NodeId, const Interaction*>>();
      for(auto interaction : get_effective(id)) {
        if(interaction->m_is_enabled &&
            interaction->m_trigger.index() == trigger) {
          found.emplace_back(id, interaction);
        }
      }
      if(found.empty()) {
        continue;
      }
      auto bounds = get_world_bounds(
        *m_stage, id, BoundsType::GEOMETRY, m_measurer);
      if(auto node = m_stage->find(id); node && node->get_artboard()) {
        bounds = node->get_artboard()->m_frame;
      }
      if(bounds && bounds->contains(target.second)) {
        best = std::move(found);
      }
    }
  }
  return best;
}

bool PreviewPlayer::is_live(const Interaction& interaction) const {
  auto nodes = std::vector<NodeId>();
  for(auto& root : get_roots()) {
    collect(root, nodes);
  }
  for(auto& id : nodes) {
    for(auto effective : get_effective(id)) {
      if(effective == &interaction) {
        return true;
      }
    }
  }
  return false;
}

StateId PreviewPlayer::get_original_state(const NodeId& instance) const {
  auto node = m_stage->find(instance);
  if(!node) {
    return StateId();
  }
  auto data = get_instance(*node);
  if(!data) {
    return StateId();
  }
  return data->m_state;
}

void PreviewPlayer::finish_animations() {
  while(!m_animations.empty()) {
    auto animating = std::move(m_animations.front());
    m_animations.erase(m_animations.begin());
    finish(animating);
  }
}

void PreviewPlayer::finish(Animating& animating) {
  if(animating.m_auto) {
    animating.m_auto->finish();
  }
  if(!animating.m_discard.is_nil()) {
    discard(animating.m_discard);
  }
  if(animating.m_kind == Kind::SCREEN && m_screen) {
    schedule(m_screen->m_artboard);
  } else if(animating.m_kind == Kind::OVERLAY && !animating.m_is_reversed &&
      animating.m_overlay) {
    schedule(animating.m_overlay->m_artboard);
  }
}

void PreviewPlayer::schedule(const NodeId& root) {
  auto nodes = std::vector<NodeId>();
  collect(root, nodes);
  if(auto fixed = m_fixed_layers.find(root); fixed != m_fixed_layers.end()) {
    collect(fixed->second, nodes);
  }
  for(auto& id : nodes) {
    for(auto interaction : get_effective(id)) {
      if(auto time = std::get_if<TimeTrigger>(&interaction->m_trigger);
          time && interaction->m_is_enabled) {
        std::erase_if(m_timers, [&] (const auto& timer) {
          return timer.m_interaction == interaction->m_id;
        });
        m_timers.push_back(Timer(interaction->m_id, id,
          m_time + std::max(time->m_delay, 0.0)));
      }
    }
  }
}

void PreviewPlayer::cancel_timers() {
  m_timers.clear();
}

bool PreviewPlayer::perform(
    const Interaction& interaction, const NodeId& node) {
  if(!m_screen) {
    return false;
  }
  if(auto transition =
      std::get_if<ArtboardTransition>(&interaction.m_action)) {
    return navigate(transition->m_destination, transition->m_animation,
      transition->m_preserve_scroll);
  } else if(auto overlay = std::get_if<Overlay>(&interaction.m_action)) {
    return show_overlay(*overlay);
  } else if(auto state =
      std::get_if<StateTransition>(&interaction.m_action)) {
    auto instance = find_instance(node, state->m_state);
    if(instance.is_nil()) {
      report("State not found: " + describe(state->m_state));
      return false;
    }
    return switch_state(instance, state->m_state, state->m_animation);
  } else if(std::holds_alternative<PreviousArtboard>(interaction.m_action)) {
    return go_back();
  } else if(auto link = std::get_if<Hyperlink>(&interaction.m_action)) {
    if(link->m_url.empty()) {
      report("Hyperlink without an address");
      return false;
    }
    if(!m_link_handler) {
      report("Hyperlink not opened: " + link->m_url);
      return false;
    }
    m_link_handler(link->m_url);
    return true;
  }
  report("Video playback is not played");
  return false;
}

bool PreviewPlayer::navigate(const NodeId& destination,
    const Animation& animation, bool is_scroll_preserved) {
  auto node = m_stage->find(destination);
  if(!node || !node->get_artboard() || !m_stage->is_on_canvas(destination)) {
    report("Destination not found: " + describe(destination));
    return false;
  }
  finish_animations();
  leave();
  auto from = *m_screen;
  from.m_animation = animation;
  m_history.push_back(from);
  m_overlays.clear();
  cancel_timers();
  auto scroll = 0.0;
  if(is_scroll_preserved) {
    scroll = std::clamp(from.m_scroll, 0.0, get_maximum_scroll(destination));
  }
  auto to = Screen(destination, scroll, animation);
  m_screen = to;
  auto is_auto = animation.m_transition == Transition::AUTO_ANIMATE;
  if(is_instant(animation) ||
      (is_auto && destination == from.m_artboard)) {
    schedule(destination);
    return true;
  }
  auto animating = Animating(Kind::SCREEN, animation, m_time, false, from, to,
    std::nullopt, nullptr);
  if(is_auto) {
    animating.m_auto =
      std::make_unique<AutoAnimation>(*m_stage, from.m_artboard, destination);
  }
  m_animations.push_back(std::move(animating));
  return true;
}

bool PreviewPlayer::go_back() {
  if(!m_overlays.empty()) {
    return hide_overlay();
  }
  if(m_history.empty()) {
    return false;
  }
  finish_animations();
  leave();
  auto previous = m_history.back();
  m_history.pop_back();
  auto current = *m_screen;
  cancel_timers();
  auto animation = previous.m_animation;
  m_screen = Screen(previous.m_artboard, previous.m_scroll, Animation());
  auto is_auto = animation.m_transition == Transition::AUTO_ANIMATE;
  if(is_instant(animation) ||
      (is_auto && previous.m_artboard == current.m_artboard)) {
    schedule(previous.m_artboard);
    return true;
  }
  if(is_auto) {
    auto animating = Animating(Kind::SCREEN, animation, m_time, false,
      current, *m_screen, std::nullopt, nullptr);
    animating.m_auto = std::make_unique<AutoAnimation>(
      *m_stage, current.m_artboard, previous.m_artboard);
    m_animations.push_back(std::move(animating));
    return true;
  }
  auto target = previous;
  target.m_animation = Animation();
  m_animations.push_back(Animating(Kind::SCREEN, animation, m_time, true,
    target, current, std::nullopt, nullptr));
  return true;
}

bool PreviewPlayer::show_overlay(const Overlay& overlay) {
  auto node = m_stage->find(overlay.m_destination);
  if(!node || !node->get_artboard() ||
      !m_stage->is_on_canvas(overlay.m_destination)) {
    report("Destination not found: " + describe(overlay.m_destination));
    return false;
  }
  finish_animations();
  auto shown = PreviewOverlay(
    overlay.m_destination, overlay.m_position, overlay.m_animation);
  m_overlays.push_back(shown);
  if(is_instant(overlay.m_animation) ||
      overlay.m_animation.m_transition == Transition::AUTO_ANIMATE) {
    schedule(overlay.m_destination);
    return true;
  }
  m_animations.push_back(Animating(Kind::OVERLAY, overlay.m_animation,
    m_time, false, *m_screen, *m_screen, shown, nullptr));
  return true;
}

bool PreviewPlayer::hide_overlay() {
  if(m_overlays.empty()) {
    return false;
  }
  finish_animations();
  auto hidden = m_overlays.back();
  m_overlays.pop_back();
  if(!is_instant(hidden.m_animation) &&
      hidden.m_animation.m_transition != Transition::AUTO_ANIMATE) {
    m_animations.push_back(Animating(Kind::OVERLAY, hidden.m_animation,
      m_time, true, *m_screen, *m_screen, hidden, nullptr));
  }
  return true;
}

bool PreviewPlayer::switch_state(const NodeId& instance, const StateId& state,
    const Animation& animation) {
  finish_animations();
  auto node = m_stage->find(instance);
  if(!node) {
    return false;
  }
  auto data = get_instance(*node);
  if(!data) {
    return false;
  }
  auto component = data->m_component;
  auto entry = m_instances.find(instance);
  if(entry == m_instances.end()) {
    auto original = get_original_state(instance);
    entry = m_instances.emplace(instance,
      InstanceState(original, original,
        std::unordered_map<StateId, NodeId>(), false)).first;
  }
  auto& record = entry->second;
  auto target = state;
  auto is_original = [&] (const StateId& id) {
    if(id == record.m_original) {
      return true;
    } else if(!record.m_original.is_nil() || id.is_nil()) {
      return false;
    } else if(id == component) {
      return true;
    }
    auto& components = m_stage->get_resources().m_components;
    auto definition = components.find(component);
    return definition != components.end() &&
      definition->second.m_default_state == id;
  };
  if(is_original(target)) {
    target = record.m_original;
  }
  if(target == record.m_current) {
    return false;
  }
  auto source = NodeId();
  auto is_shared = false;
  if(target == record.m_original) {
    if(auto stored = record.m_storage.find(target);
        stored != record.m_storage.end()) {
      source = stored->second;
    }
  } else {
    source = find_tree(instance, target, is_shared);
  }
  if(source.is_nil() || !m_stage->find(source)) {
    report("State not found: " + describe(state));
    return false;
  }
  auto transaction = Transaction(*m_stage);
  auto destination = NodeId();
  auto removed = NodeId();
  if(record.m_current == record.m_original) {
    if(auto stored = record.m_storage.find(record.m_original);
        stored != record.m_storage.end()) {
      destination = stored->second;
    } else {
      auto header = NodeHeader();
      header.m_id = generate_node_id();
      destination = transaction.insert_root(std::move(header), Group());
      record.m_storage.emplace(record.m_original, destination);
      m_state_roots.emplace(destination, instance);
    }
  } else if(record.m_is_cloned) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    destination = transaction.insert_root(std::move(header), Group());
    removed = destination;
  } else {
    auto is_current_shared = false;
    destination = find_tree(instance, record.m_current, is_current_shared);
  }
  if(destination.is_nil() || !m_stage->find(destination)) {
    report("State not found: " + describe(record.m_current));
    return false;
  }
  auto shown = m_stage->get(instance).m_children;
  for(auto& child : shown) {
    transaction.move(child, destination,
      m_stage->get_children(destination).size());
  }
  if(is_shared) {
    auto pending = std::vector<std::pair<NodeId, NodeId>>();
    for(auto& child : m_stage->get(source).m_children) {
      pending.emplace_back(child, instance);
    }
    for(auto i = std::size_t(0); i != pending.size(); ++i) {
      auto [original, parent] = pending[i];
      auto& copied = m_stage->get(original);
      auto header = copied.m_header;
      header.m_id = generate_node_id();
      auto id = transaction.append(parent, std::move(header),
        copied.m_payload);
      m_aliases.emplace(id, original);
      for(auto& child : copied.m_children) {
        pending.emplace_back(child, id);
      }
    }
  } else {
    auto incoming = m_stage->get(source).m_children;
    for(auto& child : incoming) {
      transaction.move(
        child, instance, m_stage->get_children(instance).size());
    }
  }
  transaction.commit();
  m_stage->clear_history();
  record.m_current = target;
  record.m_is_cloned = is_shared;
  if(!is_instant(animation)) {
    auto screen = m_screen.value_or(Screen(NodeId(), 0, Animation()));
    auto animating = Animating(Kind::STATE, animation, m_time, false, screen,
      screen, std::nullopt, nullptr, removed);
    animating.m_auto = std::make_unique<AutoAnimation>(*m_stage, destination,
      instance, animation.m_transition == Transition::AUTO_ANIMATE);
    m_animations.push_back(std::move(animating));
  } else if(!removed.is_nil()) {
    discard(removed);
  }
  schedule(instance);
  return true;
}

NodeId PreviewPlayer::find_instance(
    const NodeId& node, const StateId& state) const {
  auto current = node;
  if(auto root = m_state_roots.find(node); root != m_state_roots.end()) {
    current = root->second;
  }
  for(auto depth = 0; depth != 256 && !current.is_nil(); ++depth) {
    auto candidate = m_stage->find(current);
    if(!candidate) {
      return NodeId();
    }
    if(auto data = get_instance(*candidate)) {
      if(state == data->m_state || state == data->m_component) {
        return current;
      }
      auto& components = m_stage->get_resources().m_components;
      if(auto component = components.find(data->m_component);
          component != components.end() && !state.is_nil() &&
          component->second.m_default_state == state) {
        return current;
      }
      auto is_shared = false;
      if(!find_tree(current, state, is_shared).is_nil()) {
        return current;
      }
    }
    if(auto root = m_state_roots.find(current);
        root != m_state_roots.end() && root->second != current) {
      current = root->second;
    } else {
      current = candidate->m_parent;
    }
  }
  return NodeId();
}

NodeId PreviewPlayer::find_tree(const NodeId& instance, const StateId& state,
    bool& is_shared) const {
  is_shared = false;
  auto node = m_stage->find(instance);
  if(!node || state.is_nil()) {
    return NodeId();
  }
  auto data = get_instance(*node);
  if(!data) {
    return NodeId();
  }
  for(auto& candidate : data->m_states) {
    if(candidate.m_id == state) {
      return candidate.m_tree;
    }
  }
  auto& components = m_stage->get_resources().m_components;
  if(auto component = components.find(data->m_component);
      component != components.end()) {
    for(auto& candidate : component->second.m_states) {
      if(candidate.m_id == state) {
        is_shared = true;
        return candidate.m_tree;
      }
    }
  }
  return NodeId();
}

void PreviewPlayer::discard(const NodeId& root) {
  if(!m_stage->find(root)) {
    return;
  }
  auto nodes = std::vector<NodeId>({root});
  for(auto i = std::size_t(0); i != nodes.size(); ++i) {
    m_aliases.erase(nodes[i]);
    auto& children = m_stage->get_children(nodes[i]);
    nodes.insert(nodes.end(), children.begin(), children.end());
  }
  auto transaction = Transaction(*m_stage);
  transaction.remove(root);
  transaction.commit();
  m_stage->clear_history();
}

bool PreviewPlayer::update_hover(Point position) {
  auto is_changed = false;
  auto targets = get_targets(position);
  auto is_under = [&] (const NodeId& id) {
    if(!m_stage->find(id)) {
      return false;
    }
    auto bounds =
      get_world_bounds(*m_stage, id, BoundsType::GEOMETRY, m_measurer);
    if(!bounds) {
      return false;
    }
    for(auto& target : targets) {
      if(!bounds->contains(target.second)) {
        continue;
      }
      auto ancestor = id;
      for(auto depth = 0; depth != 256 && !ancestor.is_nil(); ++depth) {
        if(ancestor == target.first) {
          return true;
        }
        ancestor = m_stage->get(ancestor).m_parent;
      }
    }
    return false;
  };
  for(auto i = m_hovers.size(); i-- != 0;) {
    if(is_under(m_hovers[i].m_owner)) {
      continue;
    }
    auto hover = m_hovers[i];
    m_hovers.erase(m_hovers.begin() + i);
    if(!hover.m_instance.is_nil()) {
      is_changed = switch_state(hover.m_instance, hover.m_state,
        hover.m_animation) || is_changed;
    }
  }
  for(auto& target : targets) {
    auto nodes = std::vector<NodeId>();
    collect(target.first, nodes);
    for(auto& id : nodes) {
      if(std::ranges::any_of(m_hovers, [&] (const auto& hover) {
          return hover.m_owner == id;
        })) {
        continue;
      }
      auto hover = static_cast<const Interaction*>(nullptr);
      for(auto interaction : get_effective(id)) {
        if(interaction->m_is_enabled &&
            interaction->m_trigger.index() == HOVER) {
          hover = interaction;
          break;
        }
      }
      if(!hover || !is_under(id)) {
        continue;
      }
      if(auto state = std::get_if<StateTransition>(&hover->m_action)) {
        auto instance = find_instance(id, state->m_state);
        if(instance.is_nil()) {
          report("State not found: " + describe(state->m_state));
          continue;
        }
        auto previous = get_original_state(instance);
        if(auto entry = m_instances.find(instance);
            entry != m_instances.end()) {
          previous = entry->second.m_current;
        }
        m_hovers.push_back(Hover(id, instance, previous, state->m_animation));
        is_changed = switch_state(
          instance, state->m_state, state->m_animation) || is_changed;
      } else {
        m_hovers.push_back(Hover(id, NodeId(), StateId(), Animation()));
        is_changed = perform(*hover, id) || is_changed;
        if(!m_screen) {
          break;
        }
      }
    }
  }
  return is_changed;
}

std::vector<NodeId> PreviewPlayer::find_scroll_groups(Point position) const {
  auto groups = std::vector<NodeId>();
  for(auto& target : get_targets(position)) {
    auto nodes = std::vector<NodeId>();
    collect(target.first, nodes);
    for(auto& id : nodes) {
      auto group = m_stage->get(id).get_group();
      if(!group || !group->m_scroll_group) {
        continue;
      }
      auto inverse = invert(get_world_transform(*m_stage, id));
      if(!inverse) {
        continue;
      }
      auto& scroll = *group->m_scroll_group;
      auto local = inverse->map(target.second);
      auto is_inside_x = scroll.m_direction == ScrollDirection::VERTICAL ||
        local.m_x >= scroll.m_offset.m_x &&
          local.m_x < scroll.m_offset.m_x + scroll.m_viewport.m_width;
      auto is_inside_y = scroll.m_direction == ScrollDirection::HORIZONTAL ||
        local.m_y >= scroll.m_offset.m_y &&
          local.m_y < scroll.m_offset.m_y + scroll.m_viewport.m_height;
      if(is_inside_x && is_inside_y) {
        groups.push_back(id);
      }
    }
  }
  std::ranges::reverse(groups);
  return groups;
}

std::string PreviewPlayer::find_text_link(Point position) const {
  auto url = std::string();
  if(!m_text_link_finder) {
    return url;
  }
  for(auto& target : get_targets(position)) {
    auto nodes = std::vector<NodeId>();
    collect(target.first, nodes);
    for(auto& id : nodes) {
      auto& node = m_stage->get(id);
      if(!node.get_text()) {
        continue;
      }
      auto bounds = get_world_bounds(
        *m_stage, id, BoundsType::GEOMETRY, m_measurer);
      auto inverse = invert(get_world_transform(*m_stage, id));
      if(!bounds || !bounds->contains(target.second) || !inverse) {
        continue;
      }
      if(auto found = m_text_link_finder(node, inverse->map(target.second));
          !found.empty()) {
        url = std::move(found);
      }
    }
  }
  return url;
}

bool PreviewPlayer::scroll_group(const NodeId& group, Point delta) {
  auto& node = m_stage->get(group);
  auto& scroll = *node.get_group()->m_scroll_group;
  if(scroll.m_direction == ScrollDirection::HORIZONTAL) {
    delta.m_y = 0;
  } else if(scroll.m_direction == ScrollDirection::VERTICAL) {
    delta.m_x = 0;
  }
  auto content = std::optional<Rect>();
  for(auto& child : node.m_children) {
    if(auto bounds = get_bounds(*m_stage, child,
        get_transform(*m_stage, child), BoundsType::GEOMETRY, m_measurer)) {
      content = content ? unite(*content, *bounds) : *bounds;
    }
  }
  if(!content) {
    return false;
  }
  auto limit = [] (double value, double start, double end, double first,
      double last) {
    return std::clamp(value, std::min(0.0, start - first),
      std::max(0.0, end - last));
  };
  delta.m_x = limit(delta.m_x, content->get_left(), content->get_right(),
    scroll.m_offset.m_x, scroll.m_offset.m_x + scroll.m_viewport.m_width);
  delta.m_y = limit(delta.m_y, content->get_top(), content->get_bottom(),
    scroll.m_offset.m_y, scroll.m_offset.m_y + scroll.m_viewport.m_height);
  if(delta.m_x == 0 && delta.m_y == 0) {
    return false;
  }
  auto children = node.m_children;
  auto transaction = Transaction(*m_stage);
  for(auto& child : children) {
    set_transform(*m_stage, transaction, child,
      make_translation(-delta.m_x, -delta.m_y) *
        get_transform(*m_stage, child));
  }
  transaction.commit();
  m_stage->clear_history();
  auto& offset = m_scrolls[group];
  offset = Point(offset.m_x + delta.m_x, offset.m_y + delta.m_y);
  return true;
}

std::vector<PreviewLayer> PreviewPlayer::get_screen_layers(
    const Screen& screen, Point position, double opacity) const {
  auto fixed = NodeId();
  if(auto layer = m_fixed_layers.find(screen.m_artboard);
      layer != m_fixed_layers.end()) {
    fixed = layer->second;
  }
  return {PreviewLayer(screen.m_artboard, fixed, position,
    get_size(screen.m_artboard), screen.m_scroll, opacity)};
}

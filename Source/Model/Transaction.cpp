#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  constexpr auto PAYLOAD_MASK = ChangeMask::GEOMETRY | ChangeMask::STYLE |
    ChangeMask::TEXT | ChangeMask::ROLES | ChangeMask::ARTBOARD |
    ChangeMask::REFERENCE;
  constexpr auto END = std::numeric_limits<std::size_t>::max();

  std::invalid_argument make_error(std::string message, const NodeId& id) {
    return std::invalid_argument(message + ": " + to_string(id));
  }

  void validate_payload(const Payload& payload) {
    auto box = std::get_if<Box<Text>>(&payload);
    if(!box) {
      return;
    }
    auto& text = *box;
    if(text->m_runs.empty()) {
      return;
    }
    auto length = std::size_t(0);
    for(auto& run : text->m_runs) {
      length += run.m_length;
    }
    if(length != text->m_content->size()) {
      throw std::invalid_argument("Text runs do not cover the content.");
    }
  }

  bool is_layout_affected(
      const TextStyle& previous, const TextStyle& current) {
    return previous.m_font != current.m_font ||
      !equals(previous.m_letter_spacing, current.m_letter_spacing) ||
      previous.m_transform != current.m_transform ||
      previous.m_script != current.m_script;
  }

  bool is_layout_affected(const Text& previous, const Text& current) {
    if(previous.m_content != current.m_content ||
        previous.m_frame != current.m_frame ||
        previous.m_paragraph_style != current.m_paragraph_style ||
        previous.m_runs.size() != current.m_runs.size() ||
        is_layout_affected(previous.m_base_style, current.m_base_style)) {
      return true;
    }
    for(auto i = std::size_t(0); i != current.m_runs.size(); ++i) {
      auto& previous_run = previous.m_runs[i];
      auto& current_run = current.m_runs[i];
      if(previous_run.m_length != current_run.m_length ||
          is_layout_affected(previous_run.m_style, current_run.m_style)) {
        return true;
      }
    }
    return false;
  }

  bool is_recolored(const Text& previous, const Text& current,
      const StoredRun& run) {
    if(current.m_runs.empty()) {
      return previous.m_base_style.m_color != current.m_base_style.m_color;
    }
    auto start = std::size_t(0);
    for(auto i = std::size_t(0); i != current.m_runs.size() &&
        start < run.m_to; ++i) {
      auto end = start + current.m_runs[i].m_length;
      if(end > run.m_from && previous.m_runs[i].m_style.m_color !=
          current.m_runs[i].m_style.m_color) {
        return true;
      }
      start = end;
    }
    return false;
  }

  void recolor(const Text& previous, Text& current) {
    auto layout = std::optional<StoredLayout>();
    auto& stored = **current.m_layout;
    for(auto i = std::size_t(0); i != stored.m_paragraphs.size(); ++i) {
      auto& lines = stored.m_paragraphs[i].m_lines;
      for(auto j = std::size_t(0); j != lines.size(); ++j) {
        auto& runs = lines[j].m_runs;
        for(auto k = std::size_t(0); k != runs.size(); ++k) {
          if(runs[k].m_color && is_recolored(previous, current, runs[k])) {
            if(!layout) {
              layout.emplace(stored);
            }
            layout->m_paragraphs[i].m_lines[j].m_runs[k].m_color =
              std::nullopt;
          }
        }
      }
    }
    if(layout) {
      current.m_layout = Shared(std::move(*layout));
    }
  }

  ChangeMask diff(const NodeHeader& previous, const NodeHeader& current) {
    auto mask = ChangeMask::NONE;
    if(previous.m_name != current.m_name) {
      mask |= ChangeMask::NAME;
    }
    if(previous.m_is_visible != current.m_is_visible) {
      mask |= ChangeMask::VISIBILITY;
    }
    if(previous.m_is_locked != current.m_is_locked) {
      mask |= ChangeMask::LOCK;
    }
    if(previous.m_transform != current.m_transform ||
        previous.m_transform_3d != current.m_transform_3d) {
      mask |= ChangeMask::TRANSFORM;
    }
    if(!equals(previous.m_opacity, current.m_opacity)) {
      mask |= ChangeMask::OPACITY;
    }
    if(previous.m_blend_mode != current.m_blend_mode) {
      mask |= ChangeMask::BLEND_MODE;
    }
    if(previous.m_is_isolated != current.m_is_isolated) {
      mask |= ChangeMask::ISOLATION;
    }
    if(previous.m_constraints != current.m_constraints) {
      mask |= ChangeMask::CONSTRAINTS;
    }
    if(previous.m_is_marked_for_export != current.m_is_marked_for_export) {
      mask |= ChangeMask::EXPORT;
    }
    return mask;
  }

  ChangeMask diff(const Artboard& previous, const Artboard& current) {
    auto mask = ChangeMask::NONE;
    if(previous.m_frame != current.m_frame) {
      mask |= ChangeMask::GEOMETRY;
    }
    if(previous.m_background != current.m_background) {
      mask |= ChangeMask::STYLE;
    }
    if(!equals(previous.m_viewport_height, current.m_viewport_height) ||
        previous.m_layout_grid != current.m_layout_grid ||
        previous.m_guides != current.m_guides) {
      mask |= ChangeMask::ARTBOARD;
    }
    return mask;
  }

  ChangeMask diff(const Group& previous, const Group& current) {
    if(previous != current) {
      return ChangeMask::ROLES;
    }
    return ChangeMask::NONE;
  }

  ChangeMask diff(const Shape& previous, const Shape& current) {
    auto mask = ChangeMask::NONE;
    if(previous.m_geometry != current.m_geometry) {
      mask |= ChangeMask::GEOMETRY;
    }
    if(previous.m_style != current.m_style) {
      mask |= ChangeMask::STYLE;
    }
    return mask;
  }

  ChangeMask diff(const Text& previous, const Text& current) {
    auto mask = ChangeMask::NONE;
    if(previous.m_frame != current.m_frame) {
      mask |= ChangeMask::GEOMETRY;
    }
    if(previous.m_style != current.m_style) {
      mask |= ChangeMask::STYLE;
    }
    if(previous.m_content != current.m_content ||
        previous.m_base_style != current.m_base_style ||
        previous.m_runs != current.m_runs ||
        previous.m_paragraph_style != current.m_paragraph_style ||
        previous.m_layout != current.m_layout) {
      mask |= ChangeMask::TEXT;
    }
    return mask;
  }

  ChangeMask diff(const Reference& previous, const Reference& current) {
    auto mask = ChangeMask::NONE;
    if(previous.m_source != current.m_source ||
        previous.m_has_children != current.m_has_children) {
      mask |= ChangeMask::REFERENCE;
    }
    if(previous.m_transform != current.m_transform) {
      mask |= ChangeMask::TRANSFORM;
    }
    return mask;
  }

  template<typename T>
  ChangeMask diff(const Box<T>& previous, const Box<T>& current) {
    return diff(*previous, *current);
  }

  ChangeMask diff(const Node& previous, const Node& current) {
    auto mask = diff(previous.m_header, current.m_header);
    if(previous.m_children != current.m_children) {
      mask |= ChangeMask::CHILDREN;
    }
    if(previous.m_payload.index() != current.m_payload.index()) {
      return mask | PAYLOAD_MASK;
    }
    return mask | std::visit([&] (const auto& payload) {
      using Type = std::decay_t<decltype(payload)>;
      return diff(std::get<Type>(previous.m_payload), payload);
    }, current.m_payload);
  }

  template<typename T>
  auto find_by_id(std::vector<T>& values, const NodeId& id) {
    return std::find_if(values.begin(), values.end(),
      [&] (const auto& value) { return value.m_id == id; });
  }

  template<typename T>
  bool contains_id(const std::vector<T>& values, const NodeId& id) {
    return std::any_of(values.begin(), values.end(),
      [&] (const auto& value) { return value.m_id == id; });
  }

  template<typename T>
  void append_differences(const std::vector<T>& previous,
      const std::vector<T>& current, std::vector<NodeId>& ids) {
    auto index = std::unordered_map<NodeId, const T*>();
    for(auto& value : previous) {
      index.emplace(value.m_id, &value);
    }
    for(auto& value : current) {
      auto entry = index.find(value.m_id);
      if(entry == index.end()) {
        ids.push_back(value.m_id);
      } else {
        if(!(*entry->second == value)) {
          ids.push_back(value.m_id);
        }
        index.erase(entry);
      }
    }
    for(auto& value : previous) {
      if(index.contains(value.m_id)) {
        ids.push_back(value.m_id);
      }
    }
  }

  template<typename K, typename V>
  void append_differences(const std::map<K, V>& map,
      const std::vector<std::pair<K, std::optional<V>>>& entries,
      std::vector<NodeId>& ids) {
    for(auto& entry : entries) {
      auto current = map.find(entry.first);
      auto is_changed = [&] {
        if(!entry.second) {
          return current != map.end();
        }
        return current == map.end() || !(current->second == *entry.second);
      }();
      if(is_changed) {
        ids.push_back(entry.first);
      }
    }
  }

  void sort_by_depth(
      std::vector<NodeId>& ids, const std::vector<NodeId>& parents) {
    if(ids.size() < 2) {
      return;
    }
    struct Member {
      std::size_t m_index;
      std::size_t m_depth;
    };
    auto members = std::unordered_map<NodeId, Member>();
    members.reserve(ids.size());
    for(auto i = std::size_t(0); i != ids.size(); ++i) {
      members.emplace(ids[i], Member(i, END));
    }
    auto depths = std::vector<std::size_t>(ids.size());
    auto pending = std::vector<Member*>();
    for(auto i = std::size_t(0); i != ids.size(); ++i) {
      auto member = &members.find(ids[i])->second;
      auto depth = std::size_t(0);
      while(member->m_depth == END) {
        pending.push_back(member);
        auto parent = members.find(parents[member->m_index]);
        if(parent == members.end()) {
          member = nullptr;
          break;
        }
        member = &parent->second;
      }
      if(member) {
        depth = member->m_depth + 1;
      }
      while(!pending.empty()) {
        pending.back()->m_depth = depth;
        pending.pop_back();
        ++depth;
      }
      depths[i] = members.find(ids[i])->second.m_depth;
    }
    auto order = std::vector<std::size_t>(ids.size());
    for(auto i = std::size_t(0); i != order.size(); ++i) {
      order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(),
      [&] (const auto& left, const auto& right) {
        return depths[left] < depths[right];
      });
    auto sorted = std::vector<NodeId>();
    sorted.reserve(ids.size());
    for(auto index : order) {
      sorted.push_back(ids[index]);
    }
    ids = std::move(sorted);
  }

  ChangeSet make_change_set(const Document& document, const auto& revision,
      const std::vector<NodeId>& moved, bool has_removals, bool has_moves) {
    auto changes = ChangeSet();
    auto& nodes = document.get_nodes();
    auto parents = std::vector<NodeId>();
    for(auto& entry : revision.m_nodes) {
      if(!entry.second) {
        if(!has_removals || nodes.contains(entry.first)) {
          changes.m_inserted.push_back(entry.first);
        }
      } else if(auto current = nodes.find(entry.first)) {
        auto mask = diff(*entry.second, *current);
        if(mask != ChangeMask::NONE) {
          changes.m_modified.push_back(NodeChange(entry.first, mask));
        }
      } else {
        changes.m_removed.push_back(entry.first);
        parents.push_back(entry.second->m_parent);
      }
    }
    sort_by_depth(changes.m_removed, parents);
    if(has_moves && changes.m_inserted.size() > 1) {
      parents.clear();
      for(auto& id : changes.m_inserted) {
        parents.push_back(nodes.get(id).m_parent);
      }
      sort_by_depth(changes.m_inserted, parents);
    }
    if(!moved.empty()) {
      auto inserted = std::unordered_set<NodeId>(
        changes.m_inserted.begin(), changes.m_inserted.end());
      auto listed = std::unordered_set<NodeId>();
      for(auto& id : moved) {
        if(nodes.contains(id) && !inserted.contains(id) &&
            listed.insert(id).second) {
          changes.m_moved.push_back(id);
        }
      }
    }
    auto& resources = document.get_resources();
    append_differences(
      resources.m_components, revision.m_components, changes.m_components);
    append_differences(
      resources.m_images, revision.m_images, changes.m_images);
    if(revision.m_swatches) {
      append_differences(
        *revision.m_swatches, resources.m_swatches, changes.m_swatches);
    }
    if(revision.m_character_styles) {
      append_differences(*revision.m_character_styles,
        resources.m_character_styles, changes.m_character_styles);
    }
    auto& prototype = document.get_prototype();
    if(revision.m_interactions) {
      append_differences(*revision.m_interactions, prototype.m_interactions,
        changes.m_interactions);
    }
    if(revision.m_flows) {
      changes.m_is_flow_changed =
        *revision.m_home_artboard != prototype.m_home_artboard ||
        *revision.m_flows != prototype.m_flows;
    }
    return changes;
  }

  template<typename K, typename V>
  void merge(std::vector<std::pair<K, V>>& entries,
      std::vector<std::pair<K, V>>& amendment) {
    auto ids = std::unordered_set<K>();
    ids.reserve(entries.size());
    for(auto& entry : entries) {
      ids.insert(entry.first);
    }
    for(auto& entry : amendment) {
      if(ids.insert(entry.first).second) {
        entries.push_back(std::move(entry));
      }
    }
  }

  template<typename T>
  void merge(std::optional<T>& value, std::optional<T>& amendment) {
    if(!value) {
      value = std::move(amendment);
    }
  }

  void merge_revisions(auto& revision, auto& amendment) {
    merge(revision.m_nodes, amendment.m_nodes);
    merge(revision.m_canvas, amendment.m_canvas);
    merge(revision.m_components, amendment.m_components);
    merge(revision.m_images, amendment.m_images);
    merge(revision.m_swatches, amendment.m_swatches);
    merge(revision.m_character_styles, amendment.m_character_styles);
    merge(revision.m_interactions, amendment.m_interactions);
    if(!revision.m_flows) {
      revision.m_home_artboard = std::move(amendment.m_home_artboard);
      revision.m_flows = std::move(amendment.m_flows);
    }
  }
}

Transaction::Transaction(Document& document)
    : m_document(&document),
      m_mark(document.m_transactions + 1),
      m_indexed_count(0),
      m_is_open(true) {
  if(document.m_has_transaction) {
    throw std::logic_error("A transaction is already open.");
  }
  document.m_has_transaction = true;
  document.m_transactions = m_mark;
}

Transaction::~Transaction() {
  if(m_is_open) {
    m_document->swap(m_revision);
    m_document->m_has_transaction = false;
  }
}

NodeId Transaction::insert(const NodeId& parent, std::size_t index,
    NodeHeader header, Payload payload) {
  check_open();
  if(index == END) {
    throw make_error("Index out of range in parent", parent);
  }
  return add(parent, index, std::move(header), std::move(payload));
}

NodeId Transaction::append(
    const NodeId& parent, NodeHeader header, Payload payload) {
  return add(parent, END, std::move(header), std::move(payload));
}

NodeId Transaction::insert_root(NodeHeader header, Payload payload) {
  check_open();
  if(header.m_id.is_nil()) {
    header.m_id = generate_node_id();
  }
  if(get_kind(payload) == NodeKind::ARTBOARD) {
    throw make_error("An artboard must be on the canvas", header.m_id);
  }
  validate_payload(payload);
  auto& nodes = m_document->m_nodes;
  auto id = header.m_id;
  if(nodes.contains(id)) {
    throw make_error("Duplicate node", id);
  }
  auto slot = nodes.add(std::make_shared<Node>(std::move(header),
    std::move(payload), NodeId(), std::vector<NodeId>()), true);
  touch_insertion(id, slot);
  return id;
}

void Transaction::remove(const NodeId& id) {
  check_open();
  auto node = m_document->m_nodes.find(id);
  if(!node) {
    throw make_error("Unknown node", id);
  }
  if(node->m_parent.is_nil() && !m_document->is_on_canvas(id) &&
      is_used_tree(id, ComponentId())) {
    throw make_error("Node is the root of a component tree", id);
  }
  remove_subtree(id);
}

void Transaction::move(
    const NodeId& id, const NodeId& parent, std::size_t index) {
  check_open();
  auto& nodes = m_document->m_nodes;
  auto node = nodes.find(id);
  if(!node) {
    throw make_error("Unknown node", id);
  }
  if(node->m_parent.is_nil() && !m_document->is_on_canvas(id)) {
    throw make_error("Node is the root of a component tree", id);
  }
  auto kind = node->get_kind();
  auto previous_parent = node->m_parent;
  if(!parent.is_nil()) {
    auto parent_node = nodes.find(parent);
    if(!parent_node) {
      throw make_error("Unknown parent", parent);
    }
    for(auto ancestor = parent; !ancestor.is_nil();
        ancestor = nodes.get(ancestor).m_parent) {
      if(ancestor == id) {
        throw make_error("Move would create a cycle", id);
      }
    }
    if(!can_contain(parent_node->m_payload, kind)) {
      throw make_error("Parent does not accept the node", parent);
    }
  }
  auto count = m_document->get_children(parent).size();
  if(previous_parent == parent) {
    --count;
  }
  if(index > count) {
    throw make_error("Index out of range in parent", parent);
  }
  auto& previous_children = edit_children(previous_parent);
  previous_children.erase(
    std::find(previous_children.begin(), previous_children.end(), id));
  auto& children = edit_children(parent);
  children.insert(children.begin() + index, id);
  if(previous_parent != parent) {
    auto slot = find_slot(id);
    touch(slot);
    nodes.edit_slot(slot).m_parent = parent;
  }
  m_moved.push_back(id);
}

void Transaction::replace_header(NodeHeader header) {
  check_open();
  auto& nodes = m_document->m_nodes;
  auto slot = nodes.find_slot(header.m_id);
  if(slot == NodeStore::NONE) {
    throw make_error("Unknown node", header.m_id);
  }
  touch(slot);
  nodes.edit_slot(slot).m_header = std::move(header);
}

void Transaction::replace_payload(const NodeId& id, Payload payload) {
  check_open();
  auto& nodes = m_document->m_nodes;
  auto slot = nodes.find_slot(id);
  if(slot == NodeStore::NONE) {
    throw make_error("Unknown node", id);
  }
  auto node = nodes.m_slots[slot].m_record.get();
  if(get_kind(payload) != node->get_kind()) {
    throw make_error("Payload kind differs from the node's", id);
  }
  validate_payload(payload);
  for(auto& child : node->m_children) {
    if(!can_contain(payload, nodes.get(child).get_kind())) {
      throw make_error("Payload does not accept the node's children", id);
    }
  }
  if(auto text = std::get_if<Box<Text>>(&payload)) {
    auto& previous = *node->get_text();
    if((*text)->m_layout && (*text)->m_layout == previous.m_layout) {
      if(is_layout_affected(previous, **text)) {
        text->edit().m_layout = std::nullopt;
      } else {
        recolor(previous, text->edit());
      }
    }
  }
  touch(slot);
  nodes.edit_slot(slot).m_payload = std::move(payload);
}

ComponentId Transaction::add_component(Component component) {
  check_open();
  if(component.m_id.is_nil()) {
    component.m_id = generate_node_id();
  }
  auto id = component.m_id;
  if(m_document->m_resources.m_components.contains(id)) {
    throw make_error("Duplicate component", id);
  }
  validate_component(component, id);
  touch_component(id);
  m_document->m_resources.m_components.emplace(id, std::move(component));
  return id;
}

void Transaction::replace_component(Component component) {
  check_open();
  auto& components = m_document->m_resources.m_components;
  auto entry = components.find(component.m_id);
  if(entry == components.end()) {
    throw make_error("Unknown component", component.m_id);
  }
  validate_component(component, component.m_id);
  touch_component(component.m_id);
  components.find(component.m_id)->second = std::move(component);
}

void Transaction::remove_component(const ComponentId& id) {
  check_open();
  auto& components = m_document->m_resources.m_components;
  auto entry = components.find(id);
  if(entry == components.end()) {
    throw make_error("Unknown component", id);
  }
  auto roots = std::vector<NodeId>();
  roots.push_back(entry->second.m_master);
  for(auto& state : entry->second.m_states) {
    roots.push_back(state.m_tree);
  }
  touch_component(id);
  components.erase(id);
  for(auto& root : roots) {
    if(m_document->m_nodes.contains(root)) {
      remove_subtree(root);
    }
  }
}

ImageId Transaction::add_image(ImageResource image) {
  check_open();
  if(image.m_id.is_nil()) {
    image.m_id = generate_node_id();
  }
  auto id = image.m_id;
  if(m_document->m_resources.m_images.contains(id)) {
    throw make_error("Duplicate image", id);
  }
  touch_image(id);
  m_document->m_resources.m_images.emplace(id, std::move(image));
  return id;
}

void Transaction::remove_image(const ImageId& id) {
  check_open();
  if(!m_document->m_resources.m_images.contains(id)) {
    throw make_error("Unknown image", id);
  }
  touch_image(id);
  m_document->m_resources.m_images.erase(id);
}

SwatchId Transaction::add_swatch(ColorSwatch swatch) {
  check_open();
  if(swatch.m_id.is_nil()) {
    swatch.m_id = generate_node_id();
  }
  auto id = swatch.m_id;
  if(contains_id(m_document->m_resources.m_swatches, id)) {
    throw make_error("Duplicate swatch", id);
  }
  touch_swatches();
  m_document->m_resources.m_swatches.push_back(std::move(swatch));
  return id;
}

void Transaction::replace_swatch(ColorSwatch swatch) {
  check_open();
  auto& swatches = m_document->m_resources.m_swatches;
  if(!contains_id(swatches, swatch.m_id)) {
    throw make_error("Unknown swatch", swatch.m_id);
  }
  touch_swatches();
  *find_by_id(swatches, swatch.m_id) = std::move(swatch);
}

void Transaction::remove_swatch(const SwatchId& id) {
  check_open();
  auto& swatches = m_document->m_resources.m_swatches;
  if(!contains_id(swatches, id)) {
    throw make_error("Unknown swatch", id);
  }
  touch_swatches();
  swatches.erase(find_by_id(swatches, id));
}

CharacterStyleId Transaction::add_character_style(CharacterStyle style) {
  check_open();
  if(style.m_id.is_nil()) {
    style.m_id = generate_node_id();
  }
  auto id = style.m_id;
  if(contains_id(m_document->m_resources.m_character_styles, id)) {
    throw make_error("Duplicate character style", id);
  }
  touch_character_styles();
  m_document->m_resources.m_character_styles.push_back(std::move(style));
  return id;
}

void Transaction::replace_character_style(CharacterStyle style) {
  check_open();
  auto& styles = m_document->m_resources.m_character_styles;
  if(!contains_id(styles, style.m_id)) {
    throw make_error("Unknown character style", style.m_id);
  }
  touch_character_styles();
  *find_by_id(styles, style.m_id) = std::move(style);
}

void Transaction::remove_character_style(const CharacterStyleId& id) {
  check_open();
  auto& styles = m_document->m_resources.m_character_styles;
  if(!contains_id(styles, id)) {
    throw make_error("Unknown character style", id);
  }
  touch_character_styles();
  styles.erase(find_by_id(styles, id));
}

InteractionId Transaction::add_interaction(Interaction interaction) {
  check_open();
  if(interaction.m_id.is_nil()) {
    interaction.m_id = generate_node_id();
  }
  auto id = interaction.m_id;
  if(contains_id(m_document->m_prototype.m_interactions, id)) {
    throw make_error("Duplicate interaction", id);
  }
  if(!m_document->m_nodes.contains(interaction.m_owner)) {
    throw make_error("Unknown interaction owner", interaction.m_owner);
  }
  touch_interactions();
  m_document->m_prototype.m_interactions.push_back(std::move(interaction));
  return id;
}

void Transaction::replace_interaction(Interaction interaction) {
  check_open();
  auto& interactions = m_document->m_prototype.m_interactions;
  if(!contains_id(interactions, interaction.m_id)) {
    throw make_error("Unknown interaction", interaction.m_id);
  }
  if(!m_document->m_nodes.contains(interaction.m_owner)) {
    throw make_error("Unknown interaction owner", interaction.m_owner);
  }
  touch_interactions();
  *find_by_id(interactions, interaction.m_id) = std::move(interaction);
}

void Transaction::remove_interaction(const InteractionId& id) {
  check_open();
  auto& interactions = m_document->m_prototype.m_interactions;
  if(!contains_id(interactions, id)) {
    throw make_error("Unknown interaction", id);
  }
  touch_interactions();
  interactions.erase(find_by_id(interactions, id));
}

void Transaction::set_home_artboard(const NodeId& artboard) {
  check_open();
  if(!artboard.is_nil()) {
    auto node = m_document->m_nodes.find(artboard);
    if(!node || node->get_kind() != NodeKind::ARTBOARD) {
      throw make_error("Unknown artboard", artboard);
    }
  }
  touch_flows();
  m_document->m_prototype.m_home_artboard = artboard;
}

void Transaction::set_flows(std::vector<Flow> flows) {
  check_open();
  for(auto& flow : flows) {
    auto node = m_document->m_nodes.find(flow.m_artboard);
    if(!node || node->get_kind() != NodeKind::ARTBOARD) {
      throw make_error("Unknown artboard", flow.m_artboard);
    }
  }
  touch_flows();
  m_document->m_prototype.m_flows = std::move(flows);
}

bool Transaction::commit() {
  check_open();
  auto changes = make_change_set(*m_document, m_revision, m_moved,
    !m_removed.empty(), !m_moved.empty());
  finish();
  if(changes.is_empty()) {
    return false;
  }
  auto& document = *m_document;
  m_revision.m_changes = std::move(changes);
  document.m_undo.push_back(std::move(m_revision));
  document.m_redo.clear();
  document.publish(document.m_undo.back().m_changes);
  return true;
}

bool Transaction::amend() {
  check_open();
  auto& document = *m_document;
  if(document.m_undo.empty()) {
    return commit();
  }
  auto changes = make_change_set(document, m_revision, m_moved,
    !m_removed.empty(), !m_moved.empty());
  finish();
  if(changes.is_empty()) {
    return true;
  }
  auto& revision = document.m_undo.back();
  merge_revisions(revision, m_revision);
  auto moved = std::move(revision.m_changes.m_moved);
  moved.insert(moved.end(), m_moved.begin(), m_moved.end());
  revision.m_changes = make_change_set(document, revision, moved, true, true);
  auto is_recorded = !revision.m_changes.is_empty();
  if(!is_recorded) {
    document.m_undo.pop_back();
  }
  document.m_redo.clear();
  document.publish(changes);
  return is_recorded;
}

void Transaction::check_open() const {
  if(!m_is_open) {
    throw std::logic_error("The transaction is not open.");
  }
}

NodeId Transaction::add(const NodeId& parent, std::size_t index,
    NodeHeader header, Payload payload) {
  check_open();
  if(header.m_id.is_nil()) {
    header.m_id = generate_node_id();
  }
  validate_payload(payload);
  auto& nodes = m_document->m_nodes;
  auto parent_slot = NodeStore::NONE;
  auto count = std::size_t(0);
  if(parent.is_nil()) {
    count = m_document->m_canvas.size();
  } else {
    parent_slot = nodes.find_slot(parent);
    if(parent_slot == NodeStore::NONE) {
      throw make_error("Unknown parent", parent);
    }
    auto& parent_node = *nodes.m_slots[parent_slot].m_record;
    if(!can_contain(parent_node.m_payload, get_kind(payload))) {
      throw make_error("Parent does not accept the node", parent);
    }
    count = parent_node.m_children.size();
  }
  if(index == END) {
    index = count;
  } else if(index > count) {
    throw make_error("Index out of range in parent", parent);
  }
  auto id = header.m_id;
  if(nodes.contains(id)) {
    throw make_error("Duplicate node", id);
  }
  auto slot = nodes.add(std::make_shared<Node>(
    std::move(header), std::move(payload), parent, std::vector<NodeId>()),
    true);
  touch_insertion(id, slot);
  auto& children = edit_children(parent, parent_slot);
  children.insert(children.begin() + index, id);
  return id;
}

std::size_t Transaction::find_slot(const NodeId& id) const {
  return m_document->m_nodes.find_slot(id);
}

void Transaction::touch(std::size_t slot) {
  auto& entry = m_document->m_nodes.m_slots[slot];
  if(entry.m_mark != m_mark) {
    entry.m_mark = m_mark;
    m_revision.m_nodes.emplace_back(entry.m_record->get_id(), entry.m_record);
  }
}

void Transaction::touch_insertion(const NodeId& id, std::size_t slot) {
  for(; m_indexed_count != m_removed.size(); ++m_indexed_count) {
    m_removed_index.insert(m_removed[m_indexed_count]);
  }
  if(m_removed.empty() || !m_removed_index.contains(id)) {
    m_revision.m_nodes.emplace_back(id, nullptr);
  }
  m_document->m_nodes.m_slots[slot].m_mark = m_mark;
}

void Transaction::touch_canvas() {
  if(!m_revision.m_canvas) {
    m_revision.m_canvas = m_document->m_canvas;
  }
}

void Transaction::touch_component(const ComponentId& id) {
  if(!m_touched_components.insert(id).second) {
    return;
  }
  auto& components = m_document->m_resources.m_components;
  auto entry = components.find(id);
  if(entry == components.end()) {
    m_revision.m_components.emplace_back(id, std::nullopt);
  } else {
    m_revision.m_components.emplace_back(id, entry->second);
  }
}

void Transaction::touch_image(const ImageId& id) {
  if(!m_touched_images.insert(id).second) {
    return;
  }
  auto& images = m_document->m_resources.m_images;
  auto entry = images.find(id);
  if(entry == images.end()) {
    m_revision.m_images.emplace_back(id, std::nullopt);
  } else {
    m_revision.m_images.emplace_back(id, entry->second);
  }
}

void Transaction::touch_swatches() {
  if(!m_revision.m_swatches) {
    m_revision.m_swatches = m_document->m_resources.m_swatches;
  }
}

void Transaction::touch_character_styles() {
  if(!m_revision.m_character_styles) {
    m_revision.m_character_styles =
      m_document->m_resources.m_character_styles;
  }
}

void Transaction::touch_interactions() {
  if(!m_revision.m_interactions) {
    m_revision.m_interactions = m_document->m_prototype.m_interactions;
  }
}

void Transaction::touch_flows() {
  if(!m_revision.m_flows) {
    m_revision.m_home_artboard = m_document->m_prototype.m_home_artboard;
    m_revision.m_flows = m_document->m_prototype.m_flows;
  }
}

std::vector<NodeId>& Transaction::edit_children(const NodeId& parent) {
  if(parent.is_nil()) {
    return edit_children(parent, NodeStore::NONE);
  }
  return edit_children(parent, find_slot(parent));
}

std::vector<NodeId>& Transaction::edit_children(
    const NodeId& parent, std::size_t slot) {
  if(parent.is_nil()) {
    touch_canvas();
    return m_document->m_canvas;
  }
  touch(slot);
  return m_document->m_nodes.edit_slot(slot).m_children;
}

bool Transaction::is_used_tree(
    const NodeId& root, const ComponentId& owner) const {
  for(auto& entry : m_document->m_resources.m_components) {
    if(entry.first == owner) {
      continue;
    }
    if(entry.second.m_master == root) {
      return true;
    }
    for(auto& state : entry.second.m_states) {
      if(state.m_tree == root) {
        return true;
      }
    }
  }
  return false;
}

void Transaction::validate_component(
    const Component& component, const ComponentId& owner) const {
  auto roots = std::vector<NodeId>();
  roots.push_back(component.m_master);
  auto states = std::unordered_set<StateId>();
  for(auto& state : component.m_states) {
    if(!states.insert(state.m_id).second) {
      throw make_error("Duplicate state", state.m_id);
    }
    roots.push_back(state.m_tree);
  }
  auto distinct = std::unordered_set<NodeId>();
  for(auto& root : roots) {
    auto node = m_document->m_nodes.find(root);
    if(!node) {
      throw make_error("Unknown component tree", root);
    }
    if(node->get_kind() != NodeKind::GROUP) {
      throw make_error("Component tree is not a group", root);
    }
    if(!node->m_parent.is_nil() || m_document->is_on_canvas(root)) {
      throw make_error("Component tree is not a root", root);
    }
    if(!distinct.insert(root).second || is_used_tree(root, owner)) {
      throw make_error("Component tree is already in use", root);
    }
  }
}

void Transaction::remove_subtree(const NodeId& id) {
  auto& nodes = m_document->m_nodes;
  auto parent = nodes.get(id).m_parent;
  if(!parent.is_nil() || m_document->is_on_canvas(id)) {
    auto& children = edit_children(parent);
    children.erase(std::find(children.begin(), children.end(), id));
  }
  auto subtree = std::vector<NodeId>();
  auto pending = std::vector<NodeId>();
  pending.push_back(id);
  while(!pending.empty()) {
    auto current = pending.back();
    pending.pop_back();
    subtree.push_back(current);
    auto& children = nodes.get(current).m_children;
    pending.insert(pending.end(), children.rbegin(), children.rend());
  }
  auto trees = std::vector<NodeId>();
  for(auto& node : subtree) {
    if(auto group = nodes.get(node).get_group()) {
      if(group->m_component_instance) {
        for(auto& state : group->m_component_instance->m_states) {
          trees.push_back(state.m_tree);
        }
      }
    }
    auto slot = find_slot(node);
    touch(slot);
    nodes.remove_slot(slot);
    m_removed.push_back(node);
  }
  for(auto& tree : trees) {
    auto root = nodes.find(tree);
    if(root && root->m_parent.is_nil() && !m_document->is_on_canvas(tree) &&
        !is_used_tree(tree, ComponentId())) {
      remove_subtree(tree);
    }
  }
  auto& prototype = m_document->m_prototype;
  if(prototype.m_interactions.empty() && prototype.m_flows.empty() &&
      prototype.m_home_artboard.is_nil()) {
    return;
  }
  auto removed = std::unordered_set<NodeId>(subtree.begin(), subtree.end());
  auto is_owned = [&] (const auto& interaction) {
    return removed.contains(interaction.m_owner);
  };
  if(std::any_of(prototype.m_interactions.begin(),
      prototype.m_interactions.end(), is_owned)) {
    touch_interactions();
    std::erase_if(prototype.m_interactions, is_owned);
  }
  auto is_started = [&] (const auto& flow) {
    return removed.contains(flow.m_artboard);
  };
  if(removed.contains(prototype.m_home_artboard) || std::any_of(
      prototype.m_flows.begin(), prototype.m_flows.end(), is_started)) {
    touch_flows();
    if(removed.contains(prototype.m_home_artboard)) {
      prototype.m_home_artboard = NodeId();
    }
    std::erase_if(prototype.m_flows, is_started);
  }
}

void Transaction::finish() {
  m_is_open = false;
  m_document->m_has_transaction = false;
}

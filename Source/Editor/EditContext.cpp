#include <algorithm>
#include <utility>
#include <variant>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Editor/EditContext.hpp"

using namespace Rellu;

namespace {
  bool is_enterable(const Node& node) {
    if(node.get_group()) {
      return true;
    } else if(auto shape = node.get_shape()) {
      return std::holds_alternative<CompoundShape>(shape->m_geometry);
    }
    return false;
  }

  std::vector<NodeId> get_enterable_ancestors(
      const Document& document, const NodeId& id) {
    auto ancestors = std::vector<NodeId>();
    auto node = document.find(id);
    while(node && !node->m_parent.is_nil()) {
      node = document.find(node->m_parent);
      if(node && is_enterable(*node)) {
        ancestors.push_back(node->get_id());
      }
    }
    std::reverse(ancestors.begin(), ancestors.end());
    return ancestors;
  }
}

EditContext::EditContext(Document& document)
    : m_document(&document),
      m_is_prototyping(false) {
  m_subscription = document.subscribe([this] (const ChangeSet& changes) {
    on_change(changes);
  });
}

const NodeId& EditContext::get_container() const {
  return m_container;
}

const NodeId& EditContext::get_artboard() const {
  return m_artboard;
}

const NodeId& EditContext::get_hover() const {
  return m_hover;
}

const InteractionId& EditContext::get_interaction() const {
  return m_interaction;
}

const std::vector<InteractionId>& EditContext::get_interactions() const {
  return m_interactions;
}

const NodeId& EditContext::get_interaction_owner() const {
  return m_interaction_owner;
}

bool EditContext::is_prototyping() const {
  return m_is_prototyping;
}

std::optional<InteractionSelection>
    EditContext::get_interaction_selection() const {
  if(!m_is_prototyping) {
    return std::nullopt;
  }
  return InteractionSelection(m_interactions, m_pending);
}

void EditContext::set_prototyping(bool is_prototyping) {
  m_is_prototyping = is_prototyping;
}

void EditContext::restore_interactions(const NodeId& owner,
    const std::optional<InteractionSelection>& selection) {
  if(!m_is_prototyping) {
    return;
  }
  if(!selection) {
    set_interactions(owner, {});
    return;
  }
  auto& stored = m_document->get_prototype().m_interactions;
  auto interactions = std::vector<InteractionId>();
  auto pending = std::vector<Interaction>();
  for(auto& id : selection->m_interactions) {
    auto existing = std::ranges::find(stored, id, &Interaction::m_id);
    if(existing != stored.end()) {
      if(m_document->find(existing->m_owner)) {
        interactions.push_back(id);
      }
      continue;
    }
    auto entry =
      std::ranges::find(selection->m_pending, id, &Interaction::m_id);
    if(entry != selection->m_pending.end() &&
        m_document->find(entry->m_owner)) {
      interactions.push_back(id);
      pending.push_back(*entry);
    }
  }
  set_interactions(owner, std::move(interactions));
  set_pending(std::move(pending));
}

const std::optional<Interaction>& EditContext::get_last_edited() const {
  return m_last_edited;
}

void EditContext::set_last_edited(std::optional<Interaction> interaction) {
  m_last_edited = std::move(interaction);
}

const std::vector<Interaction>& EditContext::get_pending() const {
  return m_pending;
}

void EditContext::set_pending(std::vector<Interaction> interactions) {
  std::erase_if(interactions, [&] (const auto& interaction) {
    return !std::ranges::contains(m_interactions, interaction.m_id);
  });
  if(interactions == m_pending) {
    return;
  }
  m_pending = std::move(interactions);
  m_signal();
}

NodeId EditContext::get_scope() const {
  if(!m_container.is_nil()) {
    return m_container;
  }
  return m_artboard;
}

bool EditContext::enter(const NodeId& container) {
  auto node = m_document->find(container);
  if(!node || !is_enterable(*node)) {
    return false;
  }
  auto artboard = find_artboard(*m_document, container);
  if(container == m_container &&
      (artboard.is_nil() || artboard == m_artboard)) {
    return true;
  }
  m_container = container;
  m_ancestors = get_enterable_ancestors(*m_document, container);
  if(!artboard.is_nil()) {
    m_artboard = artboard;
  }
  m_signal();
  return true;
}

void EditContext::exit() {
  if(m_container.is_nil()) {
    return;
  }
  if(m_ancestors.empty()) {
    m_container = NodeId();
  } else {
    m_container = m_ancestors.back();
    m_ancestors.pop_back();
  }
  m_signal();
}

void EditContext::set_artboard(const NodeId& artboard) {
  if(artboard == m_artboard) {
    return;
  }
  if(!artboard.is_nil()) {
    auto node = m_document->find(artboard);
    if(!node || !node->get_artboard()) {
      return;
    }
  }
  m_artboard = artboard;
  m_signal();
}

void EditContext::set_hover(const NodeId& hover) {
  if(hover == m_hover || (!hover.is_nil() && !m_document->find(hover))) {
    return;
  }
  m_hover = hover;
  m_signal();
}

void EditContext::set_interaction(const InteractionId& interaction) {
  if(interaction.is_nil()) {
    set_interactions(NodeId(), {});
    return;
  }
  auto owner = NodeId();
  for(auto& candidate : m_document->get_prototype().m_interactions) {
    if(candidate.m_id == interaction) {
      owner = candidate.m_owner;
    }
  }
  set_interactions(owner, {interaction});
}

void EditContext::set_interactions(
    const NodeId& owner, std::vector<InteractionId> interactions) {
  if(owner == m_interaction_owner && interactions == m_interactions) {
    return;
  }
  m_interaction_owner = owner;
  m_interactions = std::move(interactions);
  std::erase_if(m_pending, [&] (const auto& interaction) {
    return !std::ranges::contains(m_interactions, interaction.m_id);
  });
  if(m_interactions.empty()) {
    m_interaction = InteractionId();
  } else {
    m_interaction = m_interactions.front();
  }
  m_signal();
}

NodeId EditContext::resolve(const NodeId& hit) const {
  auto node = m_document->find(hit);
  if(!node) {
    return NodeId();
  }
  if(is_ancestor(*m_document, m_container, hit)) {
    while(node->m_parent != m_container) {
      node = m_document->find(node->m_parent);
    }
  } else {
    while(!node->m_parent.is_nil()) {
      auto parent = m_document->find(node->m_parent);
      if(!parent || parent->get_artboard()) {
        break;
      }
      node = parent;
    }
  }
  return node->get_id();
}

Subscription EditContext::subscribe(std::function<void ()> observer) {
  return m_signal.subscribe(std::move(observer));
}

void EditContext::on_change(const ChangeSet& changes) {
  if(changes.m_removed.empty()) {
    return;
  }
  auto is_changed = false;
  if(!m_container.is_nil() && !m_document->find(m_container)) {
    m_container = NodeId();
    while(!m_ancestors.empty()) {
      auto ancestor = m_ancestors.back();
      m_ancestors.pop_back();
      if(m_document->find(ancestor)) {
        m_container = ancestor;
        m_ancestors = get_enterable_ancestors(*m_document, ancestor);
        break;
      }
    }
    is_changed = true;
  }
  if(!m_artboard.is_nil() && !m_document->find(m_artboard)) {
    m_artboard = NodeId();
    is_changed = true;
  }
  if(!m_hover.is_nil() && !m_document->find(m_hover)) {
    m_hover = NodeId();
    is_changed = true;
  }
  if(is_changed) {
    m_signal();
  }
}

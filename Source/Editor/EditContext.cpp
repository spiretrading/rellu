#include <algorithm>
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
    : m_document(&document) {
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
  if(interaction == m_interaction) {
    return;
  }
  m_interaction = interaction;
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

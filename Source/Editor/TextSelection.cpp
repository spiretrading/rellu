#include <algorithm>
#include <utility>
#include "Rellu/Editor/TextSelection.hpp"

using namespace Rellu;

TextSelection::TextSelection(Document& document)
    : m_document(&document),
      m_anchor(0),
      m_caret(0) {
  m_subscription = document.subscribe([this] (const ChangeSet& changes) {
    on_change(changes);
  });
}

bool TextSelection::is_editing() const {
  return !m_node.is_nil();
}

const NodeId& TextSelection::get_node() const {
  return m_node;
}

std::size_t TextSelection::get_anchor() const {
  return m_anchor;
}

std::size_t TextSelection::get_caret() const {
  return m_caret;
}

TextRange TextSelection::get_range() const {
  return TextRange(std::min(m_anchor, m_caret), std::max(m_anchor, m_caret));
}

void TextSelection::set(
    const NodeId& node, std::size_t anchor, std::size_t caret) {
  auto size = std::size_t(0);
  if(auto found = m_document->find(node); found && found->get_text()) {
    size = found->get_text()->m_content->size();
  }
  anchor = std::min(anchor, size);
  caret = std::min(caret, size);
  if(node == m_node && anchor == m_anchor && caret == m_caret) {
    return;
  }
  m_node = node;
  m_anchor = anchor;
  m_caret = caret;
  m_signal();
}

void TextSelection::clear() {
  set(NodeId(), 0, 0);
}

Subscription TextSelection::subscribe(std::function<void ()> observer) {
  return m_signal.subscribe(std::move(observer));
}

void TextSelection::on_change(const ChangeSet& changes) {
  if(m_node.is_nil()) {
    return;
  }
  auto node = m_document->find(m_node);
  if(!node || !node->get_text()) {
    clear();
    return;
  }
  auto size = node->get_text()->m_content->size();
  if(m_anchor > size || m_caret > size) {
    set(m_node, m_anchor, m_caret);
  }
}

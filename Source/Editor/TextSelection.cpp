#include <algorithm>
#include <utility>
#include <variant>
#include "Rellu/Editor/TextSelection.hpp"

using namespace Rellu;

TextSelection::TextSelection(Document& document)
    : m_document(&document),
      m_anchor(0),
      m_caret(0),
      m_last_style(get_default_text_style()),
      m_is_last_fill_enabled(true) {
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

const std::optional<TextStyle>& TextSelection::get_pending_style() const {
  return m_pending_style;
}

void TextSelection::set_pending_style(std::optional<TextStyle> style) {
  if(m_node.is_nil() || style == m_pending_style) {
    return;
  }
  m_pending_style = std::move(style);
  m_signal();
}

const std::optional<std::string>& TextSelection::get_pending_hyperlink() const {
  return m_pending_hyperlink;
}

void TextSelection::set_pending_hyperlink(
    std::optional<std::string> hyperlink) {
  if(m_node.is_nil() || hyperlink == m_pending_hyperlink) {
    return;
  }
  m_pending_hyperlink = std::move(hyperlink);
  m_signal();
}

const TextStyle& TextSelection::get_last_style() const {
  return m_last_style;
}

const ParagraphStyle& TextSelection::get_last_paragraph_style() const {
  return m_last_paragraph_style;
}

void TextSelection::set_last_style(TextStyle style, ParagraphStyle paragraph) {
  m_last_style = std::move(style);
  m_last_paragraph_style = std::move(paragraph);
}

bool TextSelection::is_last_fill_enabled() const {
  return m_is_last_fill_enabled;
}

const std::optional<Stroke>& TextSelection::get_last_border() const {
  return m_last_border;
}

void TextSelection::set_last_appearance(
    bool is_fill_enabled, std::optional<Stroke> border) {
  m_is_last_fill_enabled = is_fill_enabled;
  m_last_border = std::move(border);
}

void TextSelection::remember_appearance(const Text& text) {
  m_is_last_fill_enabled = !std::holds_alternative<NoFill>(text.m_style.m_fill);
  if(auto& stroke = text.m_style.m_stroke) {
    auto border = Stroke();
    border.m_color = stroke->m_color;
    border.m_width = stroke->m_width;
    border.m_is_enabled = stroke->m_is_enabled;
    m_last_border = std::move(border);
  }
}

void TextSelection::remember(const Text& text) {
  auto runs = get_runs(text);
  set_last_style(runs.empty() ? text.m_base_style : runs.front().m_style,
    text.m_paragraph_style);
  remember_appearance(text);
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
  m_pending_style = std::nullopt;
  m_pending_hyperlink = std::nullopt;
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

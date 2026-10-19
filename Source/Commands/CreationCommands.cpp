#include <algorithm>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include "Rellu/Commands/CreationCommands.hpp"

using namespace Rellu;

namespace {
  constexpr auto BORDER_COLOR = make_color(112, 112, 112);
  constexpr auto MAXIMUM_DIGITS = std::size_t(15);
}

Style Rellu::make_default_style() {
  auto style = Style();
  style.m_fill = SolidFill(make_color(255, 255, 255));
  style.m_stroke = Stroke(BORDER_COLOR, 1, StrokeAlignment::INSIDE);
  return style;
}

Style Rellu::make_default_line_style() {
  auto style = Style();
  style.m_fill = NoFill();
  style.m_stroke = Stroke(BORDER_COLOR, 1);
  return style;
}

std::string Rellu::make_node_name(
    const Document& document, std::string_view prefix) {
  auto largest = std::uint64_t(0);
  document.get_nodes().for_each([&] (const Node& node) {
    auto name = std::string_view(node.m_header.m_name);
    if(!name.starts_with(prefix)) {
      return;
    }
    auto digits = name.substr(prefix.size());
    if(digits.empty() || digits.size() > MAXIMUM_DIGITS ||
        !std::all_of(digits.begin(), digits.end(), [] (const auto& digit) {
          return digit >= '0' && digit <= '9';
        })) {
      return;
    }
    auto number = std::uint64_t(0);
    std::from_chars(digits.data(), digits.data() + digits.size(), number);
    largest = std::max(largest, number);
  });
  return std::string(prefix) + std::to_string(largest + 1);
}

InsertNodeCommand::InsertNodeCommand(std::string label, const NodeId& parent,
    NodeHeader header, Payload payload)
    : m_label(std::move(label)),
      m_parent(parent),
      m_header(std::move(header)),
      m_payload(std::move(payload)) {
  if(m_header.m_id.is_nil()) {
    m_header.m_id = generate_node_id();
  }
}

const NodeId& InsertNodeCommand::get_node() const {
  return m_header.m_id;
}

std::string InsertNodeCommand::get_label() const {
  return m_label;
}

void InsertNodeCommand::apply(
    const Document& document, Transaction& transaction) {
  if(auto node = document.find(m_header.m_id)) {
    if(node->m_parent != m_parent) {
      throw std::invalid_argument("The node has another parent.");
    }
    if(node->m_header != m_header) {
      transaction.replace_header(m_header);
    }
    if(node->m_payload != m_payload) {
      transaction.replace_payload(m_header.m_id, m_payload);
    }
    return;
  }
  if(!m_parent.is_nil() && !document.find(m_parent)) {
    throw std::invalid_argument("Unknown parent.");
  }
  transaction.append(m_parent, m_header, m_payload);
}

bool InsertNodeCommand::merge(const Command& command) {
  auto insertion = dynamic_cast<const InsertNodeCommand*>(&command);
  if(!insertion || insertion->m_header.m_id != m_header.m_id ||
      insertion->m_parent != m_parent) {
    return false;
  }
  m_header = insertion->m_header;
  m_payload = insertion->m_payload;
  return true;
}

std::optional<std::vector<NodeId>> InsertNodeCommand::get_selection() const {
  return std::vector{m_header.m_id};
}

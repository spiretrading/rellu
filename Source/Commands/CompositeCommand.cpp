#include "Rellu/Commands/CompositeCommand.hpp"

using namespace Rellu;

CompositeCommand::CompositeCommand(std::string label,
  std::vector<std::unique_ptr<Command>> commands) noexcept
  : m_label(std::move(label)),
    m_commands(std::move(commands)) {}

const std::vector<std::unique_ptr<Command>>&
    CompositeCommand::get_commands() const {
  return m_commands;
}

std::string CompositeCommand::get_label() const {
  return m_label;
}

void CompositeCommand::apply(
    const Document& document, Transaction& transaction) {
  for(auto& command : m_commands) {
    command->apply(document, transaction);
  }
}

std::optional<std::vector<NodeId>> CompositeCommand::get_selection() const {
  for(auto i = m_commands.rbegin(); i != m_commands.rend(); ++i) {
    if(auto selection = (*i)->get_selection()) {
      return selection;
    }
  }
  return std::nullopt;
}

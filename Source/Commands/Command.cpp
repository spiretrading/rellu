#include "Rellu/Commands/Command.hpp"

using namespace Rellu;

bool Command::merge(const Command& command) {
  return false;
}

std::optional<std::vector<NodeId>> Command::get_selection() const {
  return std::nullopt;
}

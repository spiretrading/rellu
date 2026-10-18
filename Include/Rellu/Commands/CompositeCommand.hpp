#ifndef RELLU_COMMANDS_COMPOSITE_COMMAND_HPP
#define RELLU_COMMANDS_COMPOSITE_COMMAND_HPP
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"

namespace Rellu {

  /** Applies several commands in turn as a single step. */
  class CompositeCommand : public Command {
    public:

      /**
       * Constructs a CompositeCommand.
       * @param label The label naming the combined operation.
       * @param commands The commands to apply, in order.
       */
      CompositeCommand(std::string label,
        std::vector<std::unique_ptr<Command>> commands) noexcept;

      /** Returns the commands applied. */
      const std::vector<std::unique_ptr<Command>>& get_commands() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Returns the selection of the last command that has one. */
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::string m_label;
      std::vector<std::unique_ptr<Command>> m_commands;
  };
}

#endif

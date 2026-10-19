#ifndef RELLU_COMMANDS_COMMAND_REGISTRY_HPP
#define RELLU_COMMANDS_COMMAND_REGISTRY_HPP
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"

namespace Rellu {

  /**
   * Makes a command acting on target nodes, such as the selection, or
   * returns <code>nullptr</code> when the command does not apply to them.
   */
  using CommandFactory = std::function<std::unique_ptr<Command> (
    const Document& document, const std::vector<NodeId>& targets)>;

  /** Names the commands available to menus, shortcuts and scripts. */
  class CommandRegistry {
    public:

      /** Constructs an empty CommandRegistry. */
      CommandRegistry() = default;

      /**
       * Adds a command, throwing std::invalid_argument if the name is taken.
       * @param name The command's name, such as object.group.
       * @param factory The function making the command.
       */
      void add(std::string name, CommandFactory factory);

      /** Returns <code>true</code> iff a command has the given name. */
      bool contains(std::string_view name) const;

      /** Returns the names of every command, in lexicographic order. */
      std::vector<std::string> get_names() const;

      /**
       * Makes a command, throwing std::out_of_range if none has the name.
       * @param name The command's name.
       * @param document The document the command acts on.
       * @param targets The nodes the command acts on.
       * @return The command, or <code>nullptr</code> if it does not apply to
       *         the <i>targets</i>.
       */
      std::unique_ptr<Command> make(std::string_view name,
        const Document& document, const std::vector<NodeId>& targets) const;

    private:
      std::map<std::string, CommandFactory, std::less<>> m_factories;
  };

  /**
   * Adds the standard commands acting on targets: edit.delete,
   * edit.duplicate, object.group, object.ungroup (Ungroup Grid when the
   * targets are repeat grids), object.toggle_lock,
   * object.toggle_visibility, arrange.bring_to_front, arrange.bring_forward,
   * arrange.send_backward, arrange.send_to_back, and object.nudge_left,
   * object.nudge_right, object.nudge_up and object.nudge_down by one pixel
   * with their _large variants by ten.
   * @param registry The registry to add to.
   * @param measurer The function measuring text wherever the commands
   *        measure bounds.
   */
  void add_standard_commands(CommandRegistry& registry,
    TextMeasurer measurer = estimate_text_bounds);
}

#endif

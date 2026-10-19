#ifndef RELLU_COMMANDS_STACK_COMMANDS_HPP
#define RELLU_COMMANDS_STACK_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Model/Layout.hpp"

namespace Rellu {

  /** Lists the sides of a group's padding. */
  enum class PaddingSide : unsigned char {

    /** Every side at once. */
    ALL,

    /** The top side. */
    TOP,

    /** The right side. */
    RIGHT,

    /** The bottom side. */
    BOTTOM,

    /** The left side. */
    LEFT
  };

  /**
   * Turns the stack layout of groups on, off or to another direction,
   * labelled Stack. Turning it on orders the visible children along the
   * direction by position, the first on top, and keeps the gaps between
   * them as their spacing, so that nothing moves; a padding background
   * becomes the stack's.
   */
  class SetStackCommand : public Command {
    public:

      /**
       * Constructs a SetStackCommand.
       * @param nodes The groups, other nodes being left unchanged.
       * @param orientation The direction of the stack, or
       *        <code>std::nullopt</code> to turn the stack off.
       * @param measurer The function measuring text.
       */
      SetStackCommand(std::vector<NodeId> nodes,
        std::optional<Orientation> orientation,
        TextMeasurer measurer) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      std::optional<Orientation> m_orientation;
      TextMeasurer m_measurer;
  };

  /**
   * Sets the spacing between stack items, labelled Spacing: every item of
   * the stacks, or the one item followed by the spacing.
   */
  class SetStackSpacingCommand : public Command {
    public:

      /**
       * Constructs a SetStackSpacingCommand setting the space after every
       * item but the last.
       * @param nodes The stacks, other nodes being left unchanged.
       * @param spacing The space between items.
       * @param measurer The function measuring text.
       */
      SetStackSpacingCommand(std::vector<NodeId> nodes, double spacing,
        TextMeasurer measurer) noexcept;

      /**
       * Constructs a SetStackSpacingCommand setting the space after one
       * item.
       * @param stack The stack.
       * @param item The item followed by the space.
       * @param spacing The space after the item.
       * @param measurer The function measuring text.
       */
      SetStackSpacingCommand(const NodeId& stack, const NodeId& item,
        double spacing, TextMeasurer measurer);

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the spacing of a SetStackSpacingCommand of the same stacks
       * and item.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      NodeId m_item;
      double m_spacing;
      TextMeasurer m_measurer;
  };

  /**
   * Turns the padding of groups on or off, labelled Padding. Turning it on
   * makes the bottom-most child the background and keeps its distances to
   * the rest of the content, but no less than zero, as the padding.
   */
  class SetPaddingCommand : public Command {
    public:

      /**
       * Constructs a SetPaddingCommand.
       * @param nodes The groups, other nodes being left unchanged.
       * @param is_enabled Whether the padding is on.
       * @param measurer The function measuring text.
       */
      SetPaddingCommand(std::vector<NodeId> nodes, bool is_enabled,
        TextMeasurer measurer) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      bool m_is_enabled;
      TextMeasurer m_measurer;
  };

  /**
   * Sets the padding of groups whose padding is on, labelled Padding, and
   * fits their backgrounds to it.
   */
  class SetPaddingValueCommand : public Command {
    public:

      /**
       * Constructs a SetPaddingValueCommand.
       * @param nodes The groups, other nodes being left unchanged.
       * @param side The side set.
       * @param value The padding, no less than zero.
       * @param measurer The function measuring text.
       */
      SetPaddingValueCommand(std::vector<NodeId> nodes, PaddingSide side,
        double value, TextMeasurer measurer) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the value of a SetPaddingValueCommand of the same groups and
       * side.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      PaddingSide m_side;
      double m_value;
      TextMeasurer m_measurer;
  };

  /**
   * Returns the padding of a group whose padding is on.
   * @param document The document holding the group.
   * @param id The group's id.
   * @return The padding, or <code>std::nullopt</code> if the group has no
   *         padding on.
   */
  std::optional<Margins> get_padding(
    const Document& document, const NodeId& id);

  /**
   * Returns the direction a group's visible children are spread along, the
   * direction a stack made of them takes.
   * @param document The document holding the group.
   * @param id The group's id.
   * @param measurer The function measuring text.
   * @return HORIZONTAL if the children's centers spread further across than
   *         down, otherwise VERTICAL.
   */
  Orientation get_natural_orientation(const Document& document,
    const NodeId& id, const TextMeasurer& measurer);
}

#endif

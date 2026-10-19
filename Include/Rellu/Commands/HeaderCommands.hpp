#ifndef RELLU_COMMANDS_HEADER_COMMANDS_HPP
#define RELLU_COMMANDS_HEADER_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/NodeHeader.hpp"

namespace Rellu {

  /** Marks nodes for export or unmarks them, labelled Mark for Export. */
  class SetExportMarkCommand : public Command {
    public:

      /**
       * Constructs a SetExportMarkCommand.
       * @param nodes The nodes to change.
       * @param is_marked Whether the nodes are marked for export.
       */
      SetExportMarkCommand(std::vector<NodeId> nodes, bool is_marked) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      bool m_is_marked;
  };

  /**
   * Sets whether nodes keep their place while their artboard scrolls,
   * labelled Fix Position When Scrolling.
   */
  class SetFixedPositionCommand : public Command {
    public:

      /**
       * Constructs a SetFixedPositionCommand.
       * @param nodes The nodes to change.
       * @param is_fixed Whether the nodes keep their place.
       */
      SetFixedPositionCommand(
        std::vector<NodeId> nodes, bool is_fixed) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      bool m_is_fixed;
  };

  /**
   * Locks the aspect ratio of nodes to their current size, or unlocks it,
   * labelled Lock Aspect.
   */
  class SetAspectLockCommand : public Command {
    public:

      /**
       * Constructs a SetAspectLockCommand.
       * @param nodes The nodes to change.
       * @param is_locked Whether the nodes' aspect ratios are locked.
       * @param measurer The function measuring text.
       */
      SetAspectLockCommand(std::vector<NodeId> nodes, bool is_locked,
        TextMeasurer measurer = estimate_text_bounds) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      bool m_is_locked;
      TextMeasurer m_measurer;
  };

  /**
   * The constraint properties a SetConstraintsCommand sets, absent ones
   * kept.
   */
  struct ConstraintsEdit {

    /** Whether the constraints apply at all, Responsive Resize. */
    std::optional<bool> m_is_enabled;

    /** Whether the constraints were set explicitly, Manual. */
    std::optional<bool> m_is_custom;

    /** Whether the distance to the parent's left edge stays fixed. */
    std::optional<bool> m_left;

    /** Whether the distance to the parent's right edge stays fixed. */
    std::optional<bool> m_right;

    /** Whether the distance to the parent's top edge stays fixed. */
    std::optional<bool> m_top;

    /** Whether the distance to the parent's bottom edge stays fixed. */
    std::optional<bool> m_bottom;

    /** Whether the width stays fixed. */
    std::optional<bool> m_width;

    /** Whether the height stays fixed. */
    std::optional<bool> m_height;

    bool operator ==(const ConstraintsEdit&) const = default;
  };

  /**
   * Sets the responsive resize constraints of nodes, labelled Change
   * Constraints.
   */
  class SetConstraintsCommand : public Command {
    public:

      /**
       * Constructs a SetConstraintsCommand.
       * @param nodes The nodes to change.
       * @param edit The properties to set.
       */
      SetConstraintsCommand(
        std::vector<NodeId> nodes, ConstraintsEdit edit) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the properties set by a SetConstraintsCommand changing the
       * same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      ConstraintsEdit m_edit;
  };

  /** Removes the 3D transforms of nodes, labelled Reset 3D Transforms. */
  class ResetTransform3dCommand : public Command {
    public:

      /**
       * Constructs a ResetTransform3dCommand.
       * @param nodes The nodes to change.
       */
      explicit ResetTransform3dCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
  };

  /**
   * Makes the command mirroring nodes about the center of their bounds,
   * labelled Flip Horizontally or Flip Vertically.
   * @param document The document holding the nodes.
   * @param nodes The nodes to mirror.
   * @param is_horizontal Whether the nodes are mirrored left to right rather
   *        than top to bottom.
   * @param measurer The function measuring text.
   * @return The command, or <code>nullptr</code> if no node can be mirrored
   *         or every node is a component instance, a repeat grid or a
   *         scroll group.
   */
  std::unique_ptr<Command> make_flip_command(const Document& document,
    const std::vector<NodeId>& nodes, bool is_horizontal,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Makes the command moving and resizing nodes so that their bounds lie on
   * whole document pixels, labelled Align to Pixel Grid.
   * @param document The document holding the nodes.
   * @param nodes The nodes to align.
   * @param measurer The function measuring text.
   * @return The command, or <code>nullptr</code> if every node is aligned.
   */
  std::unique_ptr<Command> make_pixel_grid_command(const Document& document,
    const std::vector<NodeId>& nodes,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Adds the commands object.transform.flip_horizontally,
   * object.transform.flip_vertically, object.transform.reset_3d,
   * object.align_to_pixel_grid and object.mark_for_export, which toggles the
   * export mark of the selection.
   * @param registry The registry to add to.
   * @param measurer The function measuring text.
   */
  void add_header_commands(CommandRegistry& registry, TextMeasurer measurer);
}

#endif

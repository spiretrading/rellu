#ifndef RELLU_COMMANDS_ALIGN_COMMANDS_HPP
#define RELLU_COMMANDS_ALIGN_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Geometry/Rect.hpp"

namespace Rellu {

  /** Lists the edges and centers nodes are aligned by. */
  enum class ObjectAlignment : unsigned char {

    /** The left edges. */
    LEFT,

    /** The horizontal centers. */
    CENTER,

    /** The right edges. */
    RIGHT,

    /** The top edges. */
    TOP,

    /** The vertical centers. */
    MIDDLE,

    /** The bottom edges. */
    BOTTOM
  };

  /** Lists the axes along which nodes are distributed. */
  enum class DistributionAxis : unsigned char {

    /** Left to right. */
    HORIZONTAL,

    /** Top to bottom. */
    VERTICAL
  };

  /**
   * Lists the nodes an align or distribute command moves, as Adobe XD does:
   * the top-level nodes among the targets that are on the canvas and
   * unlocked.
   * @param document The document holding the targets.
   * @param targets The targets, such as the selection.
   */
  std::vector<NodeId> get_alignable(
    const Document& document, const std::vector<NodeId>& targets);

  /**
   * Returns the bounds in document space that nodes align to, as Adobe XD
   * does: the union of their bounding boxes when there are several, a
   * single node's artboard otherwise.
   * @param document The document holding the nodes.
   * @param nodes The nodes aligned, as listed by get_alignable.
   * @param measurer The function measuring text.
   * @return The bounds, or <code>std::nullopt</code> if there is nothing to
   *         align to, as for a single artboard, a single node on the
   *         pasteboard or a single node within a group.
   */
  std::optional<Rect> get_alignment_bounds(const Document& document,
    const std::vector<NodeId>& nodes,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Moves nodes so that an edge or center of each one's bounding box lines
   * up with the bounds returned by get_alignment_bounds, labelled Align.
   */
  class AlignCommand : public Command {
    public:

      /**
       * Constructs an AlignCommand.
       * @param nodes The nodes to align.
       * @param alignment The edge or center aligned.
       * @param measurer The function measuring text.
       */
      AlignCommand(std::vector<NodeId> nodes, ObjectAlignment alignment,
        TextMeasurer measurer) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      ObjectAlignment m_alignment;
      TextMeasurer m_measurer;
  };

  /**
   * Moves nodes along an axis so that the gaps between their bounding
   * boxes are equal, keeping the outermost edges in place, labelled
   * Distribute.
   */
  class DistributeCommand : public Command {
    public:

      /**
       * Constructs a DistributeCommand.
       * @param nodes The nodes to distribute.
       * @param axis The axis distributed along.
       * @param measurer The function measuring text.
       */
      DistributeCommand(std::vector<NodeId> nodes, DistributionAxis axis,
        TextMeasurer measurer) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      DistributionAxis m_axis;
      TextMeasurer m_measurer;
  };

  /**
   * Adds the align and distribute commands acting on targets:
   * object.align.left, object.align.center, object.align.right,
   * object.align.top, object.align.middle and object.align.bottom, which
   * apply to a node with something to align to and to several nodes, and
   * object.distribute.horizontally and object.distribute.vertically, which
   * apply to three nodes or more.
   * @param registry The registry to add to.
   * @param measurer The function measuring text.
   */
  void add_align_commands(CommandRegistry& registry,
    TextMeasurer measurer = estimate_text_bounds);
}

#endif

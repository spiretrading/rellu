#ifndef RELLU_COMMANDS_TRANSFORM_COMMANDS_HPP
#define RELLU_COMMANDS_TRANSFORM_COMMANDS_HPP
#include <memory>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/ResponsiveResize.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"

namespace Rellu {

  /**
   * Sets the transform from a node's local space to its parent's: a
   * reference's transform override, otherwise the header's transform of any
   * node but an artboard, which has none.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param transform The new transform.
   */
  void set_transform(const Document& document, Transaction& transaction,
    const NodeId& id, const Matrix& transform);

  /**
   * Moves a node by a displacement in document space.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param delta The displacement.
   */
  void translate_node(const Document& document, Transaction& transaction,
    const NodeId& id, Point delta);

  /** Lists the ways a TransformCommand applies its mapping. */
  enum class TransformMode : unsigned char {

    /**
     * Transforms the nodes, then updates the contents of the artboards
     * resized, as an edit in the property inspector does.
     */
    RESIZE,

    /**
     * Transforms the nodes, leaving the contents of the artboards resized
     * as they are, as a handle being dragged does.
     */
    DRAG,

    /**
     * Updates the contents of the artboards among the nodes without
     * transforming anything, as releasing a dragged handle does.
     */
    SETTLE
  };

  /**
   * Applies a mapping in document space to a node, resizing its geometry
   * rather than scaling its transform where the mapping scales along the
   * node's own axes, and fitting the font sizes of point text to the bounds
   * it is mapped to. A mapping that mirrors along those axes, as a resize
   * past the opposite side or a flip does, mirrors the geometry and fills
   * of shapes and the contents of plain and mask groups, toggles whether
   * text is flipped vertically within its bounds, turning its transform by
   * 180 degrees for a horizontal mirror as Adobe XD records it, and only
   * moves other nodes, such as component instances, whose contents are
   * left as they are, as Adobe XD does.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param mapping The mapping in document space.
   * @param measurer The function measuring text.
   */
  void transform_node(const Document& document, Transaction& transaction,
    const NodeId& id, const Matrix& mapping,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Moves the nodes on the pasteboard that overlap an artboard's frame into
   * the artboard on top of its children, keeping their order, and moves
   * each of the artboard's children that no longer overlaps its frame on
   * top of the topmost other artboard it overlaps, or else on top of the
   * pasteboard, as Adobe XD does when an artboard is drawn or resized.
   * Hidden and locked nodes move too, and every node keeps where it
   * appears on the canvas.
   * @param document The document holding the artboard.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The artboard's id.
   */
  void update_artboard_contents(
    const Document& document, Transaction& transaction, const NodeId& id);

  /** Moves nodes by a displacement in document space. */
  class TranslateCommand : public Command {
    public:

      /**
       * Constructs a TranslateCommand labelled Move.
       * @param nodes The nodes to move.
       * @param delta The displacement.
       */
      TranslateCommand(std::vector<NodeId> nodes, Point delta);

      /**
       * Constructs a TranslateCommand.
       * @param nodes The nodes to move.
       * @param delta The displacement.
       * @param label The label naming the operation.
       */
      TranslateCommand(
        std::vector<NodeId> nodes, Point delta, std::string label) noexcept;

      /** Returns the nodes moved. */
      const std::vector<NodeId>& get_nodes() const;

      /** Returns the displacement. */
      Point get_delta() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Adds the displacement of a TranslateCommand with the same label
       * moving the same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      Point m_delta;
      std::string m_label;
  };

  /** Applies a mapping in document space to nodes, as by resize_nodes. */
  class TransformCommand : public Command {
    public:

      /**
       * Constructs a TransformCommand in the RESIZE mode scaling the
       * contents of containers.
       * @param nodes The nodes to transform.
       * @param mapping The mapping in document space.
       * @param label The label naming the operation.
       */
      TransformCommand(std::vector<NodeId> nodes, const Matrix& mapping,
        std::string label) noexcept;

      /**
       * Constructs a TransformCommand scaling the contents of containers.
       * @param nodes The nodes to transform.
       * @param mapping The mapping in document space.
       * @param label The label naming the operation.
       * @param mode The way the mapping is applied.
       */
      TransformCommand(std::vector<NodeId> nodes, const Matrix& mapping,
        std::string label, TransformMode mode) noexcept;

      /**
       * Constructs a TransformCommand.
       * @param nodes The nodes to transform.
       * @param mapping The mapping in document space.
       * @param label The label naming the operation.
       * @param mode The way the mapping is applied.
       * @param rule The way the contents of containers follow.
       * @param measurer The function measuring text.
       */
      TransformCommand(std::vector<NodeId> nodes, const Matrix& mapping,
        std::string label, TransformMode mode, ResizeRule rule,
        TextMeasurer measurer) noexcept;

      /** Returns the nodes transformed. */
      const std::vector<NodeId>& get_nodes() const;

      /** Returns the mapping in document space. */
      const Matrix& get_mapping() const;

      /** Returns the way the contents of containers follow. */
      ResizeRule get_rule() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Composes the mapping of a TransformCommand with the same label
       * transforming the same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      Matrix m_mapping;
      std::string m_label;
      TransformMode m_mode;
      ResizeRule m_rule;
      TextMeasurer m_measurer;
  };

  /**
   * Makes a command labelled Nudge moving nodes by a displacement in
   * document space.
   * @param nodes The nodes to move.
   * @param delta The displacement.
   */
  std::unique_ptr<TranslateCommand> make_nudge_command(
    std::vector<NodeId> nodes, Point delta);

  /**
   * Makes a command labelled Resize mapping a rect in document space, such
   * as the selection's bounds, onto another, laying out the contents of
   * containers by their constraints.
   * @param nodes The nodes to resize.
   * @param from The rect before resizing.
   * @param to The rect after resizing.
   * @param measurer The function measuring text.
   */
  std::unique_ptr<TransformCommand> make_resize_command(
    std::vector<NodeId> nodes, const Rect& from, const Rect& to,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Makes a command labelled Resize setting a node's bounds in its local
   * space, so that it is resized along its own axes, laying out the contents
   * of containers by their constraints.
   * @param document The document holding the node.
   * @param id The node's id.
   * @param bounds The new bounds in the node's local space.
   * @param measurer The function measuring text.
   * @return The command, or <code>nullptr</code> if the node draws nothing
   *         or its transform is singular.
   */
  std::unique_ptr<TransformCommand> make_bounds_command(
    const Document& document, const NodeId& id, const Rect& bounds,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Makes a command labelled Rotate rotating nodes about a point.
   * @param nodes The nodes to rotate.
   * @param angle The angle in radians, clockwise on screen.
   * @param center The point in document space that remains fixed.
   */
  std::unique_ptr<TransformCommand> make_rotate_command(
    std::vector<NodeId> nodes, double angle, Point center);
}

#endif

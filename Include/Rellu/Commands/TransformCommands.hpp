#ifndef RELLU_COMMANDS_TRANSFORM_COMMANDS_HPP
#define RELLU_COMMANDS_TRANSFORM_COMMANDS_HPP
#include <memory>
#include <string>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
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

  /**
   * Applies a mapping in document space to a node, resizing its geometry
   * rather than scaling its transform where the mapping scales along the
   * node's own axes. A mapping that mirrors along those axes, as a resize
   * past the opposite side does, mirrors the geometry and fills of shapes
   * and of the contents of plain groups, and only moves other nodes, so
   * that no transform is left mirrored, as Adobe XD does.
   * @param document The document holding the node.
   * @param transaction The open transaction on the <i>document</i>.
   * @param id The node's id.
   * @param mapping The mapping in document space.
   */
  void transform_node(const Document& document, Transaction& transaction,
    const NodeId& id, const Matrix& mapping);

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

  /** Applies a mapping in document space to nodes, as by transform_node. */
  class TransformCommand : public Command {
    public:

      /**
       * Constructs a TransformCommand.
       * @param nodes The nodes to transform.
       * @param mapping The mapping in document space.
       * @param label The label naming the operation.
       */
      TransformCommand(std::vector<NodeId> nodes, const Matrix& mapping,
        std::string label) noexcept;

      /** Returns the nodes transformed. */
      const std::vector<NodeId>& get_nodes() const;

      /** Returns the mapping in document space. */
      const Matrix& get_mapping() const;

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
   * as the selection's bounds, onto another.
   * @param nodes The nodes to resize.
   * @param from The rect before resizing.
   * @param to The rect after resizing.
   */
  std::unique_ptr<TransformCommand> make_resize_command(
    std::vector<NodeId> nodes, const Rect& from, const Rect& to);

  /**
   * Makes a command labelled Resize setting a node's bounds in its local
   * space, so that it is resized along its own axes.
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

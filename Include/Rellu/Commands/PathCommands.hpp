#ifndef RELLU_COMMANDS_PATH_COMMANDS_HPP
#define RELLU_COMMANDS_PATH_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /**
   * Returns the style Adobe XD gives a path drawn with its pen tool: no fill
   * and a 1 pixel #707070 centered border.
   */
  Style make_default_path_style();

  /**
   * Returns the label Adobe XD's menus give a boolean operation: Add,
   * Subtract, Intersect or Exclude Overlap.
   * @param operation The operation.
   */
  std::string get_operation_label(CompoundOperation operation);

  /**
   * Returns the prefix Adobe XD names the compound shapes made by a boolean
   * operation with: Union, Subtraction, Intersection or Exclusion followed
   * by a space.
   * @param operation The operation.
   */
  std::string get_name_prefix(CompoundOperation operation);

  /**
   * Replaces the geometry of a shape with a path, keeping its header and
   * style, so that editing the points of a rectangle, ellipse, polygon or
   * line converts it to a path.
   */
  class SetPathCommand : public Command {
    public:

      /**
       * Constructs a SetPathCommand.
       * @param label The label naming the operation, such as Edit Path.
       * @param id The shape node.
       * @param geometry The path in the node's local space.
       */
      SetPathCommand(std::string label, const NodeId& id,
        PathShape geometry) noexcept;

      /** Returns the node changed. */
      const NodeId& get_node() const;

      /** Returns the path set. */
      const PathShape& get_geometry() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Takes the path of a SetPathCommand changing the same node. */
      bool merge(const Command& command) override;

    private:
      std::string m_label;
      NodeId m_node;
      PathShape m_geometry;
  };

  /**
   * Tests whether Convert to Path changes a node: a shape that is not
   * already a path.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_convertible_to_path(const Document& document, const NodeId& id);

  /**
   * Converts shapes to paths drawing the same outline, labelled Convert to
   * Path. A compound shape becomes a path holding its result, and its
   * operands are removed. Nodes that are not convertible are left unchanged.
   */
  class ConvertToPathCommand : public Command {
    public:

      /**
       * Constructs a ConvertToPathCommand.
       * @param nodes The shapes to convert.
       */
      explicit ConvertToPathCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
  };

  /**
   * Combines shapes into a new compound shape, as Adobe XD's boolean
   * operations do, labelled after the operation. The compound shape takes
   * the place of the topmost shape with the style of the bottom-most one,
   * holds the shapes as its operands, each keeping where it appears on the
   * canvas, and stores the result of combining them. It is named after the
   * operation with the next number free, such as Union 1.
   */
  class BooleanCommand : public Command {
    public:

      /**
       * Constructs a BooleanCommand.
       * @param nodes The shapes to combine, at least two.
       * @param operation The operation combining them.
       */
      BooleanCommand(
        std::vector<NodeId> nodes, CompoundOperation operation) noexcept;

      /** Returns the compound shape made by the last application, or nil. */
      const NodeId& get_compound() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      CompoundOperation m_operation;
      NodeId m_compound;
  };

  /**
   * Changes the operation of compound shapes and recomputes their results,
   * labelled after the operation.
   */
  class SetCompoundOperationCommand : public Command {
    public:

      /**
       * Constructs a SetCompoundOperationCommand.
       * @param nodes The compound shapes to change.
       * @param operation The operation to set.
       */
      SetCompoundOperationCommand(
        std::vector<NodeId> nodes, CompoundOperation operation) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      CompoundOperation m_operation;
  };

  /**
   * Tests whether Outline Stroke changes a node: a shape other than a
   * compound shape or its operand whose border is enabled, or a group or
   * mask group holding such a node.
   * @param document The document holding the node.
   * @param id The node's id.
   */
  bool is_outline_strokable(const Document& document, const NodeId& id);

  /**
   * Turns the borders of shapes into filled paths as Adobe XD's Outline
   * Stroke does, labelled Outline Stroke, acting on the shapes within
   * groups and mask groups. A shape without a fill, or a line, is replaced
   * by a path of the same id and name filled with its border's color.
   * Otherwise the path, named after the shape followed by - Outline, is
   * placed above the shape, whose border is disabled, along with copies of
   * its interactions, and the two are grouped under the shape's name,
   * opacity and blend mode when the shape is translucent, blended or in a
   * stack. The path keeps the shape's disabled border and its hidden
   * shadows and blur, which are hidden on a shape kept too.
   */
  class OutlineStrokeCommand : public Command {
    public:

      /**
       * Constructs an OutlineStrokeCommand.
       * @param nodes The nodes to outline, those that cannot be outlined
       *        staying selected.
       */
      explicit OutlineStrokeCommand(std::vector<NodeId> nodes) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      std::vector<NodeId> m_nodes;
      std::vector<NodeId> m_selection;

      void outline(const Document& document, Transaction& transaction,
        const NodeId& id, std::vector<NodeId>& results);
  };

  /**
   * Adds the commands of Adobe XD's Object > Path menu acting on targets:
   * object.path.add, object.path.subtract, object.path.intersect and
   * object.path.exclude, which combine two or more shapes or change the
   * operation of a single compound shape, object.path.convert and
   * object.path.outline_stroke.
   * @param registry The registry to add to.
   */
  void add_path_commands(CommandRegistry& registry);
}

#endif

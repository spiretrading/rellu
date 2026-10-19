#ifndef RELLU_MODEL_COMPOUND_RESOLUTION_HPP
#define RELLU_MODEL_COMPOUND_RESOLUTION_HPP
#include <functional>
#include <optional>
#include "Rellu/Geometry/BooleanOperation.hpp"
#include "Rellu/Geometry/Path.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeStore.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"

namespace Rellu {

  /**
   * Returns the result a compound shape found among the operands of another
   * contributes, in the found shape's local space.
   */
  using NestedCompound = std::function<Path (const Node& compound)>;

  /**
   * Returns the boolean operation combining a compound shape's operands.
   * @param operation The compound shape's operation.
   */
  BooleanOperation to_boolean_operation(CompoundOperation operation);

  /**
   * Returns the outline an operand of a compound shape contributes, in the
   * space of the operand's parent: a shape's outline, the union of the
   * outlines of a group's children, or the outline of what a reference
   * renders, each placed by its transform. A compound shape among them
   * contributes its stored result.
   * @param nodes The nodes to resolve within.
   * @param operand The operand.
   * @param tolerance The greatest distance a curve may be flattened by.
   * @param nested The results of compound shapes among the operand's
   *        descendants, or empty to use their stored results.
   * @return The outline, or <code>std::nullopt</code> if the operand or a
   *         descendant is not something Adobe XD combines, such as text or
   *         a masked group, or is a broken reference.
   */
  std::optional<Path> get_operand_outline(const NodeStore& nodes,
    const Node& operand, double tolerance, const NestedCompound& nested = {});

  /**
   * Combines the operands of a compound shape the way Adobe XD computes its
   * result.
   * @param nodes The nodes to resolve within.
   * @param compound The compound shape's node.
   * @param tolerance The greatest distance a curve may be flattened by.
   * @param nested The results of compound shapes among the operands'
   *        descendants, or empty to use their stored results.
   * @return The result in the compound shape's local space, or
   *         <code>std::nullopt</code> if the node is not a compound shape,
   *         has no operands or has an operand get_operand_outline rejects.
   */
  std::optional<Path> combine_operands(const NodeStore& nodes,
    const Node& compound, double tolerance, const NestedCompound& nested = {});
}

#endif

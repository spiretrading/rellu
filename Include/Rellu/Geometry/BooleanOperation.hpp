#ifndef RELLU_GEOMETRY_BOOLEAN_OPERATION_HPP
#define RELLU_GEOMETRY_BOOLEAN_OPERATION_HPP
#include <span>
#include "Rellu/Geometry/Path.hpp"

namespace Rellu {

  /**
   * Lists the operations combining the regions two paths fill, named after
   * Adobe XD's compound shape operations.
   */
  enum class BooleanOperation : unsigned char {

    /** The union, filling the region either path fills. */
    ADD,

    /**
     * The difference, filling the region the first path fills and the second
     * does not.
     */
    SUBTRACT,

    /** The intersection, filling the region both paths fill. */
    INTERSECT,

    /** The symmetric difference, filling the region exactly one path fills. */
    EXCLUDE
  };

  /**
   * Combines the regions two paths fill, each under its own fill rule and
   * treating every subpath as closed. Subpaths having a coordinate that is
   * not finite or whose magnitude exceeds 10^20 are ignored, and the
   * approximation coarsens as needed to represent large coordinates.
   * @param left The first operand.
   * @param right The second operand.
   * @param operation The operation to apply.
   * @param tolerance The maximum distance, which must be positive, between
   *        the curves of the operands and their approximation.
   * @return A Path using the non-zero fill rule whose closed polygonal
   *         subpaths do not overlap, with outer boundaries running clockwise
   *         on screen and holes running counterclockwise.
   */
  Path combine(const Path& left, const Path& right, BooleanOperation operation,
    double tolerance);

  /**
   * Combines the regions a sequence of paths fill, applying an operation to
   * the first two operands and then to that result and each subsequent
   * operand in turn, the way Adobe XD combines the children of a compound
   * shape. Subpaths having a coordinate that is not finite or whose
   * magnitude exceeds 10^20 are ignored, and the approximation coarsens as
   * needed to represent large coordinates.
   * @param operands The paths to combine, each under its own fill rule and
   *        treating every subpath as closed.
   * @param operation The operation to apply.
   * @param tolerance The maximum distance, which must be positive, between
   *        the curves of the <i>operands</i> and their approximation.
   * @return A Path of the same form combine returns for two operands, filling
   *         the region of the only operand if there is just one, or an empty
   *         Path if there are none.
   */
  Path combine(std::span<const Path> operands, BooleanOperation operation,
    double tolerance);
}

#endif

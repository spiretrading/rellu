#ifndef RELLU_COMMANDS_CORNER_RADIUS_COMMANDS_HPP
#define RELLU_COMMANDS_CORNER_RADIUS_COMMANDS_HPP
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Shapes.hpp"

namespace Rellu {

  /** Lists the corners of a rectangle, in the order Adobe XD stores them. */
  enum class Corner : unsigned char {

    /** The top left corner. */
    TOP_LEFT,

    /** The top right corner. */
    TOP_RIGHT,

    /** The bottom right corner. */
    BOTTOM_RIGHT,

    /** The bottom left corner. */
    BOTTOM_LEFT
  };

  /**
   * Sets the corner radii of rectangles and the corner radius of polygons,
   * labelled Corner Radius. Radii are at least zero, and nodes of other
   * kinds are left unchanged.
   */
  class SetCornerRadiusCommand : public Command {
    public:

      /**
       * Constructs a SetCornerRadiusCommand giving every corner of the
       * rectangles and polygons the same radius.
       * @param nodes The nodes to change.
       * @param radius The radius.
       */
      SetCornerRadiusCommand(std::vector<NodeId> nodes, double radius) noexcept;

      /**
       * Constructs a SetCornerRadiusCommand setting each corner of the
       * rectangles.
       * @param nodes The nodes to change.
       * @param radii The radius of each corner.
       */
      SetCornerRadiusCommand(
        std::vector<NodeId> nodes, const CornerRadii& radii) noexcept;

      /**
       * Constructs a SetCornerRadiusCommand setting one corner of the
       * rectangles.
       * @param nodes The nodes to change.
       * @param corner The corner.
       * @param radius The corner's radius.
       */
      SetCornerRadiusCommand(
        std::vector<NodeId> nodes, Corner corner, double radius) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Combines a SetCornerRadiusCommand changing the same nodes. */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      std::array<std::optional<double>, 4> m_radii;
      std::optional<double> m_polygon_radius;
  };
}

#endif

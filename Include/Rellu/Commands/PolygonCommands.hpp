#ifndef RELLU_COMMANDS_POLYGON_COMMANDS_HPP
#define RELLU_COMMANDS_POLYGON_COMMANDS_HPP
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"

namespace Rellu {

  /**
   * Sets the number of corners of polygons, deriving their vertices anew,
   * labelled Corner Count.
   */
  class SetCornerCountCommand : public Command {
    public:

      /** The least number of corners. */
      static constexpr auto MINIMUM_CORNERS = 3;

      /** The greatest number of corners. */
      static constexpr auto MAXIMUM_CORNERS = 100;

      /**
       * Constructs a SetCornerCountCommand.
       * @param nodes The nodes to change; those that are not polygons are
       *        left unchanged.
       * @param count The number of corners, clamped to the range
       *        [MINIMUM_CORNERS, MAXIMUM_CORNERS].
       */
      SetCornerCountCommand(std::vector<NodeId> nodes, int count) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the count of a SetCornerCountCommand changing the same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      int m_count;
  };

  /**
   * Sets the star ratio of polygons, deriving their vertices anew, labelled
   * Star Ratio.
   */
  class SetStarRatioCommand : public Command {
    public:

      /** The least star ratio, as a percentage. */
      static constexpr auto MINIMUM_RATIO = 1.0;

      /** The greatest star ratio, as a percentage, that of a polygon. */
      static constexpr auto MAXIMUM_RATIO = 100.0;

      /**
       * Constructs a SetStarRatioCommand.
       * @param nodes The nodes to change; those that are not polygons are
       *        left unchanged.
       * @param ratio The star ratio as a percentage, clamped to the range
       *        [MINIMUM_RATIO, MAXIMUM_RATIO].
       */
      SetStarRatioCommand(std::vector<NodeId> nodes, double ratio) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the ratio of a SetStarRatioCommand changing the same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      double m_ratio;
  };
}

#endif

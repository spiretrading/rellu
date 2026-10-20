#ifndef RELLU_COMMANDS_STYLE_COMMANDS_HPP
#define RELLU_COMMANDS_STYLE_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /**
   * Sets the fill of shapes and text, and the background of artboards,
   * labelled Change Fill unless given another label. A group's fill is set
   * on the shapes and text within it. A solid fill also colors every
   * character of a text node.
   */
  class SetFillCommand : public Command {
    public:

      /**
       * Constructs a SetFillCommand labelled Change Fill.
       * @param nodes The nodes to change.
       * @param fill The new fill.
       */
      SetFillCommand(std::vector<NodeId> nodes, Fill fill) noexcept;

      /**
       * Constructs a SetFillCommand.
       * @param nodes The nodes to change.
       * @param fill The new fill.
       * @param label The label naming the operation.
       */
      SetFillCommand(
        std::vector<NodeId> nodes, Fill fill, std::string label) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the fill of a SetFillCommand with the same label changing the
       * same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      Fill m_fill;
      std::string m_label;
  };

  /** The stroke properties a SetStrokeCommand sets, absent ones kept. */
  struct StrokeEdit {

    /** The color. */
    std::optional<Color> m_color;

    /** The width of the line. */
    std::optional<double> m_width;

    /** Where the line lies relative to the outline. */
    std::optional<StrokeAlignment> m_alignment;

    /** The shape at the ends of open lines and dashes. */
    std::optional<StrokeCap> m_cap;

    /** The shape where segments meet. */
    std::optional<StrokeJoin> m_join;

    /** The miter limit. */
    std::optional<double> m_miter_limit;

    /** The dash and gap lengths. */
    std::optional<std::vector<double>> m_dashes;

    /** Whether the stroke is drawn. */
    std::optional<bool> m_is_enabled;

    bool operator ==(const StrokeEdit&) const = default;
  };

  /**
   * Sets stroke properties of shapes and text, labelled Change Stroke,
   * adding a default stroke to those without one. A group's stroke is set
   * on the shapes and text within it.
   */
  class SetStrokeCommand : public Command {
    public:

      /**
       * Constructs a SetStrokeCommand.
       * @param nodes The nodes to change.
       * @param edit The properties to set.
       */
      SetStrokeCommand(std::vector<NodeId> nodes, StrokeEdit edit) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the properties set by a SetStrokeCommand changing the same
       * nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      StrokeEdit m_edit;
  };
}

#endif

#ifndef RELLU_COMMANDS_ARTBOARD_COMMANDS_HPP
#define RELLU_COMMANDS_ARTBOARD_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/Artboard.hpp"

namespace Rellu {

  /** The grid properties a SetGridCommand sets, absent ones kept. */
  struct GridEdit {

    /** Whether the grid is displayed. */
    std::optional<bool> m_is_visible;

    /** The kind of grid displayed. */
    std::optional<GridType> m_type;

    /** The number of columns of the layout grid. */
    std::optional<int> m_column_count;

    /** The space between columns of the layout grid. */
    std::optional<double> m_gutter;

    /**
     * The width of each column of the layout grid, kept by moving the left
     * and right margins equally.
     */
    std::optional<double> m_column_width;

    /** The top margin of the layout grid. */
    std::optional<double> m_top;

    /** The right margin of the layout grid. */
    std::optional<double> m_right;

    /** The bottom margin of the layout grid. */
    std::optional<double> m_bottom;

    /** The left margin of the layout grid. */
    std::optional<double> m_left;

    /** The color of the layout grid's columns. */
    std::optional<Color> m_column_color;

    /** The size of each cell of the square grid. */
    std::optional<double> m_square_size;

    /** The color of the square grid's lines. */
    std::optional<Color> m_square_color;

    bool operator ==(const GridEdit&) const = default;
  };

  /**
   * Returns the grid Adobe XD gives an artboard that has none: a hidden
   * layout grid of 4 columns with a gutter of 10 within left and right
   * margins of 37, colored #00FFFF at 25%, and square cells of 8 ruled in
   * #C7C7C7 at 50%.
   * @param artboard The artboard.
   */
  LayoutGrid make_default_grid(const Artboard& artboard);

  /**
   * Returns the width of each column of a layout grid on an artboard.
   * @param grid The grid.
   * @param width The artboard's width.
   */
  double get_column_width(const LayoutGrid& grid, double width);

  /**
   * Sets properties of the design grid of artboards, labelled Change Grid,
   * giving those without one the default grid first.
   */
  class SetGridCommand : public Command {
    public:

      /**
       * Constructs a SetGridCommand.
       * @param artboards The artboards to change; other nodes are left
       *        unchanged.
       * @param edit The properties to set.
       */
      SetGridCommand(std::vector<NodeId> artboards, GridEdit edit) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the properties set by a SetGridCommand changing the same
       * artboards.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_artboards;
      GridEdit m_edit;
  };

  /**
   * Makes artboards scroll vertically within a viewport or stop scrolling,
   * labelled Change Scrolling.
   */
  class SetViewportCommand : public Command {
    public:

      /**
       * Constructs a SetViewportCommand.
       * @param artboards The artboards to change; other nodes are left
       *        unchanged.
       * @param height The height of the viewport, or none to stop
       *        scrolling. An artboard that starts scrolling without a
       *        height given takes its own height.
       * @param is_scrolling Whether the artboards scroll.
       */
      SetViewportCommand(std::vector<NodeId> artboards,
        std::optional<double> height, bool is_scrolling) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the viewport of a SetViewportCommand changing the same
       * artboards.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_artboards;
      std::optional<double> m_height;
      bool m_is_scrolling;
  };

  /**
   * Returns the artboards a view command acts on: those selected or holding
   * a selected node, or every artboard when there are none.
   * @param document The document.
   * @param nodes The selected nodes.
   */
  std::vector<NodeId> get_target_artboards(
    const Document& document, const std::vector<NodeId>& nodes);

  /**
   * Adds the commands view.layout_grid and view.square_grid, which show the
   * layout or square grid of the target artboards, or hide it when every
   * target artboard shows it.
   * @param registry The registry to add to.
   */
  void add_artboard_commands(CommandRegistry& registry);
}

#endif

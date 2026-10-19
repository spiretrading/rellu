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

    /**
     * Whether the grid is first reset to the default for its artboard's
     * width, keeping whether and which grid is shown, as Use Default does.
     */
    std::optional<bool> m_is_default;

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
   * layout grid colored #00FFFF at 25% whose columns, gutter and margins
   * depend on the artboard's width (1 column within margins of 28 up to
   * 312, 4 with a gutter of 10 and margins of 36 up to 599, 8 with a gutter
   * of 16 and margins of 80 up to 834, 12 with a gutter of 16 and margins
   * of 120 up to 1024, otherwise margins of 140), the margins then adjusted
   * so that the columns are about whole widths, and square cells of 8
   * ruled in #C7C7C7 at 50%. Grids made the default replace the square
   * grid, and their layout grids by width replace the layout grid.
   * @param artboard The artboard.
   * @param defaults The grids made the default.
   */
  LayoutGrid make_default_grid(
    const Artboard& artboard, const GridDefaults& defaults = GridDefaults());

  /** Returns Adobe XD's own layout grids by artboard width. */
  std::vector<GridOverride> get_built_in_overrides();

  /**
   * Adapts a layout grid to its artboard's new size as Adobe XD does when
   * the artboard is resized: the bottom and then the top margin shrink so
   * that rows remain, and for a new width the gutter narrows so that every
   * column remains and the margins move so that the columns are about whole
   * widths.
   * @param grid The layout grid.
   * @param size The artboard's new size.
   */
  void fit_layout_grid(LayoutGrid& grid, Size size);

  /**
   * Returns the grids made the default by Make Default: the grid itself,
   * and the layout grids by width with the grid's layout given to artboards
   * up to the artboard's width.
   * @param grid The artboard's grid.
   * @param width The artboard's width.
   * @param defaults The grids made the default until now.
   */
  GridDefaults make_grid_defaults(const LayoutGrid& grid, double width,
    const GridDefaults& defaults);

  /**
   * Returns the width of each column of a layout grid on an artboard,
   * rounded down to a whole width as Adobe XD shows it.
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
   * Makes the grid of an artboard the grid given to artboards, labelled
   * Make Default.
   */
  class MakeGridDefaultCommand : public Command {
    public:

      /**
       * Constructs a MakeGridDefaultCommand.
       * @param artboard The artboard.
       */
      explicit MakeGridDefaultCommand(const NodeId& artboard) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      NodeId m_artboard;
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

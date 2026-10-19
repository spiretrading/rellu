#ifndef RELLU_MODEL_ARTBOARD_HPP
#define RELLU_MODEL_ARTBOARD_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Layout.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /** Lists the kinds of grid an artboard displays. */
  enum class GridType : unsigned char {

    /** A grid of equal squares. */
    SQUARE,

    /** Columns separated by gutters within margins. */
    LAYOUT
  };

  /** The design grid of an artboard, holding both kinds of grid. */
  struct LayoutGrid {

    /** The kind of grid displayed. */
    GridType m_type = GridType::SQUARE;

    /** Whether the grid is displayed. */
    bool m_is_visible = false;

    /** The number of columns of the layout grid. */
    int m_column_count = 0;

    /** The space between columns of the layout grid. */
    double m_gutter = 0;

    /** The artboard width the layout grid was designed for. */
    double m_default_width = 0;

    /** The space around the columns of the layout grid. */
    Margins m_margins;

    /** The color of the layout grid's columns. */
    Color m_column_color = Color(0, 0, 0, 0);

    /** The color of the layout grid's rows. */
    Color m_row_color = Color(0, 0, 0, 0);

    /** The size of each cell of the square grid. */
    Size m_square_size = Size(0, 0);

    /** The color of the square grid's vertical lines. */
    Color m_square_column_color = Color(0, 0, 0, 0);

    /** The color of the square grid's horizontal lines. */
    Color m_square_row_color = Color(0, 0, 0, 0);

    bool operator ==(const LayoutGrid& grid) const;
  };

  /** The layout grid Adobe XD gives artboards up to a width. */
  struct GridOverride {

    /** The widest artboard the grid applies to, or none for no limit. */
    std::optional<double> m_maximum_width;

    /** The number of columns. */
    int m_column_count = 0;

    /** The space between columns. */
    double m_gutter = 0;

    /** The space around the columns. */
    Margins m_margins;

    /** The color of the columns. */
    Color m_column_color = Color(0, 0, 0, 0);

    /** The color of the rows. */
    Color m_row_color = Color(0, 0, 0, 0);

    bool operator ==(const GridOverride&) const = default;
  };

  /** The grids Adobe XD gives artboards, as set by Make Default. */
  struct GridDefaults {

    /**
     * The grid made the default, whose square grid applies to new grids, or
     * none.
     */
    std::optional<LayoutGrid> m_grid;

    /**
     * The layout grids by artboard width, or none for Adobe XD's own.
     */
    std::optional<std::vector<GridOverride>> m_overrides;

    bool operator ==(const GridDefaults&) const = default;
  };

  /** A guide line on an artboard. */
  struct Guide {

    /** The direction the guide runs. */
    Orientation m_orientation = Orientation::HORIZONTAL;

    /** The offset of the guide from the artboard's origin. */
    double m_position = 0;

    /** The guide's identifier, or empty if it has none. */
    std::string m_id;

    bool operator ==(const Guide& guide) const;
  };

  /**
   * The payload of an artboard node, a top-level frame whose children are
   * positioned relative to its origin.
   */
  struct Artboard {

    /** The frame in document space. */
    Rect m_frame = Rect(0, 0, 0, 0);

    /** The background. */
    Fill m_background;

    /** The height of the visible viewport of a scrolling artboard. */
    std::optional<double> m_viewport_height;

    /** The design grid, if any. */
    std::optional<LayoutGrid> m_layout_grid;

    /** The guides. */
    std::vector<Guide> m_guides;

    bool operator ==(const Artboard& artboard) const;
  };

  inline bool LayoutGrid::operator ==(const LayoutGrid& grid) const {
    return m_type == grid.m_type && m_is_visible == grid.m_is_visible &&
      m_column_count == grid.m_column_count &&
      equals(m_gutter, grid.m_gutter) &&
      equals(m_default_width, grid.m_default_width) &&
      m_margins == grid.m_margins && m_column_color == grid.m_column_color &&
      m_row_color == grid.m_row_color &&
      m_square_size == grid.m_square_size &&
      m_square_column_color == grid.m_square_column_color &&
      m_square_row_color == grid.m_square_row_color;
  }

  inline bool Guide::operator ==(const Guide& guide) const {
    return m_orientation == guide.m_orientation &&
      equals(m_position, guide.m_position) && m_id == guide.m_id;
  }

  inline bool Artboard::operator ==(const Artboard& artboard) const {
    return m_frame == artboard.m_frame &&
      m_background == artboard.m_background &&
      equals(m_viewport_height, artboard.m_viewport_height) &&
      m_layout_grid == artboard.m_layout_grid &&
      m_guides == artboard.m_guides;
  }
}

#endif

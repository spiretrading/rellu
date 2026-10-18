#ifndef RELLU_MODEL_ARTBOARD_HPP
#define RELLU_MODEL_ARTBOARD_HPP
#include <optional>
#include <vector>
#include "Rellu/Geometry/Color.hpp"
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

    bool operator ==(const LayoutGrid&) const = default;
  };

  /** A guide line on an artboard. */
  struct Guide {

    /** The direction the guide runs. */
    Orientation m_orientation = Orientation::HORIZONTAL;

    /** The offset of the guide from the artboard's origin. */
    double m_position = 0;

    bool operator ==(const Guide&) const = default;
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

    bool operator ==(const Artboard&) const = default;
  };
}

#endif

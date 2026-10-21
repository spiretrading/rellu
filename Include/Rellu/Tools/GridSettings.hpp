#ifndef RELLU_TOOLS_GRID_SETTINGS_HPP
#define RELLU_TOOLS_GRID_SETTINGS_HPP
#include "Rellu/Model/Artboard.hpp"

namespace Rellu {

  /**
   * How the design grids of every artboard are shown in a window, as Adobe
   * XD's View menu and the grid section of its property inspector set it.
   */
  struct GridSettings {

    /** Whether the layout grid of every artboard is shown. */
    bool m_is_layout_visible = false;

    /** Whether the square grid of every artboard is shown. */
    bool m_is_square_visible = false;

    /** The kind of grid shown most recently, offered while none is shown. */
    GridType m_last_type = GridType::LAYOUT;

    bool operator ==(const GridSettings&) const = default;
  };

  /**
   * Returns <code>true</code> iff either kind of grid is shown.
   * @param settings The settings.
   */
  bool is_grid_visible(const GridSettings& settings);

  /**
   * Returns the kind of grid the grid section offers: the kind shown, or the
   * kind shown most recently while none is.
   * @param settings The settings.
   */
  GridType get_offered_grid(const GridSettings& settings);

  /**
   * Returns settings showing or hiding each kind of grid, a kind being shown
   * becoming the kind shown most recently.
   * @param settings The settings.
   * @param is_layout_visible Whether the layout grid is shown.
   * @param is_square_visible Whether the square grid is shown.
   */
  GridSettings show_grids(GridSettings settings, bool is_layout_visible,
    bool is_square_visible);

  /**
   * Returns settings toggling a kind of grid as Adobe XD's View > Show Layout
   * Grid and Show Square Grid do, hiding the other kind.
   * @param settings The settings.
   * @param type The kind of grid toggled.
   */
  GridSettings toggle_grid(const GridSettings& settings, GridType type);

  /**
   * Returns settings showing only one kind of grid, as choosing the grid
   * type in Adobe XD's property inspector does.
   * @param settings The settings.
   * @param type The kind of grid shown.
   */
  GridSettings choose_grid(const GridSettings& settings, GridType type);

  inline bool is_grid_visible(const GridSettings& settings) {
    return settings.m_is_layout_visible || settings.m_is_square_visible;
  }

  inline GridType get_offered_grid(const GridSettings& settings) {
    if(settings.m_is_square_visible) {
      return GridType::SQUARE;
    } else if(settings.m_is_layout_visible) {
      return GridType::LAYOUT;
    }
    return settings.m_last_type;
  }

  inline GridSettings show_grids(GridSettings settings, bool is_layout_visible,
      bool is_square_visible) {
    if(is_layout_visible && !settings.m_is_layout_visible) {
      settings.m_last_type = GridType::LAYOUT;
    }
    settings.m_is_layout_visible = is_layout_visible;
    if(is_square_visible && !settings.m_is_square_visible) {
      settings.m_last_type = GridType::SQUARE;
    }
    settings.m_is_square_visible = is_square_visible;
    return settings;
  }

  inline GridSettings toggle_grid(const GridSettings& settings, GridType type) {
    if(type == GridType::SQUARE) {
      return show_grids(show_grids(settings, settings.m_is_layout_visible,
        !settings.m_is_square_visible), false, !settings.m_is_square_visible);
    }
    return show_grids(
      show_grids(settings, settings.m_is_layout_visible, false),
      !settings.m_is_layout_visible, false);
  }

  inline GridSettings choose_grid(const GridSettings& settings, GridType type) {
    auto is_square = type == GridType::SQUARE;
    return show_grids(show_grids(settings, settings.m_is_layout_visible,
      is_square), !is_square, is_square);
  }
}

#endif

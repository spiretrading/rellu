#include <doctest/doctest.h>
#include "Rellu/Tools/GridSettings.hpp"

using namespace Rellu;

TEST_SUITE("GridSettings") {
  TEST_CASE("toggle_grid") {
    auto settings = GridSettings();
    REQUIRE(!is_grid_visible(settings));
    REQUIRE(get_offered_grid(settings) == GridType::LAYOUT);
    settings = toggle_grid(settings, GridType::SQUARE);
    REQUIRE(settings.m_is_square_visible);
    REQUIRE(!settings.m_is_layout_visible);
    REQUIRE(settings.m_last_type == GridType::SQUARE);
    REQUIRE(get_offered_grid(settings) == GridType::SQUARE);
    settings = toggle_grid(settings, GridType::LAYOUT);
    REQUIRE(settings.m_is_layout_visible);
    REQUIRE(!settings.m_is_square_visible);
    REQUIRE(settings.m_last_type == GridType::LAYOUT);
    settings = toggle_grid(settings, GridType::SQUARE);
    settings = toggle_grid(settings, GridType::SQUARE);
    REQUIRE(!is_grid_visible(settings));
    REQUIRE(settings.m_last_type == GridType::SQUARE);
    REQUIRE(get_offered_grid(settings) == GridType::SQUARE);
  }

  TEST_CASE("choose_grid") {
    auto settings = choose_grid(GridSettings(), GridType::SQUARE);
    REQUIRE(settings.m_is_square_visible);
    REQUIRE(!settings.m_is_layout_visible);
    settings = choose_grid(settings, GridType::LAYOUT);
    REQUIRE(settings.m_is_layout_visible);
    REQUIRE(!settings.m_is_square_visible);
    REQUIRE(settings.m_last_type == GridType::LAYOUT);
  }

  TEST_CASE("show_grids") {
    auto settings = GridSettings();
    settings.m_last_type = GridType::SQUARE;
    REQUIRE(show_grids(settings, false, false).m_last_type ==
      GridType::SQUARE);
    REQUIRE(show_grids(settings, true, false).m_last_type ==
      GridType::LAYOUT);
    settings.m_is_layout_visible = true;
    settings.m_last_type = GridType::SQUARE;
    REQUIRE(show_grids(settings, true, false).m_last_type ==
      GridType::SQUARE);
  }
}

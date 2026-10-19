#include <doctest/doctest.h>
#include "Rellu/Editor/TextSelection.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("TextSelection") {
  TEST_CASE("pending_style") {
    auto document = Document();
    auto scene = build_scene(document);
    auto selection = TextSelection(document);
    auto count = 0;
    auto subscription = selection.subscribe([&] { ++count; });
    auto style = document.get(scene.m_text).get_text()->m_base_style;
    style.m_font.m_size = 40;
    selection.set_pending_style(style);
    REQUIRE(!selection.get_pending_style());
    REQUIRE(count == 0);
    selection.set(scene.m_text, 2, 2);
    REQUIRE(count == 1);
    selection.set_pending_style(style);
    REQUIRE(selection.get_pending_style() == style);
    REQUIRE(count == 2);
    selection.set_pending_style(style);
    REQUIRE(count == 2);
    selection.set(scene.m_text, 2, 2);
    REQUIRE(selection.get_pending_style() == style);
    selection.set(scene.m_text, 3, 3);
    REQUIRE(!selection.get_pending_style());
    REQUIRE(count == 3);
    selection.set_pending_style(style);
    selection.clear();
    REQUIRE(!selection.get_pending_style());
  }
}

#include <doctest/doctest.h>
#include "Rellu/Commands/EffectCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("EffectCommands") {
  TEST_CASE("default_effects") {
    auto shadow = std::get<DropShadow>(
      make_default_effect(EffectKind::DROP_SHADOW));
    REQUIRE(shadow.m_offset == Point(0, 3));
    REQUIRE(shadow.m_blur == 3);
    REQUIRE(shadow.m_color == Color(0, 0, 0, 0.16));
    REQUIRE(shadow.m_is_visible);
    REQUIRE(std::holds_alternative<InnerShadow>(
      make_default_effect(EffectKind::INNER_SHADOW)));
    auto blur = std::get<Blur>(make_default_effect(EffectKind::BLUR));
    REQUIRE(blur.m_is_background);
    REQUIRE(blur.m_amount == 10);
  }

  TEST_CASE("add_effect") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto edit = EffectEdit();
    edit.m_is_visible = true;
    require_undoable(history, std::make_unique<SetEffectCommand>(
      std::vector{scene.m_rectangle, scene.m_text, scene.m_group},
      EffectKind::DROP_SHADOW, edit));
    REQUIRE(history.get_undo_label() == "Change Effect");
    auto& effects =
      document.get(scene.m_rectangle).get_shape()->m_style.m_effects;
    REQUIRE(effects.size() == 1);
    REQUIRE(effects.front() == make_default_effect(EffectKind::DROP_SHADOW));
    REQUIRE(find_effect(
      document.get(scene.m_text).get_text()->m_style.m_effects,
      EffectKind::DROP_SHADOW));
  }

  TEST_CASE("edit_effect") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto nodes = std::vector{scene.m_rectangle};
    auto offset = EffectEdit();
    offset.m_x = 5;
    offset.m_y = -2;
    auto blur = EffectEdit();
    blur.m_blur = 12;
    blur.m_color = make_color(255, 0, 0);
    REQUIRE(history.merge(std::make_unique<SetEffectCommand>(
      nodes, EffectKind::INNER_SHADOW, offset)));
    REQUIRE(history.merge(std::make_unique<SetEffectCommand>(
      nodes, EffectKind::INNER_SHADOW, blur)));
    REQUIRE(history.get_undo_count() == 1);
    auto& effects =
      document.get(scene.m_rectangle).get_shape()->m_style.m_effects;
    auto shadow = std::get<InnerShadow>(
      *find_effect(effects, EffectKind::INNER_SHADOW));
    REQUIRE(shadow.m_offset == Point(5, -2));
    REQUIRE(shadow.m_blur == 12);
    REQUIRE(shadow.m_color == make_color(255, 0, 0));
    auto hide = EffectEdit();
    hide.m_is_visible = false;
    require_undoable(history, std::make_unique<SetEffectCommand>(
      nodes, EffectKind::INNER_SHADOW, hide));
    REQUIRE(!std::get<InnerShadow>(*find_effect(
      document.get(scene.m_rectangle).get_shape()->m_style.m_effects,
      EffectKind::INNER_SHADOW)).m_is_visible);
  }

  TEST_CASE("blur") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto edit = EffectEdit();
    edit.m_is_background = false;
    edit.m_blur = 7;
    edit.m_fill_opacity = 2;
    require_undoable(history, std::make_unique<SetEffectCommand>(
      std::vector{scene.m_rectangle}, EffectKind::BLUR, edit));
    auto blur = std::get<Blur>(*find_effect(
      document.get(scene.m_rectangle).get_shape()->m_style.m_effects,
      EffectKind::BLUR));
    REQUIRE(!blur.m_is_background);
    REQUIRE(blur.m_amount == 7);
    REQUIRE(blur.m_fill_opacity == 1);
  }
}

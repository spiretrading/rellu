#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Prototype/Easing.hpp"
#include "Stage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  Animation make_linear(Transition transition, double duration = 1) {
    return Animation(transition, Easing::LINEAR, duration);
  }

  void tap(PreviewPlayer& player, Point position) {
    player.press(position);
    player.release(position);
  }

  void require_layer(const PreviewLayer& layer, const NodeId& artboard,
      Point position, double opacity = 1) {
    REQUIRE(layer.m_artboard == artboard);
    REQUIRE(layer.m_position.m_x == doctest::Approx(position.m_x));
    REQUIRE(layer.m_position.m_y == doctest::Approx(position.m_y));
    REQUIRE(layer.m_opacity == doctest::Approx(opacity));
  }

  const Rect& get_bounds(const Document& document, const NodeId& id) {
    return std::get<RectangleShape>(
      document.get(id).get_shape()->m_geometry).m_bounds;
  }
}

TEST_SUITE("PreviewPlayer") {
  TEST_CASE("start") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto player = make_player(document, scene.m_home);
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(player->get_viewport() == Size(400, 300));
    REQUIRE(player->get_history().empty());
    REQUIRE(!player->is_animating());
    auto layers = player->get_layers();
    REQUIRE(layers.size() == 1);
    require_layer(layers[0], scene.m_home, Point(0, 0));
    REQUIRE(layers[0].m_size == Size(400, 300));
    REQUIRE_THROWS_AS(player->start(scene.m_button), std::invalid_argument);
  }

  TEST_CASE("tap") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    REQUIRE(!player->find_interaction(Point(300, 250), TapTrigger()));
    REQUIRE(player->find_interaction(Point(20, 20), TapTrigger()));
    REQUIRE(!player->find_interaction(Point(20, 20), HoverTrigger()));
    tap(*player, Point(300, 250));
    REQUIRE(player->get_artboard() == scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_second);
    REQUIRE(!player->is_animating());
    REQUIRE(player->get_history() == std::vector<NodeId>({scene.m_home}));
  }

  TEST_CASE("disabled_interaction") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto id = add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    auto interaction = document.get_prototype().m_interactions.front();
    interaction.m_is_enabled = false;
    auto transaction = Transaction(document);
    transaction.replace_interaction(interaction);
    transaction.commit();
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(player->trigger(id));
    REQUIRE(player->get_artboard() == scene.m_second);
  }

  TEST_CASE("innermost_target") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_home, TapTrigger(),
      ArtboardTransition(scene.m_menu, make_linear(Transition::NONE)));
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_second);
    player->start(scene.m_home);
    tap(*player, Point(300, 250));
    REQUIRE(player->get_artboard() == scene.m_menu);
  }

  TEST_CASE("dissolve") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::DISSOLVE)));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->is_animating());
    REQUIRE(player->get_artboard() == scene.m_second);
    auto layers = player->get_layers();
    REQUIRE(layers.size() == 2);
    require_layer(layers[0], scene.m_home, Point(0, 0));
    require_layer(layers[1], scene.m_second, Point(0, 0), 0);
    player->advance(0.5);
    require_layer(player->get_layers()[1], scene.m_second, Point(0, 0), 0.5);
    player->advance(0.5);
    REQUIRE(!player->is_animating());
    layers = player->get_layers();
    REQUIRE(layers.size() == 1);
    require_layer(layers[0], scene.m_second, Point(0, 0));
  }

  TEST_CASE("easing") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second,
        Animation(Transition::DISSOLVE, Easing::EASE_OUT, 0.4)));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    player->advance(0.1);
    require_layer(player->get_layers()[1], scene.m_second, Point(0, 0),
      ease(Easing::EASE_OUT, 0.25));
    player->advance(0.3);
    REQUIRE(!player->is_animating());
  }

  TEST_CASE("slides") {
    auto expect = [] (Transition transition, Point to) {
      auto document = Document();
      auto scene = build_prototype_scene(document);
      add_interaction(document, scene.m_button, TapTrigger(),
        ArtboardTransition(scene.m_second, make_linear(transition)));
      auto player = make_player(document, scene.m_home);
      tap(*player, Point(20, 20));
      player->advance(0.25);
      auto layers = player->get_layers();
      REQUIRE(layers.size() == 2);
      require_layer(layers[0], scene.m_home, Point(0, 0));
      require_layer(layers[1], scene.m_second, to);
    };
    expect(Transition::SLIDE_LEFT, Point(300, 0));
    expect(Transition::SLIDE_RIGHT, Point(-300, 0));
    expect(Transition::SLIDE_UP, Point(0, 225));
    expect(Transition::SLIDE_DOWN, Point(0, -225));
  }

  TEST_CASE("pushes") {
    auto expect = [] (Transition transition, Point from, Point to) {
      auto document = Document();
      auto scene = build_prototype_scene(document);
      add_interaction(document, scene.m_button, TapTrigger(),
        ArtboardTransition(scene.m_second, make_linear(transition)));
      auto player = make_player(document, scene.m_home);
      tap(*player, Point(20, 20));
      player->advance(0.25);
      auto layers = player->get_layers();
      REQUIRE(layers.size() == 2);
      require_layer(layers[0], scene.m_home, from);
      require_layer(layers[1], scene.m_second, to);
    };
    expect(Transition::PUSH_LEFT, Point(-100, 0), Point(300, 0));
    expect(Transition::PUSH_RIGHT, Point(100, 0), Point(-300, 0));
    expect(Transition::PUSH_UP, Point(0, -75), Point(0, 225));
    expect(Transition::PUSH_DOWN, Point(0, 75), Point(0, -225));
  }

  TEST_CASE("previous") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second,
        make_linear(Transition::SLIDE_LEFT)));
    add_interaction(document, scene.m_back, TapTrigger(), PreviousArtboard());
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    player->advance(1);
    REQUIRE(player->get_artboard() == scene.m_second);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(player->get_history().empty());
    player->advance(0.25);
    auto layers = player->get_layers();
    REQUIRE(layers.size() == 2);
    require_layer(layers[0], scene.m_home, Point(0, 0));
    require_layer(layers[1], scene.m_second, Point(100, 0));
    player->advance(0.75);
    REQUIRE(player->get_layers().size() == 1);
    tap(*player, Point(300, 250));
    REQUIRE(!player->trigger(
      document.get_prototype().m_interactions.back().m_id));
  }

  TEST_CASE("overlay") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      Overlay(scene.m_menu, Point(50, 60), make_linear(Transition::NONE)));
    add_interaction(document, scene.m_menu, KeyTrigger("Esc"),
      PreviousArtboard());
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(player->get_overlays() == std::vector<PreviewOverlay>(
      {PreviewOverlay(scene.m_menu, Point(50, 60),
        make_linear(Transition::NONE))}));
    auto layers = player->get_layers();
    REQUIRE(layers.size() == 2);
    require_layer(layers[1], scene.m_menu, Point(50, 60));
    REQUIRE(layers[1].m_size == Size(200, 100));
    tap(*player, Point(60, 70));
    REQUIRE(player->get_overlays().size() == 1);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_overlays().empty());
    REQUIRE(player->get_artboard() == scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->press_key(KeyChord("Esc")));
    REQUIRE(player->get_overlays().empty());
  }

  TEST_CASE("overlay_animation") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      Overlay(scene.m_menu, Point(50, 60),
        make_linear(Transition::SLIDE_UP)));
    add_interaction(document, scene.m_second_box, TapTrigger(),
      Overlay(scene.m_menu, Point(0, 0), make_linear(Transition::DISSOLVE)));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    player->advance(0.5);
    require_layer(player->get_layers()[1], scene.m_menu, Point(50, 180));
    player->advance(0.5);
    require_layer(player->get_layers()[1], scene.m_menu, Point(50, 60));
    tap(*player, Point(390, 290));
    REQUIRE(player->get_overlays().empty());
    player->advance(0.75);
    auto layers = player->get_layers();
    REQUIRE(layers.size() == 2);
    require_layer(layers[1], scene.m_menu, Point(50, 240));
    player->advance(0.25);
    REQUIRE(player->get_layers().size() == 1);
  }

  TEST_CASE("navigation_from_overlay") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      Overlay(scene.m_menu, Point(0, 0), make_linear(Transition::NONE)));
    add_interaction(document, scene.m_menu, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_second);
    REQUIRE(player->get_overlays().empty());
  }

  TEST_CASE("hover_state") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_instance, HoverTrigger(),
      StateTransition(scene.m_hover, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    auto& stage = player->get_stage();
    REQUIRE(player->move(Point(55, 205)));
    REQUIRE(player->get_state(scene.m_instance) == scene.m_hover);
    REQUIRE(stage.get_children(scene.m_instance) ==
      std::vector<NodeId>({scene.m_hover_knob}));
    REQUIRE(!player->move(Point(80, 205)));
    REQUIRE(player->get_state(scene.m_instance) == scene.m_hover);
    REQUIRE(player->move(Point(300, 250)));
    REQUIRE(player->get_state(scene.m_instance).is_nil());
    REQUIRE(stage.get_children(scene.m_instance) ==
      std::vector<NodeId>({scene.m_knob}));
    player->move(Point(55, 205));
    REQUIRE(player->leave());
    REQUIRE(player->get_state(scene.m_instance).is_nil());
  }

  TEST_CASE("tap_state") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_instance, TapTrigger(),
      StateTransition(scene.m_hover, make_linear(Transition::NONE)));
    add_interaction(document, scene.m_hover_tree, TapTrigger(),
      StateTransition(scene.m_component, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(55, 205));
    REQUIRE(player->get_state(scene.m_instance) == scene.m_hover);
    tap(*player, Point(55, 205));
    REQUIRE(player->get_state(scene.m_instance).is_nil());
    tap(*player, Point(55, 205));
    REQUIRE(player->get_state(scene.m_instance) == scene.m_hover);
    player->start(scene.m_home);
    REQUIRE(player->get_state(scene.m_instance).is_nil());
    REQUIRE(player->get_stage().get_children(scene.m_instance) ==
      std::vector<NodeId>({scene.m_knob}));
  }

  TEST_CASE("auto_animate") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second,
        make_linear(Transition::AUTO_ANIMATE)));
    auto player = make_player(document, scene.m_home);
    auto& stage = player->get_stage();
    tap(*player, Point(20, 20));
    REQUIRE(player->is_animating());
    auto layers = player->get_layers();
    REQUIRE(layers.size() == 1);
    require_layer(layers[0], scene.m_second, Point(0, 0));
    REQUIRE(get_bounds(stage, scene.m_second_box) == Rect(0, 0, 50, 50));
    player->advance(0.5);
    REQUIRE(get_bounds(stage, scene.m_second_box) == Rect(0, 0, 75, 75));
    REQUIRE(stage.get(scene.m_second_box).m_header.m_transform ==
      make_translation(250, 150));
    REQUIRE(stage.get(scene.m_button).m_parent == scene.m_second);
    player->advance(0.5);
    REQUIRE(!player->is_animating());
    REQUIRE(get_bounds(stage, scene.m_second_box) == Rect(0, 0, 100, 100));
    REQUIRE(stage.get(scene.m_button).m_parent == scene.m_home);
  }

  TEST_CASE("shared_state") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto pressed = generate_node_id();
    auto tree = NodeId();
    auto knob = NodeId();
    {
      auto transaction = Transaction(document);
      tree = transaction.insert_root(
        make_stage_header("Pressed", 0, 0), Group());
      knob = add_box(transaction, tree, "Knob", Rect(0, 0, 60, 20),
        make_color(0, 0, 255));
      auto component =
        document.get_resources().m_components.at(scene.m_component);
      component.m_states.push_back(ComponentState(pressed, "Pressed", tree));
      transaction.replace_component(component);
      transaction.commit();
    }
    add_interaction(document, scene.m_instance, TapTrigger(),
      StateTransition(pressed, make_linear(Transition::AUTO_ANIMATE)));
    add_interaction(document, knob, TapTrigger(),
      StateTransition(scene.m_component, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    auto& stage = player->get_stage();
    tap(*player, Point(55, 205));
    REQUIRE(player->get_state(scene.m_instance) == pressed);
    auto children = stage.get_children(scene.m_instance);
    REQUIRE(children.size() == 1);
    REQUIRE(children[0] != knob);
    player->advance(0.5);
    REQUIRE(get_bounds(stage, children[0]) == Rect(0, 0, 40, 20));
    player->advance(0.5);
    REQUIRE(stage.get_children(tree) == std::vector<NodeId>({knob}));
    tap(*player, Point(100, 205));
    REQUIRE(player->get_state(scene.m_instance).is_nil());
    REQUIRE(!stage.find(children[0]));
    REQUIRE(stage.get_children(scene.m_instance) ==
      std::vector<NodeId>({scene.m_knob}));
  }

  TEST_CASE("auto_animate_state") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_instance, HoverTrigger(),
      StateTransition(scene.m_hover, make_linear(Transition::AUTO_ANIMATE)));
    auto player = make_player(document, scene.m_home);
    auto& stage = player->get_stage();
    player->move(Point(55, 205));
    REQUIRE(player->is_animating());
    player->advance(0.5);
    REQUIRE(get_bounds(stage, scene.m_hover_knob) == Rect(0, 0, 30, 20));
    player->advance(0.5);
    REQUIRE(get_bounds(stage, scene.m_hover_knob) == Rect(0, 0, 40, 20));
    player->move(Point(300, 250));
    player->advance(0.5);
    REQUIRE(get_bounds(stage, scene.m_knob) == Rect(0, 0, 30, 20));
    player->advance(0.5);
    REQUIRE(stage.get_children(scene.m_instance) ==
      std::vector<NodeId>({scene.m_knob}));
  }

  TEST_CASE("time") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_home, TimeTrigger(2),
      ArtboardTransition(scene.m_second, make_linear(Transition::DISSOLVE)));
    add_interaction(document, scene.m_second, TimeTrigger(0.5),
      ArtboardTransition(scene.m_home, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    REQUIRE(player->get_next_time() == 2);
    REQUIRE(!player->advance(1.9));
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(player->advance(0.2));
    REQUIRE(player->get_artboard() == scene.m_second);
    REQUIRE(!player->get_next_time());
    player->advance(1);
    REQUIRE(player->get_next_time() == doctest::Approx(3.5));
    player->advance(0.5);
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(player->get_next_time() == doctest::Approx(5.5));
  }

  TEST_CASE("keys") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, KeyTrigger("Ctrl+K"),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    add_interaction(document, scene.m_home, KeyTrigger("A", KeyDevice::GAMEPAD),
      ArtboardTransition(scene.m_menu, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    REQUIRE(!player->press_key(KeyChord("K")));
    REQUIRE(!player->press_key(KeyChord("A")));
    REQUIRE(player->press_key(parse_key_chord("Ctrl+K")));
    REQUIRE(player->get_artboard() == scene.m_second);
  }

  TEST_CASE("drag") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, DragTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_home);
    player->press(Point(20, 20));
    player->move(Point(21, 20));
    REQUIRE(player->get_artboard() == scene.m_home);
    player->move(Point(40, 20));
    REQUIRE(player->get_artboard() == scene.m_second);
    REQUIRE(!player->release(Point(40, 20)));
  }

  TEST_CASE("scroll") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_footer, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    add_interaction(document, scene.m_header, TapTrigger(),
      ArtboardTransition(scene.m_home, make_linear(Transition::NONE), true));
    auto player = make_player(document, scene.m_long);
    REQUIRE(player->get_viewport() == Size(400, 300));
    REQUIRE(player->scroll(1000));
    REQUIRE(player->get_scroll() == 500);
    REQUIRE(!player->scroll(10));
    REQUIRE(player->scroll(-100));
    REQUIRE(player->get_layers()[0].m_scroll == 400);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_long);
    tap(*player, Point(50, 250));
    REQUIRE(player->get_artboard() == scene.m_second);
    player->start(scene.m_long);
    player->scroll(20);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(player->get_scroll() == 0);
  }

  TEST_CASE("scroll_to") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto animation = make_linear(Transition::DISSOLVE);
    REQUIRE(get_scroll_target(document, scene.m_long,
      ScrollTransition(scene.m_footer, -400, animation), 500) == 200.0);
    REQUIRE(get_scroll_target(document, scene.m_long,
      ScrollTransition(scene.m_footer, 0, animation), 500) == 500.0);
    REQUIRE(get_scroll_target(document, scene.m_long,
      ScrollTransition(scene.m_header, -30, animation), 500) == 0.0);
    REQUIRE(!get_scroll_target(document, scene.m_long,
      ScrollTransition(scene.m_box, 0, animation), 500));
    REQUIRE(!get_scroll_target(document, scene.m_long,
      ScrollTransition(NodeId(), 0, animation), 500));
    add_interaction(document, scene.m_header, TapTrigger(),
      ScrollTransition(scene.m_footer, -150, animation));
    add_interaction(document, scene.m_footer, TapTrigger(),
      ScrollTransition(scene.m_header, 0, Animation(Transition::DISSOLVE,
        Easing::EASE_OUT, 0)));
    auto player = make_player(document, scene.m_long);
    tap(*player, Point(20, 20));
    REQUIRE(player->is_animating());
    REQUIRE(player->get_scroll() == 0);
    player->advance(0.5);
    REQUIRE(player->get_scroll() == doctest::Approx(225));
    REQUIRE(player->get_layers()[0].m_scroll == doctest::Approx(225));
    player->advance(0.5);
    REQUIRE(!player->is_animating());
    REQUIRE(player->get_scroll() == 450);
    REQUIRE(player->get_artboard() == scene.m_long);
    REQUIRE(player->get_history().empty());
    tap(*player, Point(50, 200));
    REQUIRE(player->get_scroll() == 0);
    REQUIRE(!player->is_animating());
    REQUIRE(player->get_reports().empty());
  }

  TEST_CASE("scroll_to_fixed") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto low = NodeId();
    auto middle = NodeId();
    auto pin = NodeId();
    {
      auto transaction = Transaction(document);
      low = add_box(transaction, scene.m_long, "Low", Rect(200, 600, 50, 20),
        make_color(0, 0, 255));
      middle = add_box(transaction, scene.m_long, "Middle",
        Rect(200, 250, 50, 20), make_color(0, 0, 255));
      pin = add_box(transaction, scene.m_long, "Pin", Rect(300, 10, 50, 20),
        make_color(0, 0, 255));
      for(auto& id : {low, middle, pin}) {
        auto header = document.get(id).m_header;
        header.m_is_fixed_position = true;
        transaction.replace_header(std::move(header));
      }
      transaction.commit();
      document.clear_history();
    }
    auto instant = Animation(Transition::DISSOLVE, Easing::EASE_OUT, 0);
    auto to_low = add_interaction(document, scene.m_header, KeyTrigger("A"),
      ScrollTransition(low, 0, instant));
    auto to_middle = add_interaction(document, scene.m_header,
      KeyTrigger("B"), ScrollTransition(middle, 0, instant));
    add_interaction(document, pin, TapTrigger(),
      ScrollTransition(scene.m_footer, 0, instant));
    auto player = make_player(document, scene.m_long);
    REQUIRE(player->trigger(to_low));
    REQUIRE(player->get_scroll() == 500);
    player->scroll(-1000);
    REQUIRE(player->trigger(to_middle));
    REQUIRE(player->get_scroll() == 250);
    player->scroll(-1000);
    tap(*player, Point(310, 15));
    REQUIRE(player->get_scroll() == 500);
    REQUIRE(player->get_reports().empty());
  }

  TEST_CASE("scroll_to_overlay") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto sheet = NodeId();
    auto target = NodeId();
    auto close = NodeId();
    auto card = NodeId();
    auto item = NodeId();
    auto go = NodeId();
    {
      auto transaction = Transaction(document);
      sheet = add_board(transaction, "Sheet", Rect(2000, 0, 300, 900),
        make_color(255, 255, 255));
      close = add_box(transaction, sheet, "Close", Rect(0, 0, 300, 50),
        make_color(0, 0, 0));
      target = add_box(transaction, sheet, "Target", Rect(0, 700, 300, 50),
        make_color(255, 0, 0));
      card = add_board(transaction, "Small", Rect(2500, 0, 200, 200),
        make_color(255, 255, 255));
      go = add_box(transaction, card, "Go", Rect(0, 0, 200, 20),
        make_color(0, 0, 0));
      item = add_box(transaction, card, "Item", Rect(0, 150, 200, 20),
        make_color(255, 0, 0));
      transaction.commit();
      document.clear_history();
    }
    auto instant = Animation(Transition::NONE, Easing::EASE_OUT, 0);
    auto show_sheet = add_interaction(document, scene.m_header,
      KeyTrigger("A"), Overlay(sheet, Point(40, 0), instant));
    auto show_small = add_interaction(document, scene.m_header,
      KeyTrigger("B"), Overlay(card, Point(40, 40), instant));
    add_interaction(document, close, TapTrigger(), ScrollTransition(target, 0,
      Animation(Transition::DISSOLVE, Easing::EASE_OUT, 0.3)));
    add_interaction(document, go, TapTrigger(),
      ScrollTransition(item, 0, instant));
    auto player = make_player(document, scene.m_long);
    player->set_window_height(540);
    REQUIRE(player->trigger(show_sheet));
    tap(*player, Point(60, 10));
    REQUIRE(player->is_animating());
    player->advance(0.3);
    REQUIRE(!player->is_animating());
    REQUIRE(player->get_overlays().back().m_scroll == 360);
    REQUIRE(player->get_layers().back().m_scroll == 360);
    REQUIRE(player->get_scroll() == 0);
    player->scroll(Point(60, 100), Point(0, -50));
    REQUIRE(player->get_overlays().back().m_scroll == 360);
    auto unbounded = make_player(document, scene.m_long);
    REQUIRE(unbounded->trigger(show_sheet));
    tap(*unbounded, Point(60, 10));
    unbounded->advance(0.3);
    REQUIRE(unbounded->get_overlays().back().m_scroll == 600);
    auto fitting = make_player(document, scene.m_long);
    REQUIRE(fitting->trigger(show_small));
    tap(*fitting, Point(50, 45));
    REQUIRE(!fitting->is_animating());
    REQUIRE(fitting->get_overlays().back().m_scroll == 0);
    REQUIRE(fitting->get_scroll() == 0);
    REQUIRE(player->get_reports().empty());
    REQUIRE(fitting->get_reports().empty());
  }

  TEST_CASE("position") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_footer, TapTrigger(),
      ArtboardTransition(scene.m_home, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_long);
    player->scroll(400);
    tap(*player, Point(50, 250));
    player->scroll(10);
    auto position = player->get_position();
    REQUIRE(position.m_artboard == scene.m_home);
    REQUIRE(position.m_history == std::vector<NodeId>({scene.m_long}));
    auto other = make_player(document, scene.m_second);
    other->start(position);
    REQUIRE(other->get_artboard() == scene.m_home);
    REQUIRE(other->get_history() == std::vector<NodeId>({scene.m_long}));
    position.m_artboard = generate_node_id();
    other->start(position);
    REQUIRE(other->get_artboard() == scene.m_home);
  }

  TEST_CASE("reports") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto missing = generate_node_id();
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(missing, make_linear(Transition::NONE)));
    add_interaction(document, scene.m_box, TapTrigger(),
      StateTransition(generate_node_id(), make_linear(Transition::NONE)));
    auto video = add_interaction(document, scene.m_home, TimeTrigger(5),
      VideoPlayback(scene.m_home, VideoCommand::PLAY));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    tap(*player, Point(210, 110));
    REQUIRE(!player->trigger(video));
    REQUIRE(!player->trigger(generate_node_id()));
    auto& reports = player->get_reports();
    REQUIRE(reports.size() == 4);
    REQUIRE(reports[0] == "Destination not found: " + to_string(missing));
    REQUIRE(reports[1].starts_with("State not found: "));
    REQUIRE(reports[2] == "Video playback is not played");
    REQUIRE(reports[3].starts_with("Interaction not found: "));
    REQUIRE(player->get_artboard() == scene.m_home);
  }

  TEST_CASE("scroll_group") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto group = NodeId();
    auto top = NodeId();
    {
      auto transaction = Transaction(document);
      auto payload = Group();
      payload.m_scroll_group =
        ScrollGroup(ScrollDirection::VERTICAL, Size(100, 100), Point(0, 0));
      group = transaction.append(
        scene.m_home, make_stage_header("List", 250, 10), payload);
      top = add_box(transaction, group, "Top", Rect(0, 0, 100, 100),
        make_color(0, 0, 0));
      add_box(transaction, group, "Bottom", Rect(0, 100, 100, 150),
        make_color(255, 0, 0));
      transaction.commit();
    }
    add_interaction(document, top, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    REQUIRE(player->scroll(Point(260, 20), Point(0, 100)));
    REQUIRE(player->get_scroll(group) == Point(0, 100));
    REQUIRE(player->get_scroll() == 0);
    REQUIRE(player->scroll(Point(260, 20), Point(0, 100)));
    REQUIRE(player->get_scroll(group) == Point(0, 150));
    REQUIRE(!player->scroll(Point(260, 20), Point(0, 10)));
    tap(*player, Point(260, 20));
    REQUIRE(player->get_artboard() == scene.m_home);
    player->press(Point(260, 20));
    player->move(Point(260, 120));
    player->release(Point(260, 120));
    REQUIRE(player->get_scroll(group) == Point(0, 50));
    REQUIRE(player->scroll(Point(260, 20), Point(0, -50)));
    REQUIRE(player->get_scroll(group) == Point(0, 0));
    REQUIRE(!player->scroll(Point(260, 20), Point(0, -10)));
    tap(*player, Point(260, 20));
    REQUIRE(player->get_artboard() == scene.m_second);
  }

  TEST_CASE("hyperlink") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      Hyperlink("https://www.example.com"));
    auto player = make_player(document, scene.m_home);
    tap(*player, Point(20, 20));
    REQUIRE(player->get_reports().size() == 1);
    auto opened = std::vector<std::string>();
    player->set_link_handler([&] (const std::string& url) {
      opened.push_back(url);
    });
    tap(*player, Point(20, 20));
    REQUIRE(opened == std::vector<std::string>({"https://www.example.com"}));
    REQUIRE(player->get_artboard() == scene.m_home);
  }

  TEST_CASE("text_link") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto label = NodeId();
    {
      auto transaction = Transaction(document);
      auto text = Text();
      text.m_content = Shared(std::u16string(u"Help"));
      text.m_frame = AreaFrame(Size(100, 20));
      label = transaction.append(
        scene.m_home, make_stage_header("Help", 10, 250), text);
      transaction.commit();
    }
    add_interaction(document, label, TapTrigger(),
      ArtboardTransition(scene.m_second, make_linear(Transition::NONE)));
    auto player = make_player(document, scene.m_home);
    auto opened = std::vector<std::string>();
    player->set_link_handler([&] (const std::string& url) {
      opened.push_back(url);
    });
    auto positions = std::vector<Point>();
    player->set_text_link_finder([&] (const Node& node, Point position) {
      positions.push_back(position);
      return node.get_id() == label ? std::string("https://help") :
        std::string();
    });
    tap(*player, Point(20, 255));
    REQUIRE(opened == std::vector<std::string>({"https://help"}));
    REQUIRE(player->get_artboard() == scene.m_home);
    REQUIRE(positions.size() == 1);
    REQUIRE(positions.front() == Point(10, 5));
    tap(*player, Point(300, 280));
    REQUIRE(opened.size() == 1);
  }
}

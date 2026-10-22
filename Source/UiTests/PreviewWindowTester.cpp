#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <doctest/doctest.h>
#include "Rellu/Ui/PreviewWindow.hpp"
#include "../PrototypeTests/Stage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  void click(PreviewWindow& window, QPointF position) {
    auto press = QMouseEvent(QEvent::MouseButtonPress, position, position,
      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window.get_view(), &press);
    auto release = QMouseEvent(QEvent::MouseButtonRelease, position, position,
      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&window.get_view(), &release);
  }
}

TEST_SUITE("PreviewWindow") {
  TEST_CASE("start") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    REQUIRE(find_preview_start(document, {scene.m_second_box}) ==
      scene.m_second);
    REQUIRE(find_preview_start(document, std::vector<NodeId>()) ==
      scene.m_home);
    {
      auto transaction = Transaction(document);
      transaction.set_home_artboard(scene.m_menu);
      transaction.commit();
    }
    REQUIRE(find_preview_start(document, std::vector<NodeId>()) ==
      scene.m_home);
    {
      auto transaction = Transaction(document);
      transaction.set_flows({Flow(scene.m_long, "Flow 1")});
      transaction.commit();
    }
    REQUIRE(find_preview_start(document, std::vector<NodeId>()) ==
      scene.m_long);
    {
      auto transaction = Transaction(document);
      transaction.set_flows(
        {Flow(scene.m_long, "Flow 1"), Flow(scene.m_second, "Flow 2")});
      transaction.commit();
    }
    REQUIRE(find_preview_start(document, std::vector<NodeId>()) ==
      scene.m_second);
    REQUIRE(find_preview_start(Document(), std::vector<NodeId>()).is_nil());
  }

  TEST_CASE("play") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto window = PreviewWindow();
    window.play(document, scene.m_home);
    REQUIRE(window.get_document() == &document);
    REQUIRE(window.get_player()->get_artboard() == scene.m_home);
    REQUIRE(window.size() == QSize(400, 300));
    window.update_frame();
    REQUIRE(window.get_zoom() == 1);
    auto& list = window.get_view().get_renderer().get_draw_list();
    REQUIRE(list);
    REQUIRE(!list->empty());
    window.resize(200, 300);
    window.update_frame();
    REQUIRE(window.get_zoom() == 0.5);
    REQUIRE(window.to_viewport(Point(100, 150)) == Point(200, 150));
    window.stop();
    REQUIRE(!window.get_player());
    REQUIRE(!window.get_view().get_renderer().get_draw_list());
  }

  TEST_CASE("input") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second,
        Animation(Transition::DISSOLVE, Easing::LINEAR, 0.3)));
    add_interaction(document, scene.m_second, KeyTrigger("Esc"),
      ArtboardTransition(scene.m_home, Animation()));
    auto window = PreviewWindow();
    window.play(document, scene.m_home);
    click(window, QPointF(20, 20));
    auto& player = *window.get_player();
    REQUIRE(player.get_artboard() == scene.m_second);
    REQUIRE(player.is_animating());
    window.advance(0.15);
    REQUIRE(player.get_layers().size() == 2);
    window.advance(0.2);
    REQUIRE(!player.is_animating());
    auto key = QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&window.get_view(), &key);
    REQUIRE(player.get_artboard() == scene.m_home);
  }

  TEST_CASE("reload") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    add_interaction(document, scene.m_button, TapTrigger(),
      ArtboardTransition(scene.m_second, Animation()));
    auto window = PreviewWindow();
    window.play(document, scene.m_home);
    click(window, QPointF(20, 20));
    auto transaction = Transaction(document);
    auto header = document.get(scene.m_second).m_header;
    header.m_name = "Renamed";
    transaction.replace_header(header);
    transaction.commit();
    window.reload();
    auto& player = *window.get_player();
    REQUIRE(player.get_artboard() == scene.m_second);
    REQUIRE(player.get_history() == std::vector<NodeId>({scene.m_home}));
    REQUIRE(player.get_stage().get(scene.m_second).m_header.m_name ==
      "Renamed");
  }
}

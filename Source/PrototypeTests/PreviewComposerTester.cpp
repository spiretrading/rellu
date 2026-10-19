#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Prototype/PreviewComposer.hpp"
#include "Stage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  std::vector<const FillCommand*> get_fills(const DrawList& list) {
    auto fills = std::vector<const FillCommand*>();
    for(auto& command : list) {
      if(auto fill = std::get_if<FillCommand>(&command)) {
        fills.push_back(fill);
      }
    }
    return fills;
  }
}

TEST_SUITE("PreviewComposer") {
  TEST_CASE("translate") {
    auto path = std::make_shared<const Path>(make_rectangle(
      Rect(0, 0, 10, 10)));
    auto list = DrawList();
    list.push_back(FillCommand(path, make_translation(5, 5),
      SolidPaint(Color(0, 0, 0, 1)), Rect(0, 0, 100, 100)));
    auto begin = BeginLayerCommand();
    begin.m_mask = LayerMask(path, Matrix());
    list.push_back(begin);
    list.push_back(EndLayerCommand());
    auto out = DrawList();
    translate(list, Point(10, 20), Rect(0, 0, 50, 50), out);
    REQUIRE(out.size() == 3);
    auto& fill = std::get<FillCommand>(out[0]);
    REQUIRE(fill.m_transform == make_translation(15, 25));
    REQUIRE(fill.m_clip == Rect(10, 20, 40, 30));
    auto& layer = std::get<BeginLayerCommand>(out[1]);
    REQUIRE(layer.m_mask->m_transform == make_translation(10, 20));
    REQUIRE(layer.m_clip == Rect(0, 0, 50, 50));
    REQUIRE(std::holds_alternative<EndLayerCommand>(out[2]));
  }

  TEST_CASE("layers") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto composer = PreviewComposer(document, 0.25);
    auto frame = composer.compose({PreviewLayer(scene.m_second, NodeId(),
      Point(0, 0), Size(400, 300), 0, 1)});
    auto fills = get_fills(*frame);
    REQUIRE(fills.size() == 3);
    REQUIRE(fills[0]->m_transform == make_translation(0, 0));
    REQUIRE(fills[0]->m_clip == Rect(0, 0, 400, 300));
    REQUIRE(fills[1]->m_transform == make_translation(300, 200));
    auto faded = composer.compose({PreviewLayer(scene.m_second, NodeId(),
      Point(100, 0), Size(400, 300), 0, 0.5)});
    REQUIRE(std::get<BeginLayerCommand>(faded->front()).m_opacity == 0.5);
    REQUIRE(std::holds_alternative<EndLayerCommand>(faded->back()));
    auto moved = get_fills(*faded);
    REQUIRE(moved[0]->m_transform == make_translation(100, 0));
    REQUIRE(moved[0]->m_clip == Rect(100, 0, 400, 300));
    REQUIRE(composer.compose({PreviewLayer(scene.m_second, NodeId(),
      Point(0, 0), Size(400, 300), 0, 0)})->empty());
  }

  TEST_CASE("scroll") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto composer = PreviewComposer(document, 0.25);
    auto frame = composer.compose({PreviewLayer(scene.m_long, NodeId(),
      Point(0, 0), Size(400, 300), 100, 1)});
    auto fills = get_fills(*frame);
    REQUIRE(fills.size() == 3);
    REQUIRE(fills[1]->m_transform == make_translation(0, -100));
    REQUIRE(fills[2]->m_transform == make_translation(0, 500));
    REQUIRE(fills[2]->m_clip == Rect(0, 0, 400, 300));
  }

  TEST_CASE("changes") {
    auto document = Document();
    auto scene = build_prototype_scene(document);
    auto composer = PreviewComposer(document, 0.25);
    auto layer = PreviewLayer(
      scene.m_second, NodeId(), Point(0, 0), Size(400, 300), 0, 1);
    composer.compose({layer});
    auto transaction = Transaction(document);
    auto header = document.get(scene.m_second_box).m_header;
    header.m_transform = make_translation(0, 0);
    transaction.replace_header(header);
    transaction.commit();
    auto frame = composer.compose({layer});
    auto fills = get_fills(*frame);
    REQUIRE(fills[1]->m_transform == make_translation(0, 0));
  }
}

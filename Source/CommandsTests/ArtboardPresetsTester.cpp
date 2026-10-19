#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("ArtboardPresets") {
  TEST_CASE("presets") {
    auto& presets = get_artboard_presets();
    auto titles = std::vector<std::string>();
    auto counts = std::vector<std::size_t>();
    for(auto& group : presets) {
      titles.push_back(group.m_name);
      counts.push_back(group.m_presets.size());
    }
    REQUIRE(titles == std::vector<std::string>{
      "MOBILE", "TABLET", "WEB/DESKTOP", "SOCIAL MEDIA", "WATCH"});
    REQUIRE(counts == std::vector<std::size_t>{11, 8, 3, 7, 5});
    auto& phone = presets[0].m_presets[0];
    REQUIRE(phone.m_name == "iPhone 14 Pro Max");
    REQUIRE(phone.m_size == Size(430, 932));
    auto& web = presets[2].m_presets[0];
    REQUIRE(web.m_name == "Web 1920");
    REQUIRE(web.m_size == Size(1920, 1080));
    auto& story = presets[3].m_presets[0];
    REQUIRE(story.m_name == "Instagram Story");
    REQUIRE(story.m_size == Size(1080, 1920));
    REQUIRE(presets[1].m_device == ArtboardDevice::TABLET);
    REQUIRE(get_last_used_preset().m_name == "iPhone 14 Pro");
    REQUIRE(get_last_used_preset().m_size == Size(393, 852));
    set_last_used_preset(web);
    REQUIRE(get_last_used_preset().m_name == "Web 1920");
    set_last_used_preset(presets[0].m_presets[1]);
  }

  TEST_CASE("click") {
    auto document = Document();
    auto history = History(document);
    auto phone = get_artboard_presets()[0].m_presets[1];
    auto command = require_undoable(history,
      std::make_unique<CreateArtboardCommand>(
        phone, Point(10, 10), Rect(0, 0, 1000, 800)));
    auto& node = document.get(command->get_selection()->front());
    REQUIRE(node.m_header.m_name == "iPhone 14 Pro \xE2\x80\x93 1");
    REQUIRE(node.get_artboard()->m_frame == Rect(304, -26, 393, 852));
    REQUIRE(node.get_artboard()->m_viewport_height == 852);
  }

  TEST_CASE("create") {
    auto document = Document();
    auto history = History(document);
    auto web = get_artboard_presets()[2].m_presets[0];
    auto view = Rect(0, 0, 1600, 900);
    auto command = require_undoable(history,
      std::make_unique<CreateArtboardCommand>(
        web, std::vector<NodeId>(), view));
    REQUIRE(history.get_undo_label() == "Create Artboard");
    auto first = static_cast<const CreateArtboardCommand&>(*command).
      get_artboard();
    REQUIRE(command->get_selection() == std::vector{first});
    auto& node = document.get(first);
    REQUIRE(node.m_header.m_name == "Web 1920 \xE2\x80\x93 1");
    REQUIRE(!node.m_header.m_constraints.m_is_enabled);
    auto& artboard = *node.get_artboard();
    REQUIRE(artboard.m_frame == Rect(-160, -90, 1920, 1080));
    REQUIRE(artboard.m_viewport_height == 1080);
    REQUIRE(artboard.m_background ==
      Fill(SolidFill(make_color(255, 255, 255))));
    REQUIRE(!artboard.m_layout_grid);
    auto second = history.execute(std::make_unique<CreateArtboardCommand>(
      web, std::vector{first}, view));
    auto& next = document.get(second->get_selection()->front());
    REQUIRE(next.m_header.m_name == "Web 1920 \xE2\x80\x93 2");
    REQUIRE(next.get_artboard()->m_frame == Rect(1830, -90, 1920, 1080));
    REQUIRE(document.get_canvas() ==
      std::vector{first, next.get_id()});
  }
}

#include <string>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Commands/ArtboardPlacement.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_artboard(Document& document, std::string name, Rect frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    auto transaction = Transaction(document);
    auto id =
      transaction.append(NodeId(), make_header(std::move(name)), artboard);
    transaction.commit();
    return id;
  }
}

TEST_SUITE("ArtboardPlacement") {
  TEST_CASE("artboard_name") {
    auto document = Document();
    add_artboard(document, "Web 1920 \xE2\x80\x93 1", Rect(0, 0, 10, 10));
    add_artboard(document, "Web 1920 \xE2\x80\x93 3", Rect(0, 0, 10, 10));
    add_artboard(document, "Home", Rect(0, 0, 10, 10));
    REQUIRE(make_artboard_name(document, "Web 1920", false) ==
      "Web 1920 \xE2\x80\x93 2");
    REQUIRE(make_artboard_name(document, "Web 1920 \xE2\x80\x93 3", false) ==
      "Web 1920 \xE2\x80\x93 4");
    REQUIRE(make_artboard_name(document, "Home", true) ==
      "Home \xE2\x80\x93 1");
    REQUIRE(make_artboard_name(document, "About", true) == "About");
    REQUIRE(make_artboard_name(document, "About", false) ==
      "About \xE2\x80\x93 1");
    REQUIRE(make_artboard_name(document, "Step\xE2\x80\x93" "7", false) ==
      "Step \xE2\x80\x93 7");
  }

  TEST_CASE("centered") {
    auto document = Document();
    REQUIRE(place_artboards(document, {}, Rect(0, 0, 1000, 800),
      Rect(0, 0, 375, 812)) == Rect(313, -6, 375, 812));
    add_artboard(document, "Away", Rect(300, 500, 100, 100));
    REQUIRE(place_artboards(document, {}, Rect(2000, 0, 1000, 2000),
      Rect(0, 0, 100, 100)) == Rect(2450, 950, 100, 100));
  }

  TEST_CASE("row") {
    auto document = Document();
    auto first = add_artboard(document, "First", Rect(0, 200, 100, 100));
    add_artboard(document, "Second", Rect(170, 200, 100, 100));
    add_artboard(document, "Top", Rect(500, 0, 100, 100));
    auto view = Rect(0, 0, 1000, 1000);
    auto bounds = Rect(0, 0, 100, 100);
    REQUIRE(place_artboards(document, {first}, view, bounds) ==
      Rect(340, 200, 100, 100));
    REQUIRE(place_artboards(document, {}, view, bounds) ==
      Rect(670, 0, 100, 100));
    REQUIRE(place_artboards(document, {first}, Rect(0, 150, 50, 100),
      bounds) == Rect(0, 390, 100, 100));
  }

  TEST_CASE("next_row") {
    auto document = Document();
    auto first = add_artboard(document, "First", Rect(0, 0, 100, 100));
    add_artboard(document, "Second", Rect(170, 0, 100, 100));
    add_artboard(document, "Third", Rect(340, 0, 100, 100));
    REQUIRE(place_artboards(document, {first}, Rect(0, 0, 300, 300),
      Rect(0, 0, 100, 100)) == Rect(0, 190, 100, 100));
  }

  TEST_CASE("clicked") {
    auto document = Document();
    auto view = Rect(0, 0, 1000, 800);
    auto size = Size(393, 852);
    REQUIRE(place_artboard_at(document, size, Point(10, 10), view) ==
      Rect(304, -26, 393, 852));
    add_artboard(document, "First", Rect(100, 100, 300, 200));
    REQUIRE(place_artboard_at(document, size, Point(800, 700), view) ==
      Rect(100, 390, 393, 852));
    REQUIRE(place_artboard_at(document, size, Point(200, 50), view) ==
      Rect(100, -842, 393, 852));
    REQUIRE(place_artboard_at(document, size, Point(450, 200), view) ==
      Rect(470, 100, 393, 852));
    REQUIRE(place_artboard_at(document, size, Point(120, 200), view) ==
      Rect(-363, 100, 393, 852));
  }

  TEST_CASE("artboard_index") {
    auto document = Document();
    REQUIRE(get_artboard_index(document) == 0);
    build_scene(document);
    REQUIRE(get_artboard_index(document) == 1);
    add_artboard(document, "Last", Rect(0, 0, 10, 10));
    REQUIRE(get_artboard_index(document) == 3);
  }
}

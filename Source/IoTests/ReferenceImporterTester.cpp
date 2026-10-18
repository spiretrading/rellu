#include <doctest/doctest.h>
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  const auto REFERENCE_ID =
    *parse_node_id("225db36e-d0a8-4044-abe7-706c74ffdaa9");
  const auto SOURCE_ID = *parse_node_id("a650ae17-29ed-4167-b3c4-1a7883857f22");
  const auto CHILD_ID = *parse_node_id("1eed2b25-c194-40b8-95c6-ee825a833333");
}

TEST_SUITE("ReferenceImporter") {
  TEST_CASE("reference") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "group",
      "id": "a650ae17-29ed-4167-b3c4-1a7883857f22", "group": {"children": [
      {"type": "shape", "id": "42fb4ec1-f669-4486-99b7-e00bd5bbc69b",
      "shape": {"type": "rect", "x": 0, "y": 0, "width": 1, "height": 1}}]}},
      {"type": "syncRef",
      "syncSourceGuid": "a650ae17-29ed-4167-b3c4-1a7883857f22",
      "guid": "225db36e-d0a8-4044-abe7-706c74ffdaa9",
      "transform": {"a": 1, "b": 0, "c": 0, "d": 1, "tx": 3, "ty": 4},
      "group": {"children": [{"type": "syncRef",
      "syncSourceGuid": "42fb4ec1-f669-4486-99b7-e00bd5bbc69b",
      "guid": "1eed2b25-c194-40b8-95c6-ee825a833333"}]}}])", document);
    REQUIRE(report.m_unconsumed.empty());
    REQUIRE(report.m_warnings.empty());
    auto& node = document.get(REFERENCE_ID);
    REQUIRE(*node.get_reference() ==
      Reference(SOURCE_ID, make_translation(3, 4), true));
    REQUIRE(node.m_header.m_transform.is_identity());
    REQUIRE(node.m_children == std::vector<NodeId>({CHILD_ID}));
    REQUIRE(*document.get(CHILD_ID).get_reference() == Reference(
      *parse_node_id("42fb4ec1-f669-4486-99b7-e00bd5bbc69b"), std::nullopt,
      false));
  }

  TEST_CASE("scroll_settings") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "group",
      "id": "a650ae17-29ed-4167-b3c4-1a7883857f22", "meta": {"ux": {
      "scrollingType": "vertical", "viewportHeight": 221}}},
      {"type": "syncRef",
      "syncSourceGuid": "a650ae17-29ed-4167-b3c4-1a7883857f22",
      "guid": "225db36e-d0a8-4044-abe7-706c74ffdaa9", "meta": {"ux": {
      "scrollingType": "vertical", "viewportHeight": 221}}},
      {"type": "syncRef",
      "syncSourceGuid": "a650ae17-29ed-4167-b3c4-1a7883857f22",
      "guid": "1eed2b25-c194-40b8-95c6-ee825a833333", "meta": {"ux": {
      "scrollingType": "vertical", "viewportHeight": 100}}}])", document);
    REQUIRE(report.m_unconsumed == std::map<std::string, std::size_t,
      std::less<>>({{"<syncRef>.meta.ux.scrollingType:str", 1},
      {"<syncRef>.meta.ux.viewportHeight:number", 1}}));
  }

  TEST_CASE("unresolved_source") {
    auto document = Document();
    auto report = import_nodes(R"([{"type": "syncRef",
      "syncSourceGuid": "a650ae17-29ed-4167-b3c4-1a7883857f22",
      "guid": "225db36e-d0a8-4044-abe7-706c74ffdaa9"},
      {"type": "syncRef", "guid": "1eed2b25-c194-40b8-95c6-ee825a833333"}])",
      document);
    REQUIRE(report.m_warnings.at("Unresolved reference source") == 1);
    REQUIRE(report.m_warnings.at("Reference without a source") == 1);
    REQUIRE(document.get_canvas() == std::vector<NodeId>({REFERENCE_ID}));
  }
}

#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Io/XdError.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  QByteArray make_minimal_package(
      std::vector<std::pair<std::string, QByteArray>> extra) {
    auto entries = std::vector<std::pair<std::string, QByteArray>>();
    entries.emplace_back("manifest", R"({"children": [{"path": "artwork",
      "children": [{"path": "pasteboard"}, {"path": "artboard-b"},
      {"path": "artboard-a"}]}]})");
    entries.emplace_back(
      "resources/graphics/graphicContent.agc", R"({"version": "1.5.0"})");
    entries.emplace_back("artwork/pasteboard/graphics/graphicContent.agc",
      R"({"children": []})");
    entries.insert(entries.end(), extra.begin(), extra.end());
    return make_archive(entries);
  }
}

TEST_SUITE("XdPackage") {
  TEST_CASE("entries") {
    auto package = XdPackage(make_minimal_package({
      {"artwork/artboard-c/graphics/graphicContent.agc", "{}"},
      {"artwork/artboard-a/graphics/graphicContent.agc", "{}"},
      {"artwork/artboard-b/graphics/graphicContent.agc", "{}"},
      {"resources/0123456789abcdef0123456789abcdef", "\x89PNG"}}));
    REQUIRE(package.get_entries().size() == 7);
    REQUIRE(package.contains("manifest"));
    REQUIRE(!package.contains("interactions/interactions.json"));
    REQUIRE(package.get_artboard_entries() == std::vector<std::string>({
      "artwork/artboard-b/graphics/graphicContent.agc",
      "artwork/artboard-a/graphics/graphicContent.agc",
      "artwork/artboard-c/graphics/graphicContent.agc"}));
    REQUIRE(package.get_image_entries() == std::vector<std::string>({
      "resources/0123456789abcdef0123456789abcdef"}));
    REQUIRE(package.read("resources/0123456789abcdef0123456789abcdef") ==
      QByteArray("\x89PNG"));
  }

  TEST_CASE("json_entries") {
    auto package = XdPackage(make_minimal_package({}));
    REQUIRE(package.get_manifest().contains("children"));
    REQUIRE(package.read_resources().value("version").toString() == "1.5.0");
    REQUIRE(package.read_pasteboard().value("children").isArray());
    REQUIRE(package.read_interactions().isEmpty());
  }

  TEST_CASE("missing_entry") {
    auto package = XdPackage(make_minimal_package({}));
    REQUIRE_THROWS_AS(package.read("missing"), XdError);
    REQUIRE_THROWS_WITH_AS(package.read_json("artwork/missing.agc"),
      "The package is missing the entry artwork/missing.agc.", XdError);
  }

  TEST_CASE("missing_manifest") {
    REQUIRE_THROWS_WITH_AS(XdPackage(make_archive({{"mimetype", "x"}})),
      "The package is missing the entry manifest.", XdError);
  }

  TEST_CASE("invalid_json") {
    auto package = XdPackage(make_minimal_package({
      {"interactions/interactions.json", "{\"version\": "}}));
    REQUIRE_THROWS_AS(package.read_interactions(), XdError);
    auto array = XdPackage(make_minimal_package({
      {"interactions/interactions.json", "[]"}}));
    REQUIRE_THROWS_WITH_AS(array.read_interactions(), "The package's entry "
      "interactions/interactions.json is not a JSON object.", XdError);
  }

  TEST_CASE("corrupt_archive") {
    REQUIRE_THROWS_AS(XdPackage(QByteArray("not a zip archive")), XdError);
    REQUIRE_THROWS_AS(XdPackage(QByteArray(0, 'x')), XdError);
    auto bytes = make_minimal_package({
      {"resources/abc", QByteArray(4096, 'x')}});
    auto position = bytes.indexOf(QByteArray("resources/abc"));
    REQUIRE(position >= 0);
    bytes[position + 14] = static_cast<char>(bytes[position + 14] ^ 0x55);
    bytes[position + 15] = static_cast<char>(bytes[position + 15] ^ 0x55);
    auto package = XdPackage(bytes);
    REQUIRE_THROWS_WITH_AS(package.read("resources/abc"),
      "The package's entry resources/abc is corrupt.", XdError);
  }

  TEST_CASE("missing_file") {
    REQUIRE_THROWS_AS(
      XdPackage(std::filesystem::path("missing/file.xd")), XdError);
  }
}

#include <string>
#include <string_view>
#include <doctest/doctest.h>
#include "Rellu/Io/XdPatch.hpp"
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  XdValue parse_value(std::string_view json) {
    return to_xd_value(parse_json(json));
  }

  XdValue patch(std::string_view original, std::string_view generated,
      std::string_view current, bool is_cache_valid = true) {
    auto difference = make_patch(parse_json(original),
      parse_value(generated), XdStructure::get_node(), "<shape>", nullptr);
    auto value = parse_value(current);
    apply_patch(difference.m_patch, value, is_cache_valid);
    return value;
  }

  bool is_same(const XdValue& value, std::string_view json) {
    return to_json_value(value).toObject() == parse_json(json);
  }
}

TEST_SUITE("XdPatch") {
  TEST_CASE("equal_values") {
    auto difference = make_patch(parse_json(R"({"a":1,"b":{"c":"x"}})"),
      parse_value(R"({"b":{"c":"x"},"a":1})"), XdStructure::get_none(), "",
      nullptr);
    REQUIRE(difference.m_patch.empty());
    REQUIRE(!difference.m_has_cache);
  }

  TEST_CASE("restoration") {
    auto original = R"({"a":1,"b":{"c":"x","d":[1,2]},"e":true,"f":[{"g":1}]})";
    auto generated = R"({"a":2,"b":{"c":"x"},"h":3,"f":[{}]})";
    REQUIRE(is_same(patch(original, generated, generated), original));
  }

  TEST_CASE("guards") {
    auto original = R"({"a":1,"b":{"x":1}})";
    auto generated = R"({"a":2,"c":3})";
    REQUIRE(is_same(patch(original, generated, R"({"a":5,"c":4})"),
      R"({"a":5,"c":4,"b":{"x":1}})"));
  }

  TEST_CASE("modelled_members") {
    auto original = R"({"type":"shape","opacity":1,"extra":true})";
    auto generated = R"({"type":"shape"})";
    REQUIRE(is_same(patch(original, generated, R"({"type":"shape",
      "opacity":0.5})"), R"({"type":"shape","opacity":0.5,"extra":true})"));
  }

  TEST_CASE("discriminator") {
    auto original =
      R"({"type":"pattern","pattern":{"href":"a.png","width":1}})";
    auto generated = R"({"type":"pattern","pattern":{"width":1}})";
    REQUIRE(is_same(patch(original, generated, R"({"type":"solid",
      "pattern":{"width":1}})"), R"({"type":"solid","pattern":{"width":1}})"));
    REQUIRE(is_same(patch(original, generated, generated), original));
  }

  TEST_CASE("cache") {
    auto original = R"({"shape":{"x":1},"visualBounds":{"x":0}})";
    auto generated = R"({"shape":{"x":1}})";
    auto difference = make_patch(parse_json(original),
      parse_value(generated), XdStructure::get_node(), "<shape>", nullptr);
    REQUIRE(difference.m_has_cache);
    REQUIRE(is_same(patch(original, generated, generated, false),
      generated));
    REQUIRE(is_same(patch(original, generated, generated, true), original));
  }

  TEST_CASE("structure") {
    auto original = R"({"group":{"children":[{"id":"x"}],"k":1}})";
    auto generated = R"({"group":{"children":[null]}})";
    auto value = patch(original, generated, R"({"group":{"children":[]}})");
    REQUIRE(is_same(value, R"({"group":{"children":[],"k":1}})"));
  }

  TEST_CASE("arrays_by_id") {
    auto original = R"({"list":[{"id":"b","v":2,"u":1},{"id":"x","v":9},
      {"id":"a","v":1}]})";
    auto generated = R"({"list":[{"id":"a","v":1},{"id":"b","v":2},
      {"id":"c","v":3}]})";
    REQUIRE(is_same(patch(original, generated, generated), original));
    REQUIRE(is_same(patch(original, generated, R"({"list":[{"id":"a","v":1},
      {"id":"b","v":5},{"id":"c","v":4}]})"), R"({"list":[{"id":"b","v":5,
      "u":1},{"id":"x","v":9},{"id":"a","v":1},{"id":"c","v":4}]})"));
  }

  TEST_CASE("arrays_by_index") {
    auto original = R"({"list":[{"v":1,"w":0},{"v":2}]})";
    auto generated = R"({"list":[{"v":1},{"v":2}]})";
    REQUIRE(is_same(patch(original, generated, generated), original));
    REQUIRE(is_same(patch(original, generated, R"({"list":[{"v":1}]})"),
      R"({"list":[{"v":1}]})"));
  }

  TEST_CASE("counts") {
    auto counts = XdPatchCounts();
    make_patch(parse_json(R"({"meta":{"ux":{"hasCustomName":true}},"a":1})"),
      parse_value(R"({"meta":{"ux":{}},"a":2,"b":3})"),
      XdStructure::get_node(), "<shape>", &counts);
    REQUIRE(counts.size() == 3);
    REQUIRE(counts.at("add <shape>.meta.ux.hasCustomName") == 1);
    REQUIRE(counts.at("replace <shape>.a") == 1);
    REQUIRE(counts.at("remove <shape>.b") == 1);
  }

  TEST_CASE("value_encoding") {
    auto json = parse_json(
      R"({"a":-3,"b":0.1,"c":"text","d":[true,false,null],"e":{}})");
    auto encoding = std::string();
    encode_value(json, encoding);
    auto view = std::string_view(encoding);
    REQUIRE(to_json_value(decode_value(view)).toObject() == json);
    REQUIRE(view.empty());
  }
}

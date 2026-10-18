#include <string>
#include <doctest/doctest.h>
#include "Rellu/Io/XdValue.hpp"
#include "XdTestPackage.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  std::string format(double value) {
    auto out = std::string();
    append_number(value, out);
    return out;
  }
}

TEST_SUITE("XdValue") {
  TEST_CASE("numbers") {
    REQUIRE(format(0) == "0");
    REQUIRE(format(-0.0) == "0");
    REQUIRE(format(245) == "245");
    REQUIRE(format(-12) == "-12");
    REQUIRE(format(4278190080) == "4278190080");
    REQUIRE(format(0.5) == "0.5");
    REQUIRE(format(0.30000001192092896) == "0.30000001192092896");
    REQUIRE(format(1e21) == "1e+21");
    REQUIRE(format(123456789012345680000.0) == "123456789012345680000");
    REQUIRE(format(0.000001) == "0.000001");
    REQUIRE(format(1.2e-7) == "1.2e-7");
    REQUIRE(format(-5.960464477539062e-08) == "-5.960464477539062e-8");
  }

  TEST_CASE("serialization") {
    auto value = XdValue::make_object();
    value.set("type", "text");
    value.set("rawText", "a\"b\\c\nd\x01");
    value.set("visible", false);
    value.set("nothing", XdValue());
    auto& list = value.set("list", XdValue::make_array());
    list.append(1);
    list.append(2.5);
    REQUIRE(to_json(value) == R"({"type":"text","rawText":"a\"b\\c\nd\u0001",)"
      R"("visible":false,"nothing":null,"list":[1,2.5]})");
  }

  TEST_CASE("members") {
    auto value = XdValue::make_object();
    value.set("a", 1);
    value.set("b", 2);
    value.set("a", 3);
    REQUIRE(value.get_members().size() == 2);
    REQUIRE(value.get_members().front().m_key == "a");
    REQUIRE(value.find("a")->to_number() == 3);
    REQUIRE(value.erase("a"));
    REQUIRE(!value.erase("a"));
    REQUIRE(!value.find("a"));
  }

  TEST_CASE("conversion") {
    auto json = parse_json(R"({"a":[1,"x",true,null],"b":{"c":0.25}})");
    auto value = to_xd_value(json);
    REQUIRE(value.find("a")->get_elements().size() == 4);
    REQUIRE(value.find("b")->find("c")->to_number() == 0.25);
    REQUIRE(to_json_value(value).toObject() == json);
  }

  TEST_CASE("hash") {
    auto left = XdValue::make_object();
    left.set("x", 1);
    auto right = XdValue::make_object();
    right.set("x", 1);
    REQUIRE(hash_value(left) == hash_value(right));
    right.set("x", 2);
    REQUIRE(hash_value(left) != hash_value(right));
    REQUIRE(hash_value(XdValue(0.0)) == hash_value(XdValue(-0.0)));
    REQUIRE(hash_value(XdValue("1")) != hash_value(XdValue(1)));
  }
}

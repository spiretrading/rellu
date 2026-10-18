#include <map>
#include <string>
#include <doctest/doctest.h>
#include "Rellu/Io/XdObject.hpp"
#include "XdTestPackage.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  using Counts = std::map<std::string, std::size_t, std::less<>>;
}

TEST_SUITE("XdObject") {
  TEST_CASE("unconsumed_scalars") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"a": 1, "b": "text",
        "c": true, "d": null, "e": [1, 2], "f": {"g": 1}})"), "<shape>",
        report);
    }
    REQUIRE(report.m_unconsumed == Counts({{"<shape>.a:number", 1},
      {"<shape>.b:str", 1}, {"<shape>.c:bool", 1}, {"<shape>.d:null", 1},
      {"<shape>.e[]", 1}, {"<shape>.f", 1}}));
  }

  TEST_CASE("accessors") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"number": 2.5, "flag": false,
        "name": "label", "wrong": "3"})"), "agc", report);
      REQUIRE(object.contains("number"_L1));
      REQUIRE(object.get_number("number"_L1) == 2.5);
      REQUIRE(object.get_number("missing"_L1, 7) == 7);
      auto flag = object.get_bool("flag"_L1);
      REQUIRE(flag);
      REQUIRE(!*flag);
      REQUIRE(object.get_string("name"_L1) == QString("label"));
      REQUIRE(!object.get_number("wrong"_L1));
    }
    REQUIRE(report.m_unconsumed == Counts({{"agc.wrong:str", 1}}));
  }

  TEST_CASE("nested_objects") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"meta": {"ux": {"x": 1, "y": 2,
        "z": {"w": 3}}}, "style": {"fill": {"type": "solid"}}})"), "<group>",
        report);
      auto ux = object.get_object("meta"_L1)->get_object("ux"_L1);
      REQUIRE(ux->get_number("x"_L1) == 1);
      REQUIRE(object.get_object("meta"_L1)->get_object("ux"_L1) == ux);
      object.get_object("style"_L1)->get_object("fill"_L1);
    }
    REQUIRE(report.m_unconsumed == Counts({{"<group>.meta.ux.y:number", 1},
      {"<group>.meta.ux.z", 1}, {"<group>.style.fill.type:str", 1}}));
  }

  TEST_CASE("arrays") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"lines": [[{"from": 0, "x": 1}],
        [{"from": 1}]], "stops": [{"offset": 0, "color": 1}, 5]})"),
        "<text>", report);
      auto lines = object.get_array("lines"_L1);
      REQUIRE(lines->get_size() == 2);
      lines->get_array(0)->get_object(0)->get_number("from"_L1);
      lines->get_array(1)->get_object(0)->get_number("from"_L1);
      auto stops = object.get_array("stops"_L1);
      stops->get_object(0)->get_number("offset"_L1);
      REQUIRE(!stops->get_object(1));
      REQUIRE(!stops->get_object(2));
    }
    REQUIRE(report.m_unconsumed == Counts({{"<text>.lines[][].x:number", 1},
      {"<text>.stops[].color:number", 1}}));
  }

  TEST_CASE("identifier_keys") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"syncMap": {
        "6aa68cb5-302d-4233-a3a8-6dd498552b5a": "x",
        "1EED2B25-C194-40B8-95C6-EE825A833333": "y"}, "artboard-1": 1})"),
        "<group>", report);
      object.get_object("syncMap"_L1);
    }
    REQUIRE(report.m_unconsumed == Counts({{"<group>.artboard-1:number", 1},
      {"<group>.syncMap.<id>:str", 2}}));
  }

  TEST_CASE("ignore") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"a": 1, "b": {"c": 2}, "d": 3})"),
        "agc", report);
      object.ignore({"a"_L1, "b"_L1, "missing"_L1});
    }
    REQUIRE(report.m_unconsumed == Counts({{"agc.d:number", 1}}));
  }

  TEST_CASE("consume_all") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"a": 1, "b": {"c": 2}})"), "agc",
        report);
      object.get_object("b"_L1);
      object.consume_all();
    }
    REQUIRE(report.m_unconsumed == Counts({{"agc.b.c:number", 1}}));
  }

  TEST_CASE("null_object") {
    auto report = ImportReport();
    {
      auto object = XdObject(parse_json(R"({"symbolOverrides": null,
        "text": "x"})"), "<group>", report);
      REQUIRE(!object.get_object("symbolOverrides"_L1));
      REQUIRE(!object.get_object("text"_L1));
    }
    REQUIRE(report.m_unconsumed == Counts({{"<group>.text:str", 1}}));
  }

  TEST_CASE("many_keys") {
    auto json = QJsonObject();
    for(auto i = 0; i != 100; ++i) {
      json.insert(QString("k%1").arg(i, 3, 10, QChar('0')), i);
    }
    auto report = ImportReport();
    {
      auto object = XdObject(json, "agc", report);
      for(auto i = 0; i != 100; ++i) {
        if(i != 3 && i != 90) {
          object.consume(QString("k%1").arg(i, 3, 10, QChar('0')));
        }
      }
    }
    REQUIRE(report.m_unconsumed == Counts({{"agc.k003:number", 1},
      {"agc.k090:number", 1}}));
  }
}

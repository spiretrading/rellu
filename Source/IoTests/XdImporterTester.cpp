#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Io/XdImporter.hpp"
#include "Rellu/Io/XdValues.hpp"
#include "XdTestPackage.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  XdImporter make_group_importer(std::vector<std::string>& events) {
    auto importer = XdImporter();
    importer.set_node_importer("group"_L1, [&] (XdNode& node) {
      events.push_back("node " + node.m_type.toStdString());
      node.m_header.m_id =
        to_node_id(*node.m_json->get_string("id"_L1));
      if(auto group = node.m_json->get_object("group"_L1)) {
        node.m_children = group->get_array("children"_L1);
      }
      node.m_payload = Group();
    });
    importer.add_property_importer([&] (XdNode& node) {
      events.push_back("property " + node.m_header.m_name);
    });
    importer.add_property_importer([&] (XdNode& node) {
      if(auto name = node.m_json->get_string("name"_L1)) {
        node.m_header.m_name = name->toStdString();
      }
    });
    importer.add_finisher([&] (XdNode& node) {
      events.push_back("finish " + node.m_header.m_name + " " +
        (node.m_is_inserted ? "inserted" : "pending"));
    });
    return importer;
  }
}

TEST_SUITE("XdImporter") {
  TEST_CASE("node_order") {
    auto events = std::vector<std::string>();
    auto importer = make_group_importer(events);
    importer.add_document_importer(XdPhase::CANVAS, [&] (XdImport& import) {
      events.push_back("canvas");
      auto children = import.get_pasteboard().get_array("children"_L1);
      for(auto child : children->get_json()) {
        auto json = import.make_node_json(child.toObject());
        import.import_node(*json, nullptr, XdNodeRole::NODE);
      }
    });
    importer.add_document_importer(XdPhase::RESOURCES, [&] (XdImport&) {
      events.push_back("resources");
    });
    importer.add_document_importer(XdPhase::LINKS, [&] (XdImport&) {
      events.push_back("links");
    });
    auto package = XdTestPackage();
    package.m_pasteboard = parse_json_array(R"([{"type": "group",
      "id": "00000000-0000-0000-0000-000000000001", "name": "outer",
      "group": {"children": [{"type": "group",
      "id": "00000000-0000-0000-0000-000000000002", "name": "inner"}]}}])");
    auto document = Document();
    importer.import(XdPackage(package.build()), document);
    REQUIRE(events == std::vector<std::string>({"resources", "canvas",
      "node group", "property ", "node group", "property ",
      "finish inner inserted", "finish outer inserted", "links"}));
    auto outer = *parse_node_id("00000000-0000-0000-0000-000000000001");
    auto inner = *parse_node_id("00000000-0000-0000-0000-000000000002");
    REQUIRE(document.get_canvas() == std::vector<NodeId>({outer}));
    REQUIRE(document.get_children(outer) == std::vector<NodeId>({inner}));
    REQUIRE(document.get(inner).m_header.m_name == "inner");
    REQUIRE(!document.can_undo());
  }

  TEST_CASE("unsupported_type") {
    auto events = std::vector<std::string>();
    auto importer = make_group_importer(events);
    importer.add_document_importer(XdPhase::CANVAS, [&] (XdImport& import) {
      auto children = import.get_pasteboard().get_array("children"_L1);
      for(auto child : children->get_json()) {
        auto json = import.make_node_json(child.toObject());
        REQUIRE(import.import_node(*json, nullptr, XdNodeRole::NODE).
          is_nil());
      }
    });
    auto package = XdTestPackage();
    package.m_pasteboard =
      parse_json_array(R"([{"type": "widget", "size": 3}, {"name": "x"}])");
    auto document = Document();
    auto report = importer.import(XdPackage(package.build()), document);
    REQUIRE(report.m_warnings.at("Unsupported node type widget") == 1);
    REQUIRE(report.m_warnings.at("Node without a type") == 1);
    REQUIRE(report.m_unconsumed.at("<widget>.type:str") == 1);
    REQUIRE(report.m_unconsumed.at("<widget>.size:number") == 1);
    REQUIRE(report.m_unconsumed.at("<?>.name:str") == 1);
    REQUIRE(report.m_unconsumed.at("agc.version:str") == 2);
    REQUIRE(document.get_canvas().empty());
  }

  TEST_CASE("states") {
    auto states = XdStates();
    REQUIRE(!states.find<int>());
    states.get<int>() = 5;
    states.get<std::string>() = "text";
    REQUIRE(*states.find<int>() == 5);
    REQUIRE(states.get<std::string>() == "text");
  }

  TEST_CASE("failure") {
    auto importer = XdImporter();
    importer.add_document_importer(XdPhase::CANVAS, [] (XdImport& import) {
      import.get_transaction().append(NodeId(), NodeHeader(), Group());
      throw std::runtime_error("failure");
    });
    auto document = Document();
    REQUIRE_THROWS_AS(importer.import(
      XdPackage(XdTestPackage().build()), document), std::runtime_error);
    REQUIRE(document.get_canvas().empty());
  }
}

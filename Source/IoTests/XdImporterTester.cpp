#include <string>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Io/XdImporter.hpp"
#include "Rellu/Io/XdValues.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
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

  struct Counter {
    int m_value = 0;

    Counter fork() const {
      return Counter();
    }

    void merge(Counter fork) {
      m_value += fork.m_value;
    }
  };

  XdImporter make_canvas_importer(bool is_parallel) {
    auto importer = XdImporter();
    importer.set_node_importer("group"_L1, [] (XdNode& node) {
      if(auto id = node.m_json->get_string("id"_L1)) {
        node.m_header.m_id = to_node_id(*id);
      }
      if(auto name = node.m_json->get_string("name"_L1)) {
        node.m_header.m_name = name->toStdString();
      }
      if(auto group = node.m_json->get_object("group"_L1)) {
        node.m_children = group->get_array("children"_L1);
      }
      node.m_payload = Group();
    });
    importer.add_finisher([] (XdNode& node) {
      ++node.m_import->get_states().get<Counter>().m_value;
    });
    importer.add_document_importer(XdPhase::CANVAS,
      [=] (XdImport& import) {
        auto nodes = std::vector<QJsonObject>();
        auto children = import.get_pasteboard().get_array("children"_L1);
        for(auto child : children->get_json()) {
          nodes.push_back(child.toObject());
        }
        if(is_parallel) {
          import.import_nodes(nodes, XdNodeRole::NODE);
        } else {
          for(auto& node : nodes) {
            auto json = import.make_node_json(node);
            import.import_node(*json, nullptr, XdNodeRole::NODE);
          }
        }
        import.warn("finished " +
          std::to_string(import.get_states().get<Counter>().m_value));
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

  TEST_CASE("parallel_nodes") {
    auto package = XdTestPackage();
    package.m_pasteboard = parse_json_array(R"([{"type": "group",
      "id": "00000000-0000-0000-0000-000000000001", "name": "a", "size": 1,
      "group": {"children": [{"type": "group",
      "id": "00000000-0000-0000-0000-000000000002", "name": "b"},
      {"type": "widget"}]}}, {"type": "group",
      "id": "00000000-0000-0000-0000-000000000003", "name": "c"},
      {"type": "group", "id": "00000000-0000-0000-0000-000000000002",
      "name": "copy", "group": {"children": [{"type": "group",
      "id": "00000000-0000-0000-0000-000000000005"}]}}, {"type": "group",
      "id": "00000000-0000-0000-0000-000000000004", "name": "d",
      "group": {"children": [{"type": "group",
      "id": "00000000-0000-0000-0000-000000000004"}]}}])");
    auto bytes = package.build();
    auto sequential = Document();
    auto sequential_report =
      make_canvas_importer(false).import(XdPackage(bytes), sequential);
    auto parallel = Document();
    auto parallel_report =
      make_canvas_importer(true).import(XdPackage(bytes), parallel);
    REQUIRE(find_differences(sequential, parallel).empty());
    REQUIRE(parallel_report.m_warnings == sequential_report.m_warnings);
    REQUIRE(parallel_report.m_unconsumed == sequential_report.m_unconsumed);
    REQUIRE(parallel_report.m_node_count == 4);
    REQUIRE(parallel.get_canvas() == std::vector<NodeId>({
      *parse_node_id("00000000-0000-0000-0000-000000000001"),
      *parse_node_id("00000000-0000-0000-0000-000000000003"),
      *parse_node_id("00000000-0000-0000-0000-000000000004")}));
    REQUIRE(parallel_report.m_warnings.at("finished 4") == 1);
    REQUIRE(
      parallel_report.m_warnings.at("Node not inserted: Duplicate node "
        "(group)") == 1);
    REQUIRE(
      parallel_report.m_warnings.at("Node not inserted: Duplicate node "
        "(group in group)") == 1);
    REQUIRE(parallel_report.m_unconsumed.at("<group>.size:number") == 1);
  }

  TEST_CASE("states") {
    auto states = XdStates();
    REQUIRE(!states.find<int>());
    states.get<int>() = 5;
    states.get<std::string>() = "text";
    REQUIRE(*states.find<int>() == 5);
    REQUIRE(states.get<std::string>() == "text");
  }

  TEST_CASE("state_fork") {
    auto states = XdStates();
    states.get<Counter>().m_value = 1;
    states.get<int>() = 5;
    auto fork = states.fork();
    REQUIRE(fork.get<Counter>().m_value == 0);
    REQUIRE(fork.find<int>() == states.find<int>());
    fork.get<Counter>().m_value = 2;
    fork.get<std::string>() = "text";
    states.merge(std::move(fork));
    REQUIRE(states.get<Counter>().m_value == 3);
    REQUIRE(states.get<std::string>() == "text");
    REQUIRE(*states.find<int>() == 5);
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

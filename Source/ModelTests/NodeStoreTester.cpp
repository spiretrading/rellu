#include <memory>
#include <set>
#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Model/NodeStore.hpp"

using namespace Rellu;

namespace {
  Node make_node(const std::string& name) {
    auto node = Node();
    node.m_header.m_id = generate_node_id();
    node.m_header.m_name = name;
    node.m_payload = Group();
    return node;
  }
}

TEST_SUITE("NodeStore") {
  TEST_CASE("insert") {
    auto store = NodeStore();
    auto node = make_node("First");
    store.insert(node);
    REQUIRE(store.get_size() == 1);
    REQUIRE(store.contains(node.get_id()));
    REQUIRE(*store.find(node.get_id()) == node);
    REQUIRE(store.get(node.get_id()) == node);
    REQUIRE(*store.get_record(node.get_id()) == node);
    REQUIRE_THROWS_AS(store.insert(node), std::invalid_argument);
    auto unknown = generate_node_id();
    REQUIRE(!store.contains(unknown));
    REQUIRE(!store.find(unknown));
    REQUIRE(!store.get_record(unknown));
    REQUIRE_THROWS_AS(store.get(unknown), std::out_of_range);
    REQUIRE_THROWS_AS(store.edit(unknown), std::out_of_range);
  }

  TEST_CASE("erase") {
    auto store = NodeStore();
    auto first = make_node("First");
    auto second = make_node("Second");
    store.insert(first);
    store.insert(second);
    auto record = store.erase(first.get_id());
    REQUIRE(*record == first);
    REQUIRE(store.get_size() == 1);
    REQUIRE(!store.contains(first.get_id()));
    REQUIRE(!store.erase(first.get_id()));
    auto third = make_node("Third");
    store.insert(third);
    REQUIRE(store.get_size() == 2);
    REQUIRE(store.get(third.get_id()) == third);
    REQUIRE(store.get(second.get_id()) == second);
  }

  TEST_CASE("set") {
    auto store = NodeStore();
    auto node = make_node("Node");
    store.insert(node);
    auto replacement = node;
    replacement.m_header.m_name = "Replacement";
    store.set(std::make_shared<const Node>(replacement));
    REQUIRE(store.get_size() == 1);
    REQUIRE(store.get(node.get_id()).m_header.m_name == "Replacement");
    auto other = make_node("Other");
    store.set(std::make_shared<const Node>(other));
    REQUIRE(store.get_size() == 2);
    REQUIRE(store.get(other.get_id()) == other);
  }

  TEST_CASE("edit") {
    auto store = NodeStore();
    auto node = make_node("Node");
    store.insert(node);
    auto& edited = store.edit(node.get_id());
    edited.m_header.m_name = "Edited";
    REQUIRE(store.get(node.get_id()).m_header.m_name == "Edited");
    auto record = store.get_record(node.get_id());
    store.edit(node.get_id()).m_header.m_name = "Again";
    REQUIRE(record->m_header.m_name == "Edited");
    REQUIRE(store.get(node.get_id()).m_header.m_name == "Again");
    auto shared = std::make_shared<const Node>(node);
    store.set(shared);
    shared.reset();
    store.edit(node.get_id()).m_header.m_name = "Copied";
    REQUIRE(store.get(node.get_id()).m_header.m_name == "Copied");
  }

  TEST_CASE("for_each") {
    auto store = NodeStore();
    auto ids = std::set<NodeId>();
    for(auto i = 0; i != 10; ++i) {
      auto node = make_node("Node");
      ids.insert(node.get_id());
      store.insert(node);
    }
    store.erase(*ids.begin());
    ids.erase(ids.begin());
    auto visited = std::set<NodeId>();
    store.for_each([&] (const auto& node) {
      visited.insert(node.get_id());
    });
    REQUIRE(visited == ids);
  }
}

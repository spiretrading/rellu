#include <sstream>
#include <unordered_set>
#include <doctest/doctest.h>
#include "Rellu/Model/NodeId.hpp"

using namespace Rellu;

TEST_SUITE("NodeId") {
  TEST_CASE("nil") {
    auto id = NodeId();
    REQUIRE(id.is_nil());
    REQUIRE(to_string(id) == "00000000-0000-0000-0000-000000000000");
    REQUIRE(!generate_node_id().is_nil());
  }

  TEST_CASE("parse") {
    auto id = parse_node_id("0123abcd-4567-89EF-a0b1-c2d3e4f5a6b7");
    REQUIRE(id);
    REQUIRE(id->get_bytes()[0] == 0x01);
    REQUIRE(id->get_bytes()[3] == 0xcd);
    REQUIRE(id->get_bytes()[7] == 0xef);
    REQUIRE(id->get_bytes()[15] == 0xb7);
    REQUIRE(to_string(*id) == "0123abcd-4567-89ef-a0b1-c2d3e4f5a6b7");
    REQUIRE(parse_node_id("0123abcd456789efa0b1c2d3e4f5a6b7") == id);
  }

  TEST_CASE("malformed") {
    REQUIRE(!parse_node_id(""));
    REQUIRE(!parse_node_id("0123abcd-4567-89ef-a0b1-c2d3e4f5a6b"));
    REQUIRE(!parse_node_id("0123abcd-4567-89ef-a0b1-c2d3e4f5a6b7a"));
    REQUIRE(!parse_node_id("0123abcd+4567-89ef-a0b1-c2d3e4f5a6b7"));
    REQUIRE(!parse_node_id("0123abcd-4567-89ef-a0b1-c2d3e4f5a6bg"));
    REQUIRE(!parse_node_id("0123abcd456789efa0b1c2d3e4f5a6b-"));
  }

  TEST_CASE("round_trip") {
    for(auto i = 0; i != 1000; ++i) {
      auto id = generate_node_id();
      REQUIRE(parse_node_id(to_string(id)) == id);
    }
  }

  TEST_CASE("version") {
    auto id = generate_node_id();
    REQUIRE((id.get_bytes()[6] & 0xF0) == 0x40);
    REQUIRE((id.get_bytes()[8] & 0xC0) == 0x80);
  }

  TEST_CASE("uniqueness") {
    auto ids = std::unordered_set<NodeId>();
    for(auto i = 0; i != 100000; ++i) {
      ids.insert(generate_node_id());
    }
    REQUIRE(ids.size() == 100000);
  }

  TEST_CASE("ordering") {
    auto low = *parse_node_id("00000000-0000-0000-0000-000000000001");
    auto high = *parse_node_id("10000000-0000-0000-0000-000000000000");
    REQUIRE(low < high);
    REQUIRE(NodeId() < low);
    REQUIRE(low == low);
    REQUIRE(low != high);
  }

  TEST_CASE("stream") {
    auto id = *parse_node_id("0123abcd-4567-89ef-a0b1-c2d3e4f5a6b7");
    auto stream = std::ostringstream();
    stream << id;
    REQUIRE(stream.str() == "0123abcd-4567-89ef-a0b1-c2d3e4f5a6b7");
  }
}

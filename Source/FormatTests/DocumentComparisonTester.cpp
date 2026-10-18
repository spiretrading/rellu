#include <array>
#include <cstdint>
#include <string>
#include <doctest/doctest.h>
#include "Rellu/Format/DocumentComparison.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  NodeId make_id(std::uint8_t value) {
    auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
    bytes[0] = 0x5A;
    bytes[NodeId::SIZE - 1] = value;
    return NodeId(bytes);
  }

  NodeHeader make_header(std::uint8_t id, std::string name) {
    auto header = NodeHeader();
    header.m_id = make_id(id);
    header.m_name = std::move(name);
    return header;
  }

  void build(Document& document, std::string name = "Shape",
      double width = 10) {
    auto transaction = Transaction(document);
    auto artboard = transaction.append(NodeId(), make_header(1, "Artboard"),
      Artboard(Rect(0, 0, 100, 100)));
    transaction.append(artboard, make_header(2, name), Shape(RectangleShape(
      Rect(0, 0, width, 10), CornerRadii(0, 0, 0, 0)), Style()));
    transaction.add_swatch(
      ColorSwatch(make_id(3), "Swatch", make_color(1, 2, 3)));
    transaction.commit();
  }

  bool contains(const std::vector<std::string>& differences,
      const std::string& text) {
    for(auto& difference : differences) {
      if(difference.find(text) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
}

TEST_SUITE("DocumentComparison") {
  TEST_CASE("equal_documents") {
    auto left = Document();
    build(left);
    auto right = Document();
    build(right);
    REQUIRE(find_differences(left, right).empty());
    REQUIRE(is_equal(left, right));
    REQUIRE(is_equal(Document(), Document()));
  }

  TEST_CASE("header_difference") {
    auto left = Document();
    build(left);
    auto right = Document();
    build(right, "Renamed");
    auto differences = find_differences(left, right);
    REQUIRE(differences.size() == 1);
    REQUIRE(differences.front() ==
      "node " + to_string(make_id(2)) + ": name differs");
    REQUIRE(!is_equal(left, right));
  }

  TEST_CASE("payload_difference") {
    auto left = Document();
    build(left);
    auto right = Document();
    build(right, "Shape", 20);
    auto differences = find_differences(left, right);
    REQUIRE(differences.size() == 1);
    REQUIRE(contains(differences, "geometry differs"));
  }

  TEST_CASE("missing_node") {
    auto left = Document();
    build(left);
    auto right = Document();
    build(right);
    {
      auto transaction = Transaction(right);
      transaction.remove(make_id(2));
      transaction.commit();
    }
    auto differences = find_differences(left, right);
    REQUIRE(contains(differences, "only in the first document"));
    REQUIRE(contains(differences, "children differs"));
    auto reverse = find_differences(right, left);
    REQUIRE(contains(reverse, "only in the second document"));
  }

  TEST_CASE("resource_difference") {
    auto left = Document();
    build(left);
    auto right = Document();
    build(right);
    {
      auto transaction = Transaction(right);
      transaction.replace_swatch(
        ColorSwatch(make_id(3), "Swatch", make_color(9, 9, 9)));
      transaction.set_home_artboard(make_id(1));
      transaction.commit();
    }
    auto differences = find_differences(left, right);
    REQUIRE(differences.size() == 2);
    REQUIRE(contains(differences, "swatch 0"));
    REQUIRE(contains(differences, "home artboard differs"));
  }

  TEST_CASE("limit") {
    auto left = Document();
    build(left);
    auto right = Document();
    REQUIRE(find_differences(left, right, 2).size() == 2);
    REQUIRE(find_differences(left, right).size() > 2);
  }
}

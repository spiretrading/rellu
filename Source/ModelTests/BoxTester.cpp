#include <optional>
#include <string>
#include <utility>
#include <doctest/doctest.h>
#include "Rellu/Model/Box.hpp"

using namespace Rellu;

TEST_SUITE("Box") {
  TEST_CASE("value") {
    auto box = Box(std::string("hello"));
    REQUIRE(*box == "hello");
    REQUIRE(box->size() == 5);
    REQUIRE(Box<std::string>()->empty());
    REQUIRE(sizeof(Box<std::string>) == sizeof(void*));
  }

  TEST_CASE("edit") {
    auto box = Box(std::string("hello"));
    auto copy = box;
    REQUIRE(&*copy == &*box);
    copy.edit() += " world";
    REQUIRE(*box == "hello");
    REQUIRE(*copy == "hello world");
    auto address = &*copy;
    copy.edit() += "!";
    REQUIRE(&*copy == address);
    box = copy;
    REQUIRE(&*box == &*copy);
  }

  TEST_CASE("equality") {
    auto box = Box(std::string("hello"));
    REQUIRE(box == box);
    REQUIRE(box == Box(std::string("hello")));
    REQUIRE(box != Box(std::string("world")));
  }

  TEST_CASE("optional") {
    auto box = OptionalBox<std::string>();
    REQUIRE(!box);
    REQUIRE(box == std::nullopt);
    REQUIRE(sizeof(OptionalBox<std::string>) == sizeof(void*));
    box = std::string("hello");
    REQUIRE(box);
    REQUIRE(*box == "hello");
    REQUIRE(box->size() == 5);
    box = std::nullopt;
    REQUIRE(!box);
    REQUIRE(OptionalBox(std::string("x")) == OptionalBox(std::string("x")));
    REQUIRE(OptionalBox(std::string("x")) != OptionalBox(std::string("y")));
    REQUIRE(OptionalBox(std::string("x")) != OptionalBox<std::string>());
  }

  TEST_CASE("optional_edit") {
    auto box = OptionalBox(std::string("hello"));
    auto copy = box;
    REQUIRE(&*std::as_const(copy) == &*std::as_const(box));
    *copy += " world";
    REQUIRE(*std::as_const(box) == "hello");
    REQUIRE(*std::as_const(copy) == "hello world");
  }
}

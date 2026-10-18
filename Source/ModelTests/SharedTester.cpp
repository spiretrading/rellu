#include <memory>
#include <string>
#include <doctest/doctest.h>
#include "Rellu/Model/Shared.hpp"

using namespace Rellu;

TEST_SUITE("Shared") {
  TEST_CASE("value") {
    auto shared = Shared(std::string("hello"));
    REQUIRE(*shared == "hello");
    REQUIRE(shared->size() == 5);
    REQUIRE(Shared<std::string>()->empty());
  }

  TEST_CASE("sharing") {
    auto shared = Shared(std::string("hello"));
    auto copy = shared;
    REQUIRE(copy.get_pointer().get() == shared.get_pointer().get());
    auto pointer = std::make_shared<const std::string>("world");
    REQUIRE(Shared(pointer).get_pointer().get() == pointer.get());
  }

  TEST_CASE("equality") {
    auto shared = Shared(std::string("hello"));
    REQUIRE(shared == shared);
    REQUIRE(shared == Shared(std::string("hello")));
    REQUIRE(shared != Shared(std::string("world")));
  }
}

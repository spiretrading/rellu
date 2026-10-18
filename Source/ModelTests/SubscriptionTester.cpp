#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Model/Subscription.hpp"
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_group(Document& document, const std::string& name) {
    auto transaction = Transaction(document);
    auto id = transaction.append(NodeId(), make_header(name), Group());
    transaction.commit();
    return id;
  }
}

TEST_SUITE("Subscription") {
  TEST_CASE("release_function") {
    auto count = 0;
    {
      auto subscription = Subscription([&] { ++count; });
      subscription.release();
      REQUIRE(count == 1);
      subscription.release();
      REQUIRE(count == 1);
    }
    REQUIRE(count == 1);
    {
      auto subscription = Subscription([&] { ++count; });
    }
    REQUIRE(count == 2);
    {
      auto subscription = Subscription([&] { ++count; });
      auto moved = std::move(subscription);
      REQUIRE(count == 2);
      auto other = Subscription([&] { count += 10; });
      other = std::move(moved);
      REQUIRE(count == 12);
    }
    REQUIRE(count == 13);
    auto empty = Subscription();
    empty.release();
  }

  TEST_CASE("order") {
    auto document = Document();
    auto calls = std::vector<std::string>();
    auto first = document.subscribe([&] (const auto& changes) {
      calls.push_back("first " + to_string(changes.m_inserted.front()));
    });
    auto second = document.subscribe([&] (const auto& changes) {
      calls.push_back("second " + to_string(changes.m_inserted.front()));
    });
    auto a = add_group(document, "A");
    auto b = add_group(document, "B");
    REQUIRE(calls == std::vector<std::string>({"first " + to_string(a),
      "second " + to_string(a), "first " + to_string(b),
      "second " + to_string(b)}));
  }

  TEST_CASE("release") {
    auto document = Document();
    auto count = 0;
    auto subscription = document.subscribe([&] (const auto&) {
      ++count;
    });
    add_group(document, "A");
    REQUIRE(count == 1);
    subscription.release();
    add_group(document, "B");
    REQUIRE(count == 1);
    {
      auto scoped = document.subscribe([&] (const auto&) {
        ++count;
      });
      add_group(document, "C");
      REQUIRE(count == 2);
    }
    add_group(document, "D");
    REQUIRE(count == 2);
  }

  TEST_CASE("release_during_publication") {
    auto document = Document();
    auto calls = std::vector<std::string>();
    auto second = Subscription();
    auto first = document.subscribe([&] (const auto&) {
      calls.push_back("first");
      second.release();
    });
    second = document.subscribe([&] (const auto&) {
      calls.push_back("second");
    });
    auto third = Subscription();
    third = document.subscribe([&] (const auto&) {
      calls.push_back("third");
      third.release();
    });
    add_group(document, "A");
    add_group(document, "B");
    REQUIRE(calls ==
      std::vector<std::string>({"first", "third", "first"}));
  }

  TEST_CASE("subscribe_during_publication") {
    auto document = Document();
    auto count = 0;
    auto late = Subscription();
    auto first = document.subscribe([&] (const auto&) {
      if(count == 0) {
        late = document.subscribe([&] (const auto&) {
          count += 10;
        });
      }
      ++count;
    });
    add_group(document, "A");
    REQUIRE(count == 1);
    add_group(document, "B");
    REQUIRE(count == 12);
  }

  TEST_CASE("commit_order") {
    auto document = Document();
    auto sequence = std::vector<std::string>();
    auto first = document.subscribe([&] (const auto& changes) {
      auto& name = document.get(changes.m_inserted.front()).m_header.m_name;
      sequence.push_back("first " + name);
      if(name == "A") {
        add_group(document, "B");
      }
    });
    auto second = document.subscribe([&] (const auto& changes) {
      auto& name = document.get(changes.m_inserted.front()).m_header.m_name;
      sequence.push_back("second " + name);
    });
    add_group(document, "A");
    REQUIRE(sequence == std::vector<std::string>(
      {"first A", "second A", "first B", "second B"}));
  }

  TEST_CASE("outliving_document") {
    auto subscription = Subscription();
    {
      auto document = std::make_unique<Document>();
      subscription = document->subscribe([] (const auto&) {});
    }
    subscription.release();
  }
}

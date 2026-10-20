#include <stdexcept>
#include <doctest/doctest.h>
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  class NamingCommand : public Command {
    public:
      NamingCommand(const NodeId& id, std::string name)
        : m_id(id),
          m_name(std::move(name)) {}

      std::string get_label() const override {
        return "Name";
      }

      void apply(const Document& document, Transaction& transaction) override {
        auto header = document.get(m_id).m_header;
        header.m_name = m_name;
        transaction.replace_header(header);
        if(m_name == "fail") {
          throw std::invalid_argument("Failed.");
        }
      }

      bool merge(const Command& command) override {
        auto naming = dynamic_cast<const NamingCommand*>(&command);
        if(!naming) {
          return false;
        }
        m_name = naming->m_name;
        return true;
      }

    private:
      NodeId m_id;
      std::string m_name;
  };

  std::unique_ptr<Command> make_move(const NodeId& id, Point delta) {
    return std::make_unique<TranslateCommand>(std::vector{id}, delta);
  }

  std::unique_ptr<Command> make_rename(const NodeId& id, std::string name) {
    return std::make_unique<NamingCommand>(id, std::move(name));
  }
}

TEST_SUITE("History") {
  TEST_CASE("labels") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    REQUIRE(!history.can_undo());
    REQUIRE(!history.can_redo());
    REQUIRE(history.get_undo_label().empty());
    REQUIRE(history.get_redo_label().empty());
    REQUIRE(!history.undo());
    REQUIRE(!history.redo());
    REQUIRE(history.execute(make_move(scene.m_rectangle, Point(1, 0))));
    REQUIRE(history.execute(make_rename(scene.m_rectangle, "Box")));
    REQUIRE(history.can_undo());
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.get_undo_label() == "Name");
    REQUIRE(history.undo());
    REQUIRE(history.get_undo_label() == "Move");
    REQUIRE(history.get_redo_label() == "Name");
    REQUIRE(history.can_redo());
    REQUIRE(history.undo());
    REQUIRE(!history.can_undo());
    REQUIRE(history.get_redo_count() == 2);
    REQUIRE(history.get_redo_label() == "Move");
    REQUIRE(history.redo());
    REQUIRE(history.execute(make_move(scene.m_rectangle, Point(1, 0))));
    REQUIRE(!history.can_redo());
    REQUIRE(history.get_undo_count() == 2);
  }

  TEST_CASE("no_change") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto count = 0;
    auto subscription = history.subscribe([&] { ++count; });
    REQUIRE(!history.execute(make_move(scene.m_rectangle, Point(0, 0))));
    REQUIRE(!history.can_undo());
    REQUIRE(count == 0);
    REQUIRE(!history.is_dirty());
  }

  TEST_CASE("dirty") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    REQUIRE(!history.is_dirty());
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.is_dirty());
    history.mark_clean();
    REQUIRE(!history.is_dirty());
    history.execute(make_rename(scene.m_rectangle, "Box"));
    REQUIRE(history.is_dirty());
    history.undo();
    REQUIRE(!history.is_dirty());
    history.undo();
    REQUIRE(history.is_dirty());
    history.redo();
    REQUIRE(!history.is_dirty());
    history.redo();
    REQUIRE(history.is_dirty());
    history.undo();
    history.undo();
    history.execute(make_rename(scene.m_rectangle, "Other"));
    REQUIRE(history.is_dirty());
    history.undo();
    REQUIRE(history.is_dirty());
    REQUIRE(history.can_redo());
  }

  TEST_CASE("merge") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto before = take_snapshot(document);
    auto count = 0;
    auto subscription = document.subscribe([&] (const auto&) {
      ++count;
    });
    for(auto i = 0; i != 10; ++i) {
      REQUIRE(history.merge(make_move(scene.m_rectangle, Point(1, 2))));
    }
    REQUIRE(count == 10);
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Move");
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(120, 80));
    REQUIRE(!document.can_redo());
    REQUIRE(history.undo());
    REQUIRE(take_snapshot(document) == before);
    REQUIRE(history.redo());
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(120, 80));
  }

  TEST_CASE("merge_into_clean") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    history.mark_clean();
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.is_dirty());
    history.undo();
    REQUIRE(history.is_dirty());
    history.redo();
    REQUIRE(history.is_dirty());
  }

  TEST_CASE("merge_back") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    history.execute(make_rename(scene.m_rectangle, "Box"));
    history.seal();
    history.mark_clean();
    auto clean = take_snapshot(document);
    REQUIRE(history.merge(make_move(scene.m_rectangle, Point(5, 0))));
    REQUIRE(history.is_dirty());
    REQUIRE(!history.merge(make_move(scene.m_rectangle, Point(-5, 0))));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.get_undo_label() == "Name");
    REQUIRE(!history.is_dirty());
    REQUIRE(take_snapshot(document) == clean);
    REQUIRE(!document.can_redo());
    REQUIRE(history.merge(make_move(scene.m_rectangle, Point(1, 0))));
    REQUIRE(history.get_undo_count() == 2);
  }

  TEST_CASE("seal") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    history.seal();
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.get_undo_count() == 2);
    history.undo();
    history.redo();
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.get_undo_count() == 3);
    history.merge(make_rename(scene.m_rectangle, "A"));
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.get_undo_count() == 5);
  }

  TEST_CASE("merge_failure") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    history.merge(make_rename(scene.m_rectangle, "A"));
    REQUIRE_THROWS_AS(history.merge(make_rename(scene.m_rectangle, "fail")),
      std::invalid_argument);
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(document.get(scene.m_rectangle).m_header.m_name == "A");
    history.merge(make_rename(scene.m_rectangle, "B"));
    REQUIRE(history.get_undo_count() == 2);
    history.undo();
    history.undo();
    REQUIRE(document.get(scene.m_rectangle).m_header.m_name == "Rectangle");
  }

  TEST_CASE("failure") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto before = take_snapshot(document);
    REQUIRE_THROWS_AS(history.execute(make_rename(scene.m_rectangle, "fail")),
      std::invalid_argument);
    REQUIRE(take_snapshot(document) == before);
    REQUIRE(!history.can_undo());
    REQUIRE(!document.can_undo());
  }

  TEST_CASE("external_commit") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    history.undo();
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_loose).m_header;
      header.m_name = "External";
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(!history.can_redo());
    REQUIRE(history.get_undo_label().empty());
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.get_undo_count() == 3);
    history.undo();
    history.undo();
    REQUIRE(document.get(scene.m_loose).m_header.m_name == "Loose");
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(111, 60));
  }

  TEST_CASE("recorded_steps") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto setting = 0;
    auto get_x = [&] {
      return get_world_transform(document, scene.m_rectangle).m_tx;
    };
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    history.mark_clean();
    setting = 1;
    history.record("Hide All Guides", [&] {
      setting = 0;
    }, [&] {
      setting = 1;
    });
    REQUIRE(history.get_undo_label() == "Hide All Guides");
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.is_dirty());
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.get_undo_count() == 3);
    REQUIRE(get_x() == 112);
    REQUIRE(history.undo());
    REQUIRE(get_x() == 111);
    REQUIRE(setting == 1);
    REQUIRE(history.undo());
    REQUIRE(setting == 0);
    REQUIRE(get_x() == 111);
    REQUIRE(!history.is_dirty());
    REQUIRE(history.undo());
    REQUIRE(get_x() == 110);
    REQUIRE(history.redo());
    REQUIRE(history.redo());
    REQUIRE(setting == 1);
    REQUIRE(history.get_undo_label() == "Hide All Guides");
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_loose).m_header;
      header.m_name = "External";
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(history.get_undo_count() == 3);
    REQUIRE(history.undo());
    REQUIRE(document.get(scene.m_loose).m_header.m_name == "Loose");
    REQUIRE(history.undo());
    REQUIRE(setting == 0);
    history.record("Lock All Guides", nullptr, nullptr);
    REQUIRE(!history.can_redo());
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    REQUIRE(get_x() == 110);
    history.clear();
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(history.undo());
    REQUIRE(get_x() == 110);
  }

  TEST_CASE("execution_in_observer") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto is_executed = false;
    auto subscription = document.subscribe([&] (const auto&) {
      if(!is_executed) {
        is_executed = true;
        REQUIRE(history.execute(make_rename(scene.m_rectangle, "Box")));
      }
    });
    {
      auto transaction = Transaction(document);
      auto header = document.get(scene.m_loose).m_header;
      header.m_name = "External";
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.get_undo_label() == "Name");
    REQUIRE(history.undo());
    REQUIRE(document.get(scene.m_rectangle).m_header.m_name == "Rectangle");
    REQUIRE(history.get_undo_label().empty());
    REQUIRE(history.undo());
    REQUIRE(document.get(scene.m_loose).m_header.m_name == "Loose");
  }

  TEST_CASE("commit_in_observer") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto is_committed = false;
    auto subscription = document.subscribe([&] (const auto&) {
      if(!is_committed) {
        is_committed = true;
        auto transaction = Transaction(document);
        auto header = document.get(scene.m_loose).m_header;
        header.m_name = "External";
        transaction.replace_header(header);
        transaction.commit();
      }
    });
    REQUIRE(history.execute(make_move(scene.m_rectangle, Point(1, 0))));
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(history.get_undo_label().empty());
    REQUIRE(history.undo());
    REQUIRE(document.get(scene.m_loose).m_header.m_name == "Loose");
    REQUIRE(history.get_undo_label() == "Move");
    REQUIRE(history.undo());
    REQUIRE(get_world_transform(document, scene.m_rectangle) ==
      make_translation(110, 60));
  }

  TEST_CASE("annotation") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    REQUIRE(!history.get_undo_annotation());
    REQUIRE(!history.get_redo_annotation());
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    *history.get_undo_annotation() = 5;
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(std::any_cast<int>(*history.get_undo_annotation()) == 5);
    history.undo();
    REQUIRE(std::any_cast<int>(*history.get_redo_annotation()) == 5);
    REQUIRE(!history.get_undo_annotation());
  }

  TEST_CASE("notifications") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    auto count = 0;
    auto subscription = history.subscribe([&] { ++count; });
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(count == 1);
    history.merge(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(count == 2);
    history.undo();
    REQUIRE(count == 3);
    history.redo();
    REQUIRE(count == 4);
    history.mark_clean();
    REQUIRE(count == 5);
    history.mark_clean();
    REQUIRE(count == 5);
    {
      auto transaction = Transaction(document);
      transaction.remove(scene.m_loose);
      transaction.commit();
    }
    REQUIRE(count == 6);
    history.clear();
    REQUIRE(count == 7);
    subscription.release();
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    REQUIRE(count == 7);
  }

  TEST_CASE("clear") {
    auto document = Document();
    auto scene = build_scene(document);
    auto history = History(document);
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    history.clear();
    REQUIRE(!history.can_undo());
    REQUIRE(!document.can_undo());
    REQUIRE(history.is_dirty());
    history.mark_clean();
    history.execute(make_move(scene.m_rectangle, Point(1, 0)));
    history.undo();
    history.clear();
    REQUIRE(!history.can_redo());
    REQUIRE(!document.can_redo());
    REQUIRE(!history.is_dirty());
  }
}

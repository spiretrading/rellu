#include <chrono>
#include <cstddef>
#include <string>
#include <doctest/doctest.h>
#include "SampleDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  struct Stopwatch {
    std::chrono::steady_clock::time_point m_start =
      std::chrono::steady_clock::now();

    double lap() {
      auto now = std::chrono::steady_clock::now();
      auto elapsed =
        std::chrono::duration<double, std::milli>(now - m_start).count();
      m_start = now;
      return elapsed;
    }
  };

  std::string format(const char* label, double milliseconds) {
    return std::string(label) + ": " + std::to_string(milliseconds) + " ms";
  }

  void test_document(int artboard_count, int shape_count) {
    auto node_count = std::size_t(artboard_count) * (shape_count + 2);
    auto document = Document();
    auto changes = std::size_t(0);
    auto subscription = document.subscribe([&] (const auto& change) {
      changes = change.m_inserted.size() + change.m_removed.size() +
        change.m_modified.size();
    });
    auto shape = make_rectangle_shape(10, 10);
    auto header = NodeHeader();
    header.m_name = "Rectangle";
    auto stopwatch = Stopwatch();
    auto transaction = Transaction(document);
    auto target = NodeId();
    for(auto i = 0; i != artboard_count; ++i) {
      auto artboard = Artboard();
      artboard.m_frame = Rect(i * 400.0, 0, 375, 812);
      auto artboard_id =
        transaction.append(NodeId(), make_header("Artboard"), artboard);
      auto group =
        transaction.append(artboard_id, make_header("Group"), Group());
      for(auto j = 0; j != shape_count; ++j) {
        header.m_transform = make_translation(j, i);
        target = transaction.append(group, header, shape);
      }
    }
    auto insertion = stopwatch.lap();
    transaction.commit();
    auto commit = stopwatch.lap();
    REQUIRE(document.get_nodes().get_size() == node_count);
    REQUIRE(changes == node_count);
    {
      auto edit = Transaction(document);
      auto edited = document.get(target).m_header;
      edited.m_opacity = 0.5;
      edit.replace_header(edited);
      edit.commit();
    }
    auto single_edit = stopwatch.lap();
    REQUIRE(changes == 1);
    REQUIRE(document.undo());
    auto single_undo = stopwatch.lap();
    REQUIRE(document.get(target).m_header.m_opacity == 1);
    REQUIRE(document.undo());
    auto bulk_undo = stopwatch.lap();
    REQUIRE(document.get_nodes().get_size() == 0);
    REQUIRE(changes == node_count);
    REQUIRE(document.redo());
    auto bulk_redo = stopwatch.lap();
    REQUIRE(document.get_nodes().get_size() == node_count);
    {
      auto removal = Transaction(document);
      while(!document.get_canvas().empty()) {
        removal.remove(document.get_canvas().back());
      }
      removal.commit();
    }
    auto bulk_removal = stopwatch.lap();
    REQUIRE(document.get_nodes().get_size() == 0);
    MESSAGE(node_count << " nodes");
    MESSAGE(format("insertion", insertion));
    MESSAGE(format("commit", commit));
    MESSAGE(format("single edit", single_edit));
    MESSAGE(format("single undo", single_undo));
    MESSAGE(format("bulk undo", bulk_undo));
    MESSAGE(format("bulk redo", bulk_redo));
    MESSAGE(format("bulk removal", bulk_removal));
  }
}

TEST_SUITE("Scale") {
  TEST_CASE("bulk_edits") {
    test_document(4, 98);
  }

  TEST_CASE("large_document" * doctest::skip()) {
    test_document(200, 998);
  }
}

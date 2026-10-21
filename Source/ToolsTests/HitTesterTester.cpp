#include <chrono>
#include <numbers>
#include <random>
#include <doctest/doctest.h>
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Editor/EditContext.hpp"
#include "Rellu/Tools/HitTester.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_artboard(Transaction& transaction, const Rect& frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    return transaction.append(NodeId(), make_header("Artboard"), artboard);
  }

  NodeId add_rectangle(Transaction& transaction, const NodeId& parent,
      const Rect& rect) {
    return transaction.append(parent, make_header("Rectangle",
      make_translation(rect.m_x, rect.m_y)),
      make_rectangle_shape(rect.m_width, rect.m_height));
  }

  Shape make_outline(double size, double width, StrokeAlignment alignment) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, size, size));
    shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), width);
    shape.m_style.m_stroke->m_alignment = alignment;
    return shape;
  }

  void edit_header(Document& document, const NodeId& id, auto edit) {
    auto transaction = Transaction(document);
    auto header = document.get(id).m_header;
    edit(header);
    transaction.replace_header(header);
    transaction.commit();
  }

  double to_milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  }
}

TEST_SUITE("HitTester") {
  TEST_CASE("shapes") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 200, 200));
    auto rectangle = add_rectangle(transaction, artboard, Rect(10, 10, 20, 20));
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(50, 50), 10, 10);
    ellipse.m_style.m_fill = SolidFill(make_color(0, 0, 255));
    auto circle = transaction.append(artboard,
      make_header("Ellipse", make_translation(100, 0)), ellipse);
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(15, 15), 0) == rectangle);
    REQUIRE(tester.hit(Point(150, 50), 0) == circle);
    REQUIRE(tester.hit(Point(158, 42), 0) == artboard);
    REQUIRE(tester.hit(Point(100, 100), 0) == artboard);
    REQUIRE(tester.hit(Point(300, 300), 0).is_nil());
    REQUIRE(*tester.get_extent(rectangle) == Rect(0, 0, 20, 20));
  }

  TEST_CASE("clipped_child") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 200, 200));
    auto rectangle =
      add_rectangle(transaction, artboard, Rect(180, 50, 60, 20));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(190, 60), 0, true) == rectangle);
    REQUIRE(tester.hit(Point(220, 60), 0, true).is_nil());
    REQUIRE(tester.hit_within(rectangle, Point(220, 60), 0) == rectangle);
    REQUIRE(tester.hit_within(rectangle, Point(260, 60), 0).is_nil());
  }

  TEST_CASE("strokes") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 300, 300));
    auto centered = transaction.append(artboard,
      make_header("Centered", make_translation(50, 50)),
      make_outline(40, 4, StrokeAlignment::CENTER));
    auto inside = transaction.append(artboard,
      make_header("Inside", make_translation(150, 50)),
      make_outline(40, 4, StrokeAlignment::INSIDE));
    auto line = Shape();
    line.m_geometry = LineShape(Point(0, 0), Point(100, 0));
    auto segment = transaction.append(
      artboard, make_header("Line", make_translation(20, 150)), line);
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(70, 70), 0) == artboard);
    REQUIRE(tester.hit(Point(50, 70), 0) == centered);
    REQUIRE(tester.hit(Point(48.5, 70), 0) == centered);
    REQUIRE(tester.hit(Point(46, 70), 1) == artboard);
    REQUIRE(tester.hit(Point(46, 70), 3) == centered);
    REQUIRE(tester.hit(Point(153, 70), 0) == inside);
    REQUIRE(tester.hit(Point(149, 70), 0.5) == artboard);
    REQUIRE(tester.hit(Point(170, 70), 0) == artboard);
    REQUIRE(tester.hit(Point(60, 151), 2) == segment);
    REQUIRE(tester.hit(Point(60, 155), 2) == artboard);
  }

  TEST_CASE("groups") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(100, 100, 200, 200));
    auto group = transaction.append(artboard,
      make_header("Group", make_translation(20, 20)), Group());
    auto inner = transaction.append(group, make_header("Inner"), Group());
    auto rectangle = add_rectangle(transaction, inner, Rect(0, 0, 30, 30));
    auto overhang =
      add_rectangle(transaction, artboard, Rect(180, 180, 50, 50));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(125, 125), 0) == rectangle);
    REQUIRE(tester.hit(Point(155, 155), 0) == artboard);
    auto context = EditContext(document);
    REQUIRE(context.resolve(rectangle) == group);
    REQUIRE(context.enter(group));
    REQUIRE(context.resolve(rectangle) == inner);
    REQUIRE(tester.hit(Point(290, 290), 0) == overhang);
    REQUIRE(tester.hit(Point(310, 310), 0).is_nil());
  }

  TEST_CASE("hidden") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 100, 100));
    auto rectangle = add_rectangle(transaction, artboard, Rect(10, 10, 20, 20));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(15, 15), 0) == rectangle);
    edit_header(document, rectangle, [] (auto& header) {
      header.m_is_visible = false;
    });
    REQUIRE(tester.hit(Point(15, 15), 0) == artboard);
    REQUIRE(!tester.get_extent(rectangle));
  }

  TEST_CASE("locked") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 100, 100));
    auto bottom = add_rectangle(transaction, artboard, Rect(10, 10, 20, 20));
    auto top = add_rectangle(transaction, artboard, Rect(10, 10, 20, 20));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(15, 15), 0) == top);
    edit_header(document, top, [] (auto& header) {
      header.m_is_locked = true;
    });
    REQUIRE(tester.hit(Point(15, 15), 0) == bottom);
    edit_header(document, artboard, [] (auto& header) {
      header.m_is_locked = true;
    });
    REQUIRE(tester.hit(Point(15, 15), 0).is_nil());
  }

  TEST_CASE("masks") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto mask = Group();
    mask.m_mask = Mask();
    mask.m_mask->m_shapes.push_back(
      MaskShape(Matrix(), RectangleShape(Rect(0, 0, 20, 20))));
    auto group = transaction.append(
      NodeId(), make_header("Mask", make_translation(100, 100)), mask);
    auto child = add_rectangle(transaction, group, Rect(0, 0, 40, 40));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(110, 110), 0) == child);
    REQUIRE(tester.hit(Point(130, 130), 0).is_nil());
    REQUIRE(*tester.get_extent(group) == Rect(0, 0, 20, 20));
  }

  TEST_CASE("references") {
    auto document = Document();
    auto scene = build_scene(document);
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(410, 260), 0) == scene.m_reference);
    auto context = EditContext(document);
    REQUIRE(context.resolve(scene.m_reference) == scene.m_instance);
    auto transaction = Transaction(document);
    auto master = document.get(scene.m_background).m_header;
    master.m_transform = make_translation(40, 0);
    transaction.replace_header(master);
    transaction.commit();
    REQUIRE(tester.hit(Point(410, 260), 0) == scene.m_artboard);
    REQUIRE(tester.hit(Point(450, 260), 0) == scene.m_reference);
  }

  TEST_CASE("text") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 200, 200));
    auto text = transaction.append(artboard,
      make_header("Text", make_translation(20, 20)), make_text(u"Hello"));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(110, 55), 0) == text);
    REQUIRE(tester.hit(Point(130, 55), 0) == artboard);
  }

  TEST_CASE("scroll_groups") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto scroll = Group();
    scroll.m_scroll_group =
      ScrollGroup(ScrollDirection::VERTICAL, Size(50, 50), Point(0, 0));
    auto group = transaction.append(NodeId(), make_header("Scroll"), scroll);
    auto child = add_rectangle(transaction, group, Rect(0, 0, 100, 100));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(80, 20), 0) == child);
    REQUIRE(tester.hit(Point(20, 80), 0).is_nil());
  }

  TEST_CASE("query") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 500, 500));
    auto first = add_rectangle(transaction, artboard, Rect(10, 10, 20, 20));
    auto second = add_rectangle(transaction, artboard, Rect(50, 10, 20, 20));
    auto turned = transaction.append(artboard, make_header("Turned",
      make_translation(200, 200) * make_rotation(std::numbers::pi / 4)),
      make_rectangle_shape(100, 10));
    auto hidden = add_rectangle(transaction, artboard, Rect(10, 10, 20, 20));
    auto locked = add_rectangle(transaction, artboard, Rect(40, 40, 10, 10));
    transaction.commit();
    edit_header(document, hidden, [] (auto& header) {
      header.m_is_visible = false;
    });
    edit_header(document, locked, [] (auto& header) {
      header.m_is_locked = true;
    });
    auto tester = HitTester(document);
    REQUIRE(tester.query(artboard, Rect(0, 0, 60, 60)) ==
      std::vector{first, second, locked});
    REQUIRE(tester.query(artboard, Rect(0, 0, 60, 60), true) ==
      std::vector{first, locked});
    REQUIRE(tester.query(artboard, Rect(260, 200, 20, 20)).empty());
    REQUIRE(tester.query(artboard, Rect(230, 230, 20, 20)) ==
      std::vector{turned});
    REQUIRE(tester.query(NodeId(), Rect(-10, -10, 20, 20)) ==
      std::vector{artboard});
    REQUIRE(tester.query(NodeId(), Rect(-10, -10, 20, 20), true).empty());
  }

  TEST_CASE("query_groups") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 500, 500));
    auto group = transaction.append(artboard, make_header("Group"), Group());
    add_rectangle(transaction, group, Rect(10, 10, 20, 20));
    add_rectangle(transaction, group, Rect(10, 50, 20, 20));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.query(artboard, Rect(15, 35, 10, 10)).empty());
    REQUIRE(tester.query(artboard, Rect(15, 25, 10, 10)) ==
      std::vector{group});
    REQUIRE(tester.query(artboard, Rect(0, 0, 100, 100), true) ==
      std::vector{group});
  }

  TEST_CASE("changes") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 200, 200));
    auto group = transaction.append(artboard, make_header("Group"), Group());
    auto rectangle = add_rectangle(transaction, group, Rect(10, 10, 20, 20));
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(tester.hit(Point(15, 15), 0) == rectangle);
    auto history = History(document);
    history.execute(std::make_unique<TranslateCommand>(
      std::vector{rectangle}, Point(100, 0)));
    REQUIRE(tester.hit(Point(15, 15), 0) == artboard);
    REQUIRE(tester.hit(Point(115, 15), 0) == rectangle);
    REQUIRE(*tester.get_extent(group) == Rect(110, 10, 20, 20));
    history.undo();
    REQUIRE(tester.hit(Point(15, 15), 0) == rectangle);
  }

  TEST_CASE("prepare") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 1000, 1000));
    auto group = transaction.append(artboard, make_header("Group"), Group());
    auto last = NodeId();
    for(auto i = 0; i != 1000; ++i) {
      last = add_rectangle(transaction, group, Rect(i, 0, 1, 1));
    }
    transaction.commit();
    auto tester = HitTester(document);
    REQUIRE(!tester.prepare(std::chrono::nanoseconds(0)));
    auto calls = 1;
    while(!tester.prepare(std::chrono::nanoseconds(0))) {
      ++calls;
    }
    REQUIRE(calls > 1);
    REQUIRE(tester.prepare(std::chrono::nanoseconds(0)));
    REQUIRE(*tester.get_extent(group) == Rect(0, 0, 1000, 1));
    REQUIRE(tester.hit(Point(999.5, 0.5), 0) == last);
  }

  TEST_CASE("performance") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto ids = std::vector<NodeId>();
    for(auto row = 0; row != 10; ++row) {
      for(auto column = 0; column != 10; ++column) {
        auto artboard = add_artboard(transaction,
          Rect(1100 * column, 1100 * row, 1000, 1000));
        for(auto i = 0; i != 20; ++i) {
          auto group = transaction.append(artboard, make_header("Group",
            make_translation(50 * i, 0)), Group());
          for(auto j = 0; j != 50; ++j) {
            ids.push_back(add_rectangle(
              transaction, group, Rect(0, 20 * j, 40, 15)));
          }
        }
      }
    }
    transaction.commit();
    REQUIRE(document.get_nodes().get_size() == 102100);
    auto tester = HitTester(document);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(tester.hit(Point(5505, 5505), 0) == ids[55 * 1000]);
    auto cold = to_milliseconds(std::chrono::steady_clock::now() - start);
    auto random = std::mt19937(7);
    auto distribution = std::uniform_real_distribution<double>(0, 10900);
    auto count = 2000;
    start = std::chrono::steady_clock::now();
    for(auto i = 0; i != count; ++i) {
      tester.hit(Point(distribution(random), distribution(random)), 3);
    }
    auto warm =
      to_milliseconds(std::chrono::steady_clock::now() - start) / count;
    start = std::chrono::steady_clock::now();
    auto artboard = document.get_canvas()[55];
    auto group = document.get(artboard).m_children[3];
    REQUIRE(tester.query(group, Rect(5650, 5500, 100, 290)).size() == 15);
    REQUIRE(tester.query(artboard, Rect(5500, 5500, 290, 290)).size() == 6);
    auto marquee = to_milliseconds(std::chrono::steady_clock::now() - start);
    MESSAGE("cold hit ", cold, " ms, warm hit ", warm, " ms, marquee ",
      marquee, " ms");
    REQUIRE(warm < 1);
    REQUIRE(marquee < 50);
  }
}

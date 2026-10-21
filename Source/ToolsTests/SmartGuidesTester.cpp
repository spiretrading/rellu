#include <chrono>
#include <cmath>
#include <doctest/doctest.h>
#include "Rellu/Tools/SmartGuides.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "../CommandsTests/Fixture.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_rectangle(Transaction& transaction, const NodeId& parent,
      const Rect& rect) {
    return transaction.append(parent, make_header("Rectangle",
      make_translation(rect.m_x, rect.m_y)),
      make_rectangle_shape(rect.m_width, rect.m_height));
  }

  NodeId add_artboard(Transaction& transaction, const Rect& frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    return transaction.append(NodeId(), make_header("Board"), artboard);
  }

  void set_artboard(Document& document, const NodeId& id,
      const Artboard& artboard) {
    auto transaction = Transaction(document);
    transaction.replace_payload(id, artboard);
    transaction.commit();
    document.clear_history();
  }

  SmartGuides& get_guides(ToolManager& tools) {
    return dynamic_cast<SmartGuides&>(tools.get_context().get_snap_service());
  }

  PointerEvent make_event(Point position, Qt::MouseButton button,
      Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
    return PointerEvent(position, button, buttons, modifiers);
  }

  void drag(ToolManager& tools, Point from, Point to,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.press(make_event(from, Qt::LeftButton, Qt::LeftButton, modifiers));
    for(auto i = 1; i <= 4; ++i) {
      tools.move(make_event(interpolate(from, to, i / 4.0), Qt::NoButton,
        Qt::LeftButton, modifiers));
    }
    tools.release(make_event(to, Qt::LeftButton, Qt::NoButton, modifiers));
  }

  void hover(ToolManager& tools, Point position,
      Qt::KeyboardModifiers modifiers = {}) {
    tools.move(make_event(position, Qt::NoButton, Qt::NoButton, modifiers));
  }

  struct Board {
    NodeId m_artboard;
    NodeId m_left;
    NodeId m_moved;
  };

  Board build_board(Document& document) {
    auto board = Board();
    auto transaction = Transaction(document);
    board.m_artboard = add_artboard(transaction, Rect(0, 0, 400, 300));
    board.m_left =
      add_rectangle(transaction, board.m_artboard, Rect(100, 100, 50, 50));
    board.m_moved =
      add_rectangle(transaction, board.m_artboard, Rect(200, 200, 10, 10));
    transaction.commit();
    document.clear_history();
    return board;
  }

  Rect get_bounds(const Document& document, const NodeId& id) {
    return *get_world_bounds(document, id);
  }
}

TEST_SUITE("SmartGuides") {
  TEST_CASE("edge_alignment") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(board.m_artboard, std::vector{board.m_moved});
    auto delta =
      guides.snap_move(Rect(200, 200, 10, 10), Point(-97, 0), Qt::NoModifier);
    REQUIRE(delta == Point(-100, 0));
    REQUIRE(guides.get_lines() ==
      std::vector{GuideLine(Point(100, 100), Point(100, 210))});
    REQUIRE(guides.get_distances().empty());
    guides.end();
    REQUIRE(guides.get_lines().empty());
  }

  TEST_CASE("center_alignment") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(board.m_artboard, std::vector{board.m_moved});
    auto delta =
      guides.snap_move(Rect(10, 10, 20, 20), Point(178, 0), Qt::NoModifier);
    REQUIRE(delta == Point(180, 0));
    REQUIRE(guides.get_lines() ==
      std::vector{GuideLine(Point(200, 0), Point(200, 300))});
  }

  TEST_CASE("line_ends") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(board.m_artboard, std::vector{board.m_moved});
    REQUIRE(guides.snap_line(Point(10, 10), Point(148, 60), false, false,
      Qt::NoModifier) == LineEnds(Point(10, 10), Point(150, 60)));
    REQUIRE(guides.get_lines() ==
      std::vector{GuideLine(Point(150, 10), Point(150, 150))});
    REQUIRE(guides.snap_line(Point(0, 52), Point(97, 149), true, false,
      Qt::NoModifier) == LineEnds(Point(0, 52), Point(98, 150)));
    REQUIRE(guides.snap_line(Point(20, 60), Point(148, 60), true, false,
      Qt::NoModifier) == LineEnds(Point(20, 60), Point(150, 60)));
    REQUIRE(guides.snap_line(Point(98, 20), Point(98, 70), true, false,
      Qt::NoModifier) == LineEnds(Point(98, 20), Point(98, 70)));
    REQUIRE(guides.snap_line(Point(20, 60), Point(147, 60), false, true,
      Qt::NoModifier) == LineEnds(Point(17, 60), Point(150, 60)));
    REQUIRE(guides.snap_line(Point(99, 60), Point(180, 60), false, true,
      Qt::NoModifier) == LineEnds(Point(100, 60), Point(179, 60)));
    REQUIRE(guides.snap_line(Point(10, 10), Point(148, 60), false, false,
      Qt::ControlModifier) == LineEnds(Point(10, 10), Point(148, 60)));
    REQUIRE(guides.get_lines().empty());
  }

  TEST_CASE("stroked_targets") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300));
    auto shape = make_rectangle_shape(50, 50);
    shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 1);
    transaction.append(artboard,
      make_header("Stroked", make_translation(100, 100)), shape);
    auto moved = add_rectangle(transaction, artboard, Rect(200, 200, 10, 10));
    transaction.commit();
    document.clear_history();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(artboard, std::vector{moved});
    REQUIRE(guides.snap_line(Point(10, 10), Point(148.5, 60), false, false,
      Qt::NoModifier) == LineEnds(Point(10, 10), Point(150, 60)));
    REQUIRE(guides.snap_line(Point(20, 60), Point(152, 60), true, false,
      Qt::NoModifier) == LineEnds(Point(20, 60), Point(150, 60)));
    REQUIRE(guides.snap_move(Rect(200, 200, 10, 10), Point(-47.5, 0),
      Qt::NoModifier) == Point(-50, 0));
  }

  TEST_CASE("threshold") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    auto bounds = Rect(200, 200, 10, 10);
    guides.begin(board.m_artboard, std::vector{board.m_moved});
    REQUIRE(guides.snap_move(bounds, Point(-96.6, 0), Qt::NoModifier).m_x ==
      doctest::Approx(-100));
    REQUIRE(guides.snap_move(bounds, Point(-96, 0), Qt::NoModifier) ==
      Point(-96, 0));
    REQUIRE(guides.get_lines().empty());
    tools.set_view(make_scale(2, 2), 1, Size(1000, 800));
    REQUIRE(guides.snap_move(bounds, Point(-98.3, 0), Qt::NoModifier).m_x ==
      doctest::Approx(-100));
    REQUIRE(guides.snap_move(bounds, Point(-98, 0), Qt::NoModifier) ==
      Point(-98, 0));
  }

  TEST_CASE("control") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(board.m_artboard, std::vector{board.m_moved});
    REQUIRE(guides.snap_move(Rect(200, 200, 10, 10), Point(-97, 0),
      Qt::ControlModifier) == Point(-97, 0));
    REQUIRE(guides.get_lines().empty());
    REQUIRE(guides.snap_point(Point(103, 50), SnapAxes::BOTH,
      Qt::ControlModifier) == Point(103, 50));
  }

  TEST_CASE("inactive") {
    auto document = Document();
    build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    REQUIRE(guides.snap_move(Rect(200, 200, 10, 10), Point(-97, 0),
      Qt::NoModifier) == Point(-97, 0));
    REQUIRE(guides.snap_point(Point(103, 50), SnapAxes::BOTH,
      Qt::NoModifier) == Point(103, 50));
  }

  TEST_CASE("excluded_nodes") {
    auto document = Document();
    auto board = build_board(document);
    auto transaction = Transaction(document);
    auto hidden =
      add_rectangle(transaction, board.m_artboard, Rect(300, 20, 30, 30));
    auto header = document.get(hidden).m_header;
    header.m_is_visible = false;
    transaction.replace_header(header);
    transaction.commit();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(board.m_artboard, std::vector{board.m_moved});
    REQUIRE(guides.snap_point(Point(302, 250), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(302, 250));
    REQUIRE(guides.snap_point(Point(212, 250), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(212, 250));
  }

  TEST_CASE("point_axes") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(board.m_artboard, std::vector<NodeId>());
    REQUIRE(guides.snap_point(Point(103, 97), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(100, 97));
    REQUIRE(guides.get_lines() ==
      std::vector{GuideLine(Point(100, 97), Point(100, 150))});
    REQUIRE(guides.snap_point(Point(103, 97), SnapAxes::VERTICAL,
      Qt::NoModifier) == Point(103, 100));
    REQUIRE(guides.snap_point(Point(103, 97), SnapAxes::BOTH,
      Qt::NoModifier) == Point(100, 100));
    REQUIRE(guides.get_lines().size() == 2);
    REQUIRE(guides.snap_point(Point(103, 97), SnapAxes::NONE,
      Qt::NoModifier) == Point(103, 97));
  }

  TEST_CASE("equal_spacing") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300));
    add_rectangle(transaction, artboard, Rect(50, 100, 50, 50));
    add_rectangle(transaction, artboard, Rect(200, 100, 50, 50));
    auto moved = add_rectangle(transaction, artboard, Rect(300, 200, 40, 50));
    transaction.commit();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(artboard, std::vector{moved});
    REQUIRE(guides.snap_move(Rect(300, 200, 40, 50), Point(-172, -100),
      Qt::NoModifier) == Point(-170, -100));
    REQUIRE(guides.get_distances() == std::vector{
      GuideDistance(Point(100, 125), Point(130, 125)),
      GuideDistance(Point(170, 125), Point(200, 125))});
    REQUIRE(guides.get_distances().front().get_length() == 30);
    REQUIRE(guides.get_lines().size() == 3);
  }

  TEST_CASE("chained_spacing") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300));
    add_rectangle(transaction, artboard, Rect(100, 50, 50, 50));
    add_rectangle(transaction, artboard, Rect(100, 130, 50, 50));
    auto moved = add_rectangle(transaction, artboard, Rect(300, 250, 10, 20));
    transaction.commit();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(artboard, std::vector{moved});
    auto delta = guides.snap_move(
      Rect(300, 250, 10, 20), Point(-192, -38), Qt::NoModifier);
    REQUIRE(delta == Point(-192, -40));
    REQUIRE(guides.get_distances() == std::vector{
      GuideDistance(Point(125, 100), Point(125, 130)),
      GuideDistance(Point(113, 180), Point(113, 210))});
  }

  TEST_CASE("distance_labels") {
    REQUIRE(format_distance(30) == "30");
    REQUIRE(format_distance(12.5) == "12.5");
    REQUIRE(format_distance(1.0 / 3) == "0.3");
    REQUIRE(format_distance(2.999) == "3");
    REQUIRE(format_distance(100.25) == "100.3");
    REQUIRE(format_distance(1234.5) == "1,234.5");
    REQUIRE(format_distance(1234567) == "1,234,567");
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 400, 300));
    add_rectangle(transaction, artboard, Rect(50, 100, 50, 50));
    add_rectangle(transaction, artboard, Rect(200, 100, 50, 50));
    auto moved = add_rectangle(transaction, artboard, Rect(130, 100, 40, 50));
    transaction.commit();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto snap = SnapService();
    tools.get_context().set_snap_service(snap);
    tools.set_view(Matrix(), 2, Size(1000, 800));
    auto empty = tools.draw();
    auto guides = SmartGuides(tools.get_context());
    tools.get_context().set_snap_service(guides);
    guides.begin(artboard, std::vector{moved});
    guides.snap_move(Rect(130, 100, 40, 50), Point(0, 0), Qt::NoModifier);
    auto overlay = tools.draw();
    REQUIRE(overlay.m_images.size() == empty.m_images.size() + 2);
    auto& label = *overlay.m_images.back().m_image;
    REQUIRE(label.height() ==
      static_cast<int>(std::ceil(2 * SmartGuides::LABEL_HEIGHT)));
    REQUIRE(label.pixelColor(0, 0).alpha() == 0);
    auto is_pink = false;
    for(auto y = 0; y != label.height(); ++y) {
      for(auto x = 0; x != label.width(); ++x) {
        auto color = label.pixelColor(x, y);
        if(color.alpha() == 255) {
          REQUIRE(color.red() == 255);
          REQUIRE(color.green() == 73);
          REQUIRE(color.blue() == 225);
          is_pink = true;
        }
      }
    }
    REQUIRE(is_pink);
    auto position = overlay.m_images.back().m_position;
    REQUIRE(position.m_x == doctest::Approx(185 - label.width() / 4.0));
    REQUIRE(position.m_y == doctest::Approx(125 - SmartGuides::LABEL_OFFSET -
      SmartGuides::FONT_ASCENT / 2 - label.height() / 4.0));
    auto alignments = std::size_t(0);
    auto distances = std::size_t(0);
    for(auto& shape : overlay.m_shapes) {
      if(shape.m_fill == SmartGuides::ALIGNMENT_COLOR) {
        ++alignments;
      } else if(shape.m_fill == SmartGuides::DISTANCE_COLOR) {
        ++distances;
      }
    }
    REQUIRE(alignments == 3);
    REQUIRE(distances == 2);
    tools.get_context().set_snap_service(snap);
  }

  TEST_CASE("layout_grid") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto id = add_artboard(transaction, Rect(0, 0, 400, 300));
    transaction.commit();
    auto artboard = *document.get(id).get_artboard();
    artboard.m_layout_grid = LayoutGrid();
    artboard.m_layout_grid->m_column_count = 4;
    artboard.m_layout_grid->m_gutter = 10;
    artboard.m_layout_grid->m_margins.m_left = 20;
    artboard.m_layout_grid->m_margins.m_right = 20;
    set_artboard(document, id, artboard);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(id, std::vector<NodeId>());
    REQUIRE(guides.snap_point(Point(104, 50), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(104, 50));
    REQUIRE(!guides.is_grid_snapped());
    guides.end();
    auto settings = GridSettings();
    settings.m_is_layout_visible = true;
    tools.get_context().set_grid_settings(settings);
    guides.begin(id, std::vector<NodeId>());
    REQUIRE(guides.snap_point(Point(104, 50), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(102.5, 50));
    REQUIRE(guides.is_grid_snapped());
    REQUIRE(guides.snap_point(Point(379, 50), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(380, 50));
    REQUIRE(guides.get_lines().empty());
    REQUIRE(guides.snap_point(Point(2, 50), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(0, 50));
    REQUIRE(!guides.is_grid_snapped());
  }

  TEST_CASE("square_grid") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto id = add_artboard(transaction, Rect(100, 100, 400, 300));
    transaction.commit();
    auto artboard = *document.get(id).get_artboard();
    artboard.m_layout_grid = LayoutGrid();
    artboard.m_layout_grid->m_type = GridType::LAYOUT;
    artboard.m_layout_grid->m_square_size = Size(8, 8);
    set_artboard(document, id, artboard);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto settings = GridSettings();
    settings.m_is_square_visible = true;
    tools.get_context().set_grid_settings(settings);
    auto& guides = get_guides(tools);
    guides.begin(id, std::vector<NodeId>());
    REQUIRE(guides.snap_point(Point(219, 133), SnapAxes::BOTH,
      Qt::NoModifier) == Point(220, 132));
    REQUIRE(guides.is_grid_snapped());
    REQUIRE(guides.snap_point(Point(80, 133), SnapAxes::BOTH,
      Qt::NoModifier) == Point(80, 132));
    REQUIRE(guides.is_grid_snapped());
    REQUIRE(guides.snap_point(Point(219, 133), SnapAxes::BOTH,
      Qt::ControlModifier) == Point(219, 133));
    REQUIRE(!guides.is_grid_snapped());
    REQUIRE(guides.snap_point(Point(80, 300), SnapAxes::HORIZONTAL,
      Qt::NoModifier) == Point(80, 300));
    REQUIRE(!guides.is_grid_snapped());
  }

  TEST_CASE("ruler_guides") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto id = add_artboard(transaction, Rect(0, 0, 400, 300));
    transaction.commit();
    auto artboard = *document.get(id).get_artboard();
    artboard.m_guides.push_back(Guide(Orientation::VERTICAL, 33));
    artboard.m_guides.push_back(Guide(Orientation::HORIZONTAL, 77));
    set_artboard(document, id, artboard);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    guides.begin(id, std::vector<NodeId>());
    REQUIRE(guides.snap_point(Point(35, 75), SnapAxes::BOTH,
      Qt::NoModifier) == Point(33, 77));
  }

  TEST_CASE("measure") {
    auto document = Document();
    build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    auto& guides = get_guides(tools);
    guides.measure(Rect(100, 100, 50, 50), Rect(200, 100, 50, 50));
    REQUIRE(guides.get_measured_distances() ==
      std::vector{GuideDistance(Point(150, 125), Point(200, 125))});
    REQUIRE(guides.get_measured_lines().empty());
    REQUIRE(guides.get_measured_edges() == std::vector{
      GuideLine(Point(100, 100), Point(250, 100)),
      GuideLine(Point(100, 150), Point(250, 150))});
    guides.measure(Rect(100, 100, 50, 50), Rect(0, 0, 400, 300));
    REQUIRE(guides.get_measured_edges().empty());
    REQUIRE(guides.get_measured_distances() == std::vector{
      GuideDistance(Point(125, 100), Point(125, 0)),
      GuideDistance(Point(150, 125), Point(400, 125)),
      GuideDistance(Point(125, 150), Point(125, 300)),
      GuideDistance(Point(100, 125), Point(0, 125))});
    guides.measure(Rect(0, 0, 10, 10), Rect(100, 100, 10, 10));
    REQUIRE(guides.get_measured_distances() == std::vector{
      GuideDistance(Point(10, 5), Point(100, 5)),
      GuideDistance(Point(5, 10), Point(5, 100))});
    REQUIRE(guides.get_measured_lines() == std::vector{
      GuideLine(Point(100, 5), Point(100, 100)),
      GuideLine(Point(5, 100), Point(100, 100))});
    guides.measure(Rect(100, 100, 50, 50), Rect(170, 60, 50, 60));
    REQUIRE(guides.get_measured_distances() == std::vector{
      GuideDistance(Point(150, 100), Point(170, 100), LabelSide::LEADING)});
    guides.measure(Rect(100, 100, 50, 50), Rect(170, 140, 50, 60));
    REQUIRE(guides.get_measured_distances() == std::vector{
      GuideDistance(Point(150, 150), Point(170, 150), LabelSide::TRAILING)});
    guides.measure(Rect(100, 100, 50, 50), Rect(140, 170, 60, 50));
    REQUIRE(guides.get_measured_distances() == std::vector{
      GuideDistance(Point(150, 150), Point(150, 170), LabelSide::TRAILING)});
    guides.measure(Rect(100, 100, 50, 50), Rect(120, 120, 50, 50));
    REQUIRE(guides.get_measured_distances() == std::vector{
      GuideDistance(Point(150, 100), Point(170, 100), LabelSide::LEADING),
      GuideDistance(Point(170, 120), Point(170, 100), LabelSide::TRAILING),
      GuideDistance(Point(120, 170), Point(100, 170), LabelSide::TRAILING),
      GuideDistance(Point(100, 150), Point(100, 170), LabelSide::LEADING)});
    guides.measure(std::nullopt, Rect());
    REQUIRE(guides.get_measured_distances().empty());
    REQUIRE(guides.get_measured_lines().empty());
  }

  TEST_CASE("alt_hover") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    editor.get_selection().select(board.m_moved);
    hover(tools, Point(125, 125), Qt::AltModifier);
    REQUIRE(guides.get_measured_distances() == std::vector{
      GuideDistance(Point(200, 205), Point(150, 205)),
      GuideDistance(Point(205, 200), Point(205, 150))});
    REQUIRE(guides.get_measured_lines() == std::vector{
      GuideLine(Point(150, 205), Point(150, 150)),
      GuideLine(Point(205, 150), Point(150, 150))});
    hover(tools, Point(300, 50), Qt::AltModifier);
    REQUIRE(guides.get_measured_distances().size() == 4);
    hover(tools, Point(300, 50));
    REQUIRE(guides.get_measured_distances().empty());
  }

  TEST_CASE("select_tool_move") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    tools.press(make_event(
      Point(205, 205), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
    tools.move(make_event(
      Point(108, 205), Qt::NoButton, Qt::LeftButton, Qt::NoModifier));
    REQUIRE(get_bounds(document, board.m_moved) == Rect(100, 200, 10, 10));
    REQUIRE(guides.get_lines() ==
      std::vector{GuideLine(Point(100, 100), Point(100, 210))});
    tools.move(make_event(
      Point(108, 205), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier));
    REQUIRE(get_bounds(document, board.m_moved) == Rect(103, 200, 10, 10));
    REQUIRE(guides.get_lines().empty());
    tools.release(make_event(
      Point(108, 205), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    REQUIRE(guides.get_lines().empty());
    REQUIRE(editor.get_history().get_undo_count() == 1);
  }

  TEST_CASE("select_tool_resize") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    editor.get_selection().select(board.m_left);
    drag(tools, Point(150, 125), Point(197, 125));
    require_near(get_bounds(document, board.m_left), Rect(100, 100, 100, 50));
    REQUIRE(editor.get_history().get_undo_count() == 1);
    drag(tools, Point(200, 150), Point(207, 212));
    require_near(get_bounds(document, board.m_left), Rect(100, 100, 105, 110));
  }

  TEST_CASE("draw") {
    auto document = Document();
    auto board = build_board(document);
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    tools.set_tool("rectangle");
    drag(tools, Point(198, 20), Point(148, 97));
    auto& selection = editor.get_selection();
    REQUIRE(selection.get_size() == 1);
    auto drawn = selection.get_nodes().front();
    REQUIRE(document.get(drawn).m_parent == board.m_artboard);
    REQUIRE(get_bounds(document, drawn) == Rect(150, 20, 50, 80));
    REQUIRE(get_guides(tools).get_lines().empty());
    tools.set_tool("artboard");
    drag(tools, Point(500, 3), Point(600, 297));
    REQUIRE(get_bounds(document, selection.get_nodes().front()) ==
      Rect(500, 0, 100, 300));
  }

  TEST_CASE("performance") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, Rect(0, 0, 20000, 20000));
    auto moved = NodeId();
    for(auto i = 0; i != 200; ++i) {
      for(auto j = 0; j != 100; ++j) {
        moved = add_rectangle(transaction, artboard,
          Rect(100.0 * i + j % 7, 200.0 * j + i % 5, 40 + i % 11, 30));
      }
    }
    transaction.commit();
    auto editor = Editor(document);
    auto tools = ToolManager(editor);
    tools.set_view(Matrix(), 1, Size(1000, 800));
    auto& guides = get_guides(tools);
    auto start = std::chrono::steady_clock::now();
    guides.begin(artboard, std::vector{moved});
    auto bounds = get_bounds(document, moved);
    guides.snap_move(bounds, Point(1, 1), Qt::NoModifier);
    auto collected = std::chrono::steady_clock::now();
    for(auto i = 0; i != 1000; ++i) {
      guides.snap_move(bounds, Point(-0.37 * i, -0.53 * i), Qt::NoModifier);
    }
    auto finished = std::chrono::steady_clock::now();
    auto collection =
      std::chrono::duration<double, std::milli>(collected - start).count();
    auto snap = std::chrono::duration<double, std::milli>(
      finished - collected).count() / 1000;
    MESSAGE("20000 nodes: collection ", collection, " ms, snap ", snap, " ms");
    REQUIRE(collection < 2000);
    REQUIRE(snap < 5);
  }
}

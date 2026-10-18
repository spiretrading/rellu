#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <doctest/doctest.h>
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Geometry/Stroke.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

using namespace Rellu;

namespace {
  const auto TOLERANCE = 1.0 / 32;

  NodeHeader make_header(const Matrix& transform = Matrix()) {
    auto header = NodeHeader();
    header.m_transform = transform;
    return header;
  }

  Shape make_shape(const Rect& bounds, Color color) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(bounds);
    shape.m_style.m_fill = SolidFill(color);
    return shape;
  }

  const FillCommand& get_fill(const DrawList& list, std::size_t index) {
    return std::get<FillCommand>(list.at(index));
  }

  const Color& get_color(const FillCommand& command) {
    return std::get<SolidPaint>(command.m_paint).m_color;
  }
}

TEST_SUITE("SceneBuilder") {
  TEST_CASE("empty") {
    auto document = Document();
    auto builder = SceneBuilder(document, TOLERANCE);
    REQUIRE(builder.build().empty());
    REQUIRE(builder.build(generate_node_id()).empty());
  }

  TEST_CASE("fill") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto red = make_color(255, 0, 0);
    transaction.append(NodeId(), make_header(make_translation(5, 6)),
      make_shape(Rect(1, 2, 3, 4), red));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto& fill = get_fill(list, 0);
    REQUIRE(*fill.m_path == make_rectangle(Rect(1, 2, 3, 4)));
    REQUIRE(fill.m_transform == make_translation(5, 6));
    REQUIRE(get_color(fill) == red);
    REQUIRE(!fill.m_clip);
  }

  TEST_CASE("z_order") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(
      NodeId(), make_header(make_translation(10, 0)), Group());
    transaction.append(group, make_header(),
      make_shape(Rect(0, 0, 1, 1), make_color(255, 0, 0)));
    transaction.append(group, make_header(make_translation(0, 5)),
      make_shape(Rect(0, 0, 1, 1), make_color(0, 255, 0)));
    transaction.append(NodeId(), make_header(),
      make_shape(Rect(0, 0, 1, 1), make_color(0, 0, 255)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 3);
    REQUIRE(get_color(get_fill(list, 0)) == make_color(255, 0, 0));
    REQUIRE(get_fill(list, 0).m_transform == make_translation(10, 0));
    REQUIRE(get_color(get_fill(list, 1)) == make_color(0, 255, 0));
    REQUIRE(get_fill(list, 1).m_transform == make_translation(10, 5));
    REQUIRE(get_color(get_fill(list, 2)) == make_color(0, 0, 255));
    REQUIRE(get_fill(list, 2).m_transform == Matrix());
  }

  TEST_CASE("visibility") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto hidden = make_header();
    hidden.m_is_visible = false;
    auto group = transaction.append(NodeId(), hidden, Group());
    transaction.append(group, make_header(),
      make_shape(Rect(0, 0, 1, 1), make_color(255, 0, 0)));
    transaction.append(NodeId(), hidden,
      make_shape(Rect(0, 0, 1, 1), make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    REQUIRE(builder.build().empty());
  }

  TEST_CASE("opacity") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto translucent = make_header();
    translucent.m_opacity = 0.5;
    auto group = transaction.append(NodeId(), translucent, Group());
    auto shape = make_shape(Rect(0, 0, 1, 1), make_color(255, 0, 0, 0.5));
    shape.m_style.m_stroke = Stroke(make_color(0, 0, 255), 1);
    transaction.append(group, translucent, shape);
    auto single = make_shape(Rect(0, 0, 1, 1), make_color(0, 255, 0, 0.5));
    transaction.append(group, translucent, single);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 7);
    auto& group_layer = std::get<BeginLayerCommand>(list[0]);
    REQUIRE(group_layer.m_opacity == 0.5);
    REQUIRE(group_layer.m_blend_mode == BlendMode::NORMAL);
    REQUIRE(!group_layer.m_mask);
    auto& shape_layer = std::get<BeginLayerCommand>(list[1]);
    REQUIRE(shape_layer.m_opacity == 0.5);
    REQUIRE(get_color(get_fill(list, 2)) == make_color(255, 0, 0, 0.5));
    REQUIRE(get_color(get_fill(list, 3)) == make_color(0, 0, 255));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[4]));
    REQUIRE(get_color(get_fill(list, 5)) == make_color(0, 255, 0, 0.25));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[6]));
  }

  TEST_CASE("stroke") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto shape = make_shape(Rect(0, 0, 10, 10), make_color(255, 0, 0));
    auto stroke = Stroke(make_color(0, 0, 0), 2, StrokeAlignment::INSIDE,
      StrokeCap::ROUND, StrokeJoin::BEVEL, 3);
    shape.m_style.m_stroke = stroke;
    transaction.append(NodeId(), make_header(make_scale(2, 2)), shape);
    auto disabled = shape;
    disabled.m_style.m_stroke->m_is_enabled = false;
    transaction.append(NodeId(), make_header(), disabled);
    auto line = Shape();
    line.m_geometry = LineShape(Point(0, 0), Point(10, 0));
    line.m_style.m_fill = SolidFill(make_color(255, 0, 0));
    line.m_style.m_stroke = Stroke(make_color(0, 255, 0), 1);
    transaction.append(NodeId(), make_header(), line);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 4);
    REQUIRE(get_color(get_fill(list, 0)) == make_color(255, 0, 0));
    auto& outline = get_fill(list, 1);
    REQUIRE(*outline.m_path == outline_stroke(
      make_rectangle(Rect(0, 0, 10, 10)), StrokeGeometry(2,
        StrokeAlignment::INSIDE, StrokeCap::ROUND, StrokeJoin::BEVEL, 3, {},
        0), TOLERANCE / 2));
    REQUIRE(outline.m_transform == make_scale(2, 2));
    REQUIRE(get_color(outline) == make_color(0, 0, 0));
    REQUIRE(get_color(get_fill(list, 2)) == make_color(255, 0, 0));
    REQUIRE(get_color(get_fill(list, 3)) == make_color(0, 255, 0));
    REQUIRE(*get_fill(list, 3).m_path == outline_stroke(
      make_line(Point(0, 0), Point(10, 0)), StrokeGeometry(1,
        StrokeAlignment::CENTER, StrokeCap::BUTT, StrokeJoin::MITER, 4, {},
        0), TOLERANCE));
  }

  TEST_CASE("dashes") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, 10, 10));
    shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 1,
      StrokeAlignment::OUTSIDE, StrokeCap::BUTT, StrokeJoin::MITER, 4, {4, 2});
    transaction.append(NodeId(), make_header(), shape);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto& outline = *get_fill(list, 0).m_path;
    REQUIRE(contains(outline, Point(2, -0.5)));
    REQUIRE(contains(outline, Point(-0.5, 2)));
    REQUIRE(contains(outline, Point(10.5, 3)));
    REQUIRE(!contains(outline, Point(5, -0.5)));
    REQUIRE(!contains(outline, Point(2, 0.5)));
    REQUIRE(!contains(outline, Point(-0.5, -0.5)));
  }

  TEST_CASE("artboard") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 200, 50, 60);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    auto id = transaction.append(NodeId(), make_header(), artboard);
    auto child = transaction.append(id, make_header(make_translation(1, 2)),
      make_shape(Rect(0, 0, 100, 100), make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 2);
    auto& background = get_fill(list, 0);
    REQUIRE(*background.m_path == make_rectangle(Rect(0, 0, 50, 60)));
    REQUIRE(background.m_transform == make_translation(100, 200));
    REQUIRE(get_color(background) == make_color(255, 255, 255));
    auto& content = get_fill(list, 1);
    REQUIRE(content.m_transform == make_translation(101, 202));
    REQUIRE(content.m_clip == Rect(100, 200, 50, 60));
    auto single = builder.build(child);
    REQUIRE(single.size() == 1);
    REQUIRE(get_fill(single, 0).m_transform == make_translation(101, 202));
    REQUIRE(get_fill(single, 0).m_clip == Rect(100, 200, 50, 60));
    REQUIRE(builder.build(id).size() == 2);
  }

  TEST_CASE("reference") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto master = transaction.insert_root(make_header(), Group());
    auto source = transaction.append(master,
      make_header(make_translation(1, 1)),
      make_shape(Rect(0, 0, 4, 4), make_color(255, 0, 0)));
    auto translucent = make_header();
    translucent.m_opacity = 0.5;
    auto instance = transaction.append(
      NodeId(), make_header(make_translation(10, 10)), Group());
    transaction.append(instance, translucent, Reference(source));
    transaction.append(instance, make_header(),
      Reference(source, make_translation(5, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 2);
    REQUIRE(*get_fill(list, 0).m_path == make_rectangle(Rect(0, 0, 4, 4)));
    REQUIRE(get_fill(list, 0).m_transform == make_translation(11, 11));
    REQUIRE(get_color(get_fill(list, 0)) == make_color(255, 0, 0));
    REQUIRE(get_fill(list, 1).m_transform == make_translation(15, 10));
  }

  TEST_CASE("compound_shape") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto compound = Shape();
    auto result = make_rectangle(Rect(0, 0, 3, 3));
    compound.m_geometry =
      CompoundShape(CompoundOperation::SUBTRACT, Shared(result));
    compound.m_style.m_fill = SolidFill(make_color(0, 0, 255));
    auto id = transaction.append(NodeId(), make_header(), compound);
    transaction.append(id, make_header(),
      make_shape(Rect(0, 0, 5, 5), make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    REQUIRE(*get_fill(list, 0).m_path == result);
  }

  TEST_CASE("unsupported") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto gradient = make_shape(Rect(0, 0, 1, 1), make_color(0, 0, 0));
    gradient.m_style.m_fill = GradientFill();
    transaction.append(NodeId(), make_header(), gradient);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    REQUIRE(builder.build().empty());
  }

  TEST_CASE("edit") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto shape = make_shape(Rect(0, 0, 4, 4), make_color(255, 0, 0));
    shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 1);
    auto id = transaction->append(NodeId(), make_header(), shape);
    transaction->commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    REQUIRE(builder.build().size() == 2);
    transaction.emplace(document);
    shape.m_geometry = RectangleShape(Rect(0, 0, 8, 8));
    transaction->replace_payload(id, shape);
    transaction->commit();
    auto edited = builder.build();
    REQUIRE(edited.size() == 2);
    REQUIRE(*get_fill(edited, 0).m_path == make_rectangle(Rect(0, 0, 8, 8)));
    REQUIRE(get_bounds(*get_fill(edited, 1).m_path) == Rect(-0.5, -0.5, 9, 9));
  }
}

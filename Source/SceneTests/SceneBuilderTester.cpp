#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <QBuffer>
#include <QImage>
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

  ImageResource make_resource(QSize size) {
    auto image = QImage(size, QImage::Format_RGBA8888);
    image.fill(QColor(10, 20, 30));
    auto bytes = QByteArray();
    auto buffer = QBuffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    auto data = std::vector<std::byte>(bytes.size());
    std::memcpy(data.data(), bytes.constData(), bytes.size());
    auto resource = ImageResource();
    resource.m_id = generate_node_id();
    resource.m_media_type = "image/png";
    resource.m_data = Shared(std::move(data));
    return resource;
  }

  const FillCommand& get_fill(const DrawList& list, std::size_t index) {
    return std::get<FillCommand>(list.at(index));
  }

  const Color& get_color(const FillCommand& command) {
    return std::get<SolidPaint>(command.m_paint).m_color;
  }

  bool is_same(const std::vector<std::shared_ptr<const DrawList>>& lists,
      const DrawList& list) {
    auto index = std::size_t(0);
    for(auto& part : lists) {
      for(auto& command : *part) {
        if(index == list.size()) {
          return false;
        }
        auto left = std::get_if<FillCommand>(&command);
        auto right = std::get_if<FillCommand>(&list[index]);
        if(!left || !right || left->m_path != right->m_path ||
            left->m_transform != right->m_transform ||
            left->m_clip != right->m_clip ||
            get_color(*left) != get_color(*right)) {
          return false;
        }
        ++index;
      }
    }
    return index == list.size();
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
    auto& fill = get_fill(list, 0);
    REQUIRE(*fill.m_path == make_rectangle(Rect(1, 1, 8, 8)));
    REQUIRE(fill.m_transform == make_scale(2, 2));
    REQUIRE(get_color(fill) == make_color(255, 0, 0));
    auto& outline = get_fill(list, 1);
    REQUIRE(get_color(outline) == make_color(0, 0, 0));
    REQUIRE(outline.m_transform == make_scale(2, 2));
    REQUIRE(outline.m_stroke);
    REQUIRE(*outline.m_stroke->m_path == make_rectangle(Rect(1, 1, 8, 8)));
    REQUIRE(outline.m_stroke->m_width == 2);
    REQUIRE(outline.m_stroke->m_cap == StrokeCap::ROUND);
    REQUIRE(outline.m_stroke->m_join == StrokeJoin::BEVEL);
    REQUIRE(outline.m_stroke->m_miter_limit == 3);
    REQUIRE(contains(*outline.m_path, Point(0.5, 5)));
    REQUIRE(contains(*outline.m_path, Point(1.5, 5)));
    REQUIRE(!contains(*outline.m_path, Point(2.5, 5)));
    REQUIRE(!contains(*outline.m_path, Point(-0.5, 5)));
    REQUIRE(get_color(get_fill(list, 2)) == make_color(255, 0, 0));
    auto& line_stroke = get_fill(list, 3);
    REQUIRE(get_color(line_stroke) == make_color(0, 255, 0));
    REQUIRE(*line_stroke.m_path == outline_stroke(
      make_line(Point(0, 0), Point(10, 0)), StrokeGeometry(1,
        StrokeAlignment::CENTER, StrokeCap::BUTT, StrokeJoin::MITER, 4, {},
        0), TOLERANCE));
    REQUIRE(line_stroke.m_stroke);
    REQUIRE(*line_stroke.m_stroke->m_path ==
      make_line(Point(0, 0), Point(10, 0)));
    REQUIRE(line_stroke.m_stroke->m_width == 1);
    REQUIRE(line_stroke.m_stroke->m_cap == StrokeCap::BUTT);
  }

  TEST_CASE("outside_stroke") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto shape = make_shape(Rect(0, 0, 10, 10), make_color(255, 0, 0));
    shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2,
      StrokeAlignment::OUTSIDE, StrokeCap::BUTT, StrokeJoin::MITER, 4);
    transaction.append(NodeId(), make_header(), shape);
    auto translucent = shape;
    translucent.m_style.m_fill = SolidFill(make_color(255, 0, 0, 0.5));
    transaction.append(NodeId(), make_header(), translucent);
    auto faint = shape;
    faint.m_style.m_stroke->m_color = make_color(0, 0, 0, 0.5);
    transaction.append(NodeId(), make_header(), faint);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 6);
    auto& fill = get_fill(list, 0);
    REQUIRE(get_color(fill) == make_color(255, 0, 0));
    REQUIRE(*fill.m_path == make_rectangle(Rect(-1, -1, 12, 12)));
    auto& ring = get_fill(list, 1);
    REQUIRE(get_color(ring) == make_color(0, 0, 0));
    REQUIRE(*ring.m_stroke->m_path == make_rectangle(Rect(-1, -1, 12, 12)));
    REQUIRE(contains(*ring.m_path, Point(-1, 5)));
    REQUIRE(!contains(*ring.m_path, Point(5, 5)));
    REQUIRE(!contains(*ring.m_path, Point(-3, 5)));
    REQUIRE(get_color(get_fill(list, 2)) == make_color(255, 0, 0, 0.5));
    REQUIRE(*get_fill(list, 2).m_path == make_rectangle(Rect(-1, -1, 12, 12)));
    REQUIRE(*get_fill(list, 4).m_path == make_rectangle(Rect(0, 0, 10, 10)));
    REQUIRE(get_color(get_fill(list, 5)) == make_color(0, 0, 0, 0.5));
  }

  TEST_CASE("ellipse_stroke") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto shape = Shape();
    shape.m_geometry = EllipseShape(Point(5, 5), 5, 3);
    shape.m_style.m_fill = SolidFill(make_color(255, 0, 0));
    shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2,
      StrokeAlignment::INSIDE, StrokeCap::BUTT, StrokeJoin::MITER, 4);
    transaction.append(NodeId(), make_header(), shape);
    auto thick = shape;
    thick.m_style.m_stroke->m_width = 8;
    transaction.append(NodeId(), make_header(), thick);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 4);
    auto inset = make_ellipse(Point(5, 5), 4, 2);
    REQUIRE(*get_fill(list, 0).m_path == inset);
    REQUIRE(*get_fill(list, 1).m_stroke->m_path == inset);
    REQUIRE(*get_fill(list, 2).m_path == make_ellipse(Point(5, 5), 5, 3));
    REQUIRE(!get_fill(list, 2).m_stroke);
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

  TEST_CASE("scroll_group") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 100, 100);
    auto id = transaction.append(NodeId(), make_header(), artboard);
    auto add = [&] (ScrollDirection direction) {
      auto group = Group();
      group.m_scroll_group =
        ScrollGroup(direction, Size(30, 20), Point(5, 10));
      auto scroll =
        transaction.append(id, make_header(make_translation(10, 0)), group);
      return transaction.append(scroll, make_header(),
        make_shape(Rect(0, 0, 200, 200), make_color(255, 0, 0)));
    };
    auto vertical = add(ScrollDirection::VERTICAL);
    auto horizontal = add(ScrollDirection::HORIZONTAL);
    auto panning = add(ScrollDirection::PANNING);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 3);
    REQUIRE(get_fill(list, 0).m_clip == Rect(0, 10, 100, 20));
    REQUIRE(get_fill(list, 0).m_transform == make_translation(10, 0));
    REQUIRE(get_fill(list, 1).m_clip == Rect(15, 0, 30, 100));
    REQUIRE(get_fill(list, 2).m_clip == Rect(15, 10, 30, 20));
    auto single = builder.build(panning);
    REQUIRE(single.size() == 1);
    REQUIRE(get_fill(single, 0).m_clip == Rect(15, 10, 30, 20));
    REQUIRE(get_fill(builder.build(vertical), 0).m_clip ==
      Rect(0, 10, 100, 20));
    REQUIRE(get_fill(builder.build(horizontal), 0).m_clip ==
      Rect(15, 0, 30, 100));
  }

  TEST_CASE("rotated_scroll_group") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    group.m_scroll_group =
      ScrollGroup(ScrollDirection::PANNING, Size(30, 20), Point(5, 10));
    auto rotation = make_rotation(0.5);
    auto id = transaction.append(NodeId(), make_header(rotation), group);
    transaction.append(id, make_header(),
      make_shape(Rect(0, 0, 200, 200), make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 3);
    auto& layer = std::get<BeginLayerCommand>(list[0]);
    REQUIRE(layer.m_mask);
    REQUIRE(*layer.m_mask->m_path == make_rectangle(Rect(5, 10, 30, 20)));
    REQUIRE(layer.m_mask->m_transform == rotation);
    REQUIRE(layer.m_opacity == 1);
    REQUIRE(!get_fill(list, 1).m_clip);
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[2]));
  }

  TEST_CASE("transform_3d") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto parent = transaction.append(
      NodeId(), make_header(make_translation(100, 50)), Group());
    auto tilted = make_header(Matrix(1, 0, 0, 0.9, 10, 20));
    auto& transform = tilted.m_transform_3d.emplace();
    transform.m_matrix = std::array<double, 16>(
      {1, 0, 0, 10, 0, 0.9, 0.4, 20, 0, -0.4, 0.9, 8, 0, 0, 0, 1});
    transform.m_perspective_center = Point(20, 10);
    auto group = Group();
    group.m_scroll_group =
      ScrollGroup(ScrollDirection::PANNING, Size(30, 20), Point(0, 0));
    auto id = transaction.append(parent, tilted, group);
    auto shape = Shape();
    shape.m_geometry = RectangleShape(Rect(0, 0, 40, 20));
    auto gradient = GradientFill();
    gradient.m_geometry = LinearGradient(Point(0, 0), Point(1, 0));
    gradient.m_stops = {GradientStop(0, make_color(255, 0, 0)),
      GradientStop(1, make_color(0, 0, 255))};
    shape.m_style.m_fill = gradient;
    shape.m_style.m_effects.push_back(
      DropShadow(Point(0, 3), 2, make_color(0, 0, 0, 0.5), true));
    transaction.append(id, make_header(make_translation(2, 1)), shape);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 5);
    auto projection = make_translation(100, 50) *
      to_projection(transform, tilted.m_transform);
    auto& viewport = std::get<BeginLayerCommand>(list[0]);
    REQUIRE(viewport.m_mask);
    REQUIRE(*viewport.m_mask->m_path ==
      Rellu::transform(make_rectangle(Rect(0, 0, 30, 20)), projection));
    REQUIRE(viewport.m_mask->m_transform == Matrix());
    auto& shadow = std::get<BeginLayerCommand>(list[1]);
    REQUIRE(shadow.m_drop_shadows.size() == 1);
    auto jacobian = linearize(projection, Point(22, 11));
    REQUIRE(distance(shadow.m_drop_shadows[0].m_offset,
      Point(3 * jacobian.m_c, 3 * jacobian.m_d)) < 1e-9);
    auto& fill = get_fill(list, 2);
    auto shape_projection = projection * make_translation(2, 1);
    REQUIRE(*fill.m_path ==
      Rellu::transform(make_rectangle(Rect(0, 0, 40, 20)), shape_projection));
    REQUIRE(fill.m_transform == Matrix());
    REQUIRE(!fill.m_stroke);
    auto& paint = std::get<GradientPaint>(fill.m_paint);
    REQUIRE(paint.m_projection == shape_projection);
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[3]));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[4]));
  }

  TEST_CASE("repeat_grid") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = Group();
    auto grid = RepeatGrid();
    grid.m_cell_size = Size(10, 10);
    grid.m_row_count = 2;
    grid.m_size = Size(10, 15);
    group.m_repeat_grid = grid;
    auto id =
      transaction.append(NodeId(), make_header(make_translation(3, 4)), group);
    transaction.append(id, make_header(),
      make_shape(Rect(0, 0, 10, 10), make_color(255, 0, 0)));
    transaction.append(id, make_header(make_translation(0, 10)),
      make_shape(Rect(0, 0, 10, 10), make_color(0, 255, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 2);
    REQUIRE(get_fill(list, 0).m_clip == Rect(3, 4, 10, 15));
    REQUIRE(get_fill(list, 1).m_clip == Rect(3, 4, 10, 15));
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

  TEST_CASE("video") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto video = Shape();
    video.m_geometry = RectangleShape(Rect(0, 0, 64, 48));
    auto poster = ImageFill();
    poster.m_image = transaction.add_image(make_resource(QSize(8, 6)));
    poster.m_size = Size(8, 6);
    poster.m_scale_behavior = ScaleBehavior::COVER;
    video.m_video = Video(poster, generate_node_id(), false);
    transaction.append(NodeId(), make_header(), video);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto& fill = get_fill(list, 0);
    REQUIRE(*fill.m_path == make_rectangle(Rect(0, 0, 64, 48)));
    REQUIRE(std::holds_alternative<ImagePaint>(fill.m_paint));
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

  TEST_CASE("build_lists") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto group = transaction->append(
      NodeId(), make_header(make_translation(5, 5)), Group());
    auto children = std::vector<NodeId>();
    for(auto i = 0; i != 600; ++i) {
      children.push_back(transaction->append(group,
        make_header(make_translation(i % 30 * 10, i / 30 * 10)),
        make_shape(Rect(0, 0, 8, 8), make_color(i % 256, 0, 0))));
    }
    transaction->commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto dependencies = SceneDependencies();
    auto lists = builder.build_lists(group, dependencies);
    REQUIRE(lists.size() > 1);
    REQUIRE(is_same(lists, builder.build(group)));
    REQUIRE(builder.build_lists(group, dependencies) == lists);
    transaction.emplace(document);
    transaction->replace_payload(children[300],
      make_shape(Rect(0, 0, 8, 8), make_color(0, 255, 0)));
    transaction->commit();
    builder.invalidate(children[300]);
    auto edited = builder.build_lists(group, dependencies);
    REQUIRE(edited.size() == lists.size());
    REQUIRE(is_same(edited, builder.build(group)));
    auto changed = 0;
    for(auto i = std::size_t(0); i != lists.size(); ++i) {
      if(edited[i] != lists[i]) {
        ++changed;
      }
    }
    REQUIRE(changed == 1);
  }

  TEST_CASE("invalidate_transform") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 400, 400);
    auto board = transaction->append(NodeId(), make_header(), artboard);
    auto group = transaction->append(
      board, make_header(make_translation(5, 5)), Group());
    auto scroll = Group();
    scroll.m_scroll_group =
      ScrollGroup(ScrollDirection::VERTICAL, Size(100, 50), Point(0, 0));
    auto scrolled = transaction->append(
      group, make_header(make_translation(0, 20)), scroll);
    for(auto i = 0; i != 600; ++i) {
      transaction->append(i % 2 == 0 ? group : scrolled,
        make_header(make_translation(i % 30 * 10, i / 30 * 10)),
        make_shape(Rect(0, 0, 8, 8), make_color(i % 256, 0, 0)));
    }
    transaction->commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto dependencies = SceneDependencies();
    builder.build_lists(board, dependencies);
    auto move = [&] (const Matrix& transform) {
      auto header = document.get(group).m_header;
      header.m_transform = transform;
      transaction.emplace(document);
      transaction->replace_header(header);
      transaction->commit();
      builder.invalidate_transform(group);
      auto lists = builder.build_lists(board, dependencies);
      REQUIRE(is_same(lists, builder.build(board)));
    };
    move(make_translation(8, 9));
    move(make_translation(8, -30));
    move(make_translation(-250, -30));
    move(make_translation(-250, -30) * make_scale(2, 2));
    move(make_translation(7.5, 9.25));
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

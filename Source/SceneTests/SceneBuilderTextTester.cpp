#include <chrono>
#include <optional>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"
#include "Rellu/Text/FontLibrary.hpp"

using namespace Rellu;

namespace {
  const auto TOLERANCE = 1.0 / 32;

  NodeHeader make_header(const Matrix& transform) {
    auto header = NodeHeader();
    header.m_transform = transform;
    return header;
  }

  Text make_text(Color color) {
    auto text = Text();
    text.m_content = Shared(std::u16string(u"Text"));
    text.m_base_style.m_font = Font("Arial", "Regular", "ArialMT", 16);
    text.m_base_style.m_color = color;
    return text;
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
  TEST_CASE("text") {
    auto document = Document();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), make_header(make_translation(5, 30)),
      make_text(make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto& fill = get_fill(list, 0);
    REQUIRE(fill.m_transform == make_translation(5, 30));
    REQUIRE(get_color(fill) == make_color(255, 0, 0));
    auto bounds = get_bounds(*fill.m_path);
    REQUIRE(bounds.get_left() >= 0);
    REQUIRE(bounds.get_bottom() <= 0.5);
    REQUIRE(bounds.get_top() < -10);
    REQUIRE(!fill.m_clip);
  }

  TEST_CASE("flipped_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(make_color(255, 0, 0));
    text.m_is_flipped_vertically = true;
    transaction.append(NodeId(), make_header(make_translation(5, 30)), text);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto& fill = get_fill(list, 0);
    auto bounds = measure_text(text, get_default_font_library());
    REQUIRE(fill.m_transform == make_translation(5, 30) *
      Matrix(1, 0, 0, -1, 0, 2 * bounds.m_y + bounds.m_height));
    auto drawn = get_bounds(transform(*fill.m_path, fill.m_transform));
    REQUIRE(drawn.get_top() >= 30 + bounds.get_top() - 0.5);
    REQUIRE(drawn.get_bottom() <= 30 + bounds.get_bottom() + 0.5);
  }

  TEST_CASE("edited_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(make_color(255, 0, 0));
    text.m_is_flipped_vertically = true;
    auto flip = Matrix(-1, 0, 0, -1, 80, 30);
    auto id = transaction.append(NodeId(), make_header(flip), text);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto dependencies = SceneDependencies();
    auto lists = builder.build_lists(id, dependencies);
    REQUIRE(builder.get_edited_text().is_nil());
    auto bounds = measure_text(text, get_default_font_library());
    auto mirror = Matrix(1, 0, 0, -1, 0, 2 * bounds.m_y + bounds.m_height);
    REQUIRE(get_fill(*lists.at(0), 0).m_transform == flip * mirror);
    builder.set_edited_text(id);
    REQUIRE(builder.get_edited_text() == id);
    lists = builder.build_lists(id, dependencies);
    REQUIRE(get_fill(*lists.at(0), 0).m_transform == flip);
    REQUIRE(get_fill(builder.build(), 0).m_transform == flip);
    builder.set_edited_text(NodeId());
    lists = builder.build_lists(id, dependencies);
    REQUIRE(get_fill(*lists.at(0), 0).m_transform == flip * mirror);
  }

  TEST_CASE("rasterized_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), make_header(make_translation(5, 30)),
      make_text(make_color(0, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto device = make_scale(2, 2) * make_translation(-1, -2);
    builder.set_device_transform(device);
    REQUIRE(builder.get_device_transform() == device);
    auto list = builder.build();
    REQUIRE(list.size() > 1);
    REQUIRE(list.size() <= 16);
    for(auto& command : list) {
      auto& fill = std::get<FillCommand>(command);
      REQUIRE(fill.m_transform == *invert(device));
      REQUIRE(get_color(fill).m_alpha > 0);
      REQUIRE(get_color(fill).m_alpha <= 1);
      for(auto& segment : fill.m_path->get_segments()) {
        for(auto& point : segment.m_points) {
          REQUIRE(point.m_x == static_cast<int>(point.m_x));
          REQUIRE(point.m_y == static_cast<int>(point.m_y));
        }
      }
    }
    builder.set_device_transform(std::nullopt);
    REQUIRE(builder.build().size() == 1);
  }

  TEST_CASE("text_transform") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto shapes = transaction.append(
      NodeId(), make_header(make_translation(0.1, 0)), Group());
    transaction.append(
      shapes, make_header(Matrix()),
      make_shape(Rect(0, 0, 4, 4), Color(1, 0, 0, 1)));
    auto texts = transaction.append(
      NodeId(), make_header(make_translation(5, 30)), Group());
    transaction.append(
      texts, make_header(Matrix()), make_text(make_color(0, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto device = make_scale(2, 2);
    builder.set_text_transform(device);
    REQUIRE(builder.get_text_transform() == device);
    REQUIRE(!builder.get_device_transform());
    auto dependencies = SceneDependencies();
    auto shape_lists = builder.build_lists(shapes, dependencies);
    REQUIRE(!dependencies.m_is_device_dependent);
    REQUIRE(get_fill(*shape_lists.at(0), 0).m_transform ==
      make_translation(0.1, 0));
    auto text_lists = builder.build_lists(texts, dependencies);
    REQUIRE(dependencies.m_is_device_dependent);
    REQUIRE(dependencies.m_device_transform == device);
    REQUIRE(text_lists.at(0)->size() > 1);
    for(auto& command : *text_lists.at(0)) {
      REQUIRE(std::get<FillCommand>(command).m_transform == *invert(device));
    }
    REQUIRE(builder.build_lists(texts, dependencies) == text_lists);
    builder.set_text_transform(make_scale(3, 3));
    REQUIRE(builder.build_lists(shapes, dependencies) == shape_lists);
    REQUIRE(builder.build_lists(texts, dependencies) != text_lists);
    REQUIRE(dependencies.m_device_transform == make_scale(3, 3));
    builder.set_text_transform(std::nullopt);
    text_lists = builder.build_lists(texts, dependencies);
    REQUIRE(dependencies.m_is_device_dependent);
    REQUIRE(!dependencies.m_device_transform);
    REQUIRE(text_lists.at(0)->size() == 1);
    REQUIRE(get_fill(*text_lists.at(0), 0).m_transform ==
      make_translation(5, 30));
  }

  TEST_CASE("deferred_text") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto group = transaction->append(
      NodeId(), make_header(make_translation(5, 30)), Group());
    auto text = transaction->append(
      group, make_header(Matrix()), make_text(make_color(0, 0, 0)));
    transaction->commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto device = make_scale(2, 2);
    builder.set_text_transform(device);
    REQUIRE(!builder.is_text_deferred());
    builder.set_text_deferred(true);
    REQUIRE(builder.is_text_deferred());
    auto dependencies = SceneDependencies();
    auto lists = builder.build_lists(group, dependencies);
    REQUIRE(lists.at(0)->size() == 1);
    REQUIRE(get_fill(*lists.at(0), 0).m_transform ==
      make_translation(5, 30));
    REQUIRE(dependencies.m_is_device_dependent);
    REQUIRE(dependencies.m_device_transform == device);
    REQUIRE(!dependencies.m_is_rasterized);
    REQUIRE(builder.has_pending_text());
    REQUIRE(builder.rasterize_text(std::chrono::hours(1)) ==
      std::vector{text});
    REQUIRE(!builder.has_pending_text());
    builder.invalidate(text);
    lists = builder.build_lists(group, dependencies);
    REQUIRE(lists.at(0)->size() > 1);
    REQUIRE(dependencies.m_is_rasterized);
    auto move = [&] (const Matrix& transform) {
      auto header = document.get(group).m_header;
      header.m_transform = transform;
      transaction.emplace(document);
      transaction->replace_header(header);
      transaction->commit();
      builder.invalidate_transform(group);
      return builder.build_lists(group, dependencies);
    };
    lists = move(make_translation(8.3, 32));
    REQUIRE(lists.at(0)->size() == 1);
    REQUIRE(get_fill(*lists.at(0), 0).m_transform ==
      make_translation(8.3, 32));
    lists = move(make_translation(9.1, 33));
    REQUIRE(get_fill(*lists.at(0), 0).m_transform ==
      make_translation(9.1, 33));
    REQUIRE(builder.has_pending_text());
    while(builder.has_pending_text()) {
      for(auto& id : builder.rasterize_text(std::chrono::hours(1))) {
        builder.invalidate(id);
      }
      lists = builder.build_lists(group, dependencies);
    }
    REQUIRE(lists.at(0)->size() > 1);
    auto rasterized = SceneBuilder(document, TOLERANCE);
    rasterized.set_text_transform(device);
    REQUIRE(rasterized.build_lists(group, dependencies).at(0)->size() ==
      lists.at(0)->size());
  }

  TEST_CASE("late_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(NodeId(), make_header(Matrix()), Group());
    for(auto i = 0; i != 3; ++i) {
      auto child = transaction.append(
        group, make_header(make_translation(0, 30 * i)), Group());
      transaction.append(
        child, make_header(Matrix()), make_text(make_color(0, 0, 0)));
    }
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    builder.set_text_transform(make_scale(2, 2));
    auto dependencies = SceneDependencies();
    auto lists = builder.build_lists(group, dependencies);
    REQUIRE(!dependencies.m_is_device_mixed);
    builder.set_text_transform(make_scale(3, 3));
    auto late = builder.build_lists(
      group, dependencies, std::chrono::steady_clock::now());
    REQUIRE(late == lists);
    REQUIRE(dependencies.m_device_transform == make_scale(2, 2));
    REQUIRE(!dependencies.m_is_device_mixed);
    auto current = builder.build_lists(group, dependencies);
    REQUIRE(current != lists);
    REQUIRE(dependencies.m_device_transform == make_scale(3, 3));
    REQUIRE(!dependencies.m_is_device_mixed);
  }

  TEST_CASE("outlined_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    transaction.append(NodeId(), make_header(make_translation(5, 30)),
      make_text(make_color(255, 0, 0)));
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    builder.set_device_transform(make_scale(20, 20));
    auto list = builder.build();
    REQUIRE(list.size() == 4);
    for(auto i = std::size_t(0); i != list.size(); ++i) {
      auto& fill = get_fill(list, i);
      REQUIRE(fill.m_transform == make_translation(5, 30));
      REQUIRE(get_color(fill) == make_color(255, 0, 0));
    }
    REQUIRE(get_bounds(*get_fill(list, 0).m_path).get_right() <
      get_bounds(*get_fill(list, 3).m_path).get_left());
  }

  TEST_CASE("single_precision_text") {
    auto build = [] (double y) {
      auto document = Document();
      auto transaction = Transaction(document);
      transaction.append(NodeId(), make_header(make_translation(0, y)),
        make_text(make_color(0, 0, 0)));
      transaction.commit();
      auto builder = SceneBuilder(document, TOLERANCE);
      builder.set_device_transform(make_translation(0, -4455.5));
      auto paths = std::vector<Path>();
      for(auto& command : builder.build()) {
        paths.push_back(*std::get<FillCommand>(command).m_path);
      }
      return paths;
    };
    REQUIRE(build(4567.999889373779) == build(4568));
    REQUIRE(build(4567.9) != build(4568));
  }

  TEST_CASE("text_effects") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(make_color(0, 0, 0));
    auto shadow = DropShadow();
    shadow.m_offset = Point(0, 2);
    shadow.m_blur = 3;
    shadow.m_color = Color(0, 0, 0, 0.5);
    text.m_style.m_effects.push_back(shadow);
    auto inner = InnerShadow();
    inner.m_offset = Point(1, 0);
    inner.m_blur = 1;
    inner.m_color = Color(1, 0, 0, 1);
    text.m_style.m_effects.push_back(inner);
    transaction.append(NodeId(), make_header(make_scale(2, 2)), text);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 5);
    auto& outer = std::get<BeginLayerCommand>(list[0]);
    REQUIRE(outer.m_drop_shadows.size() == 1);
    REQUIRE(outer.m_drop_shadows[0].m_offset == Point(0, 4));
    REQUIRE(outer.m_drop_shadows[0].m_radius == 6);
    REQUIRE(outer.m_inner_shadows.empty());
    auto& layer = std::get<BeginLayerCommand>(list[1]);
    REQUIRE(layer.m_inner_shadows.size() == 1);
    REQUIRE(layer.m_inner_shadows[0].m_offset == Point(2, 0));
    REQUIRE(layer.m_drop_shadows.empty());
    REQUIRE(std::holds_alternative<FillCommand>(list[2]));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[3]));
    REQUIRE(std::holds_alternative<EndLayerCommand>(list[4]));
  }

  TEST_CASE("area_text") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(make_color(0, 0, 0));
    text.m_frame = AreaFrame(Size(20, 10));
    transaction.append(NodeId(), make_header(make_translation(5, 6)), text);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    REQUIRE(!get_fill(list, 0).m_clip);
  }

  TEST_CASE("edited_overflow") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto text = make_text(make_color(0, 0, 0));
    text.m_content = Shared(std::u16string(u"ab cd ef"));
    text.m_frame = AreaFrame(Size(10, 10));
    auto id = transaction.append(NodeId(), make_header(Matrix()), text);
    transaction.commit();
    auto builder = SceneBuilder(document, TOLERANCE);
    auto list = builder.build();
    REQUIRE(list.size() == 1);
    auto bottom = get_bounds(*get_fill(list, 0).m_path).get_bottom();
    REQUIRE(bottom > 10);
    builder.set_edited_text(id);
    list = builder.build();
    REQUIRE(get_bounds(*get_fill(list, 0).m_path).get_bottom() > bottom + 20);
  }
}

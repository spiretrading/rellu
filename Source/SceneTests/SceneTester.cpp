#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Scene/Scene.hpp"

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

  Artboard make_artboard(const Rect& frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    return artboard;
  }

  Text make_text(const std::u16string& content) {
    auto text = Text();
    text.m_content = Shared(content);
    text.m_base_style.m_font = Font("Arial", "Regular", "ArialMT", 16);
    text.m_base_style.m_color = make_color(0, 0, 0);
    return text;
  }

  bool is_equal(const Paint& left, const Paint& right) {
    if(left.index() != right.index()) {
      return false;
    } else if(auto solid = std::get_if<SolidPaint>(&left)) {
      return solid->m_color == std::get<SolidPaint>(right).m_color;
    } else if(auto gradient = std::get_if<GradientPaint>(&left)) {
      auto& other = std::get<GradientPaint>(right);
      return gradient->m_geometry == other.m_geometry &&
        gradient->m_stops == other.m_stops &&
        gradient->m_transform == other.m_transform &&
        gradient->m_projection == other.m_projection;
    }
    auto& image = std::get<ImagePaint>(left);
    auto& other = std::get<ImagePaint>(right);
    return image.m_image == other.m_image &&
      image.m_transform == other.m_transform &&
      image.m_opacity == other.m_opacity &&
      image.m_projection == other.m_projection;
  }

  bool is_equal(const DrawCommand& left, const DrawCommand& right) {
    if(left.index() != right.index()) {
      return false;
    } else if(auto fill = std::get_if<FillCommand>(&left)) {
      auto& other = std::get<FillCommand>(right);
      return *fill->m_path == *other.m_path &&
        fill->m_transform == other.m_transform &&
        is_equal(fill->m_paint, other.m_paint) &&
        fill->m_clip == other.m_clip;
    } else if(auto layer = std::get_if<BeginLayerCommand>(&left)) {
      auto& other = std::get<BeginLayerCommand>(right);
      return layer->m_opacity == other.m_opacity &&
        layer->m_blend_mode == other.m_blend_mode &&
        static_cast<bool>(layer->m_mask) ==
          static_cast<bool>(other.m_mask) &&
        (!layer->m_mask || (*layer->m_mask->m_path ==
          *other.m_mask->m_path &&
          layer->m_mask->m_transform == other.m_mask->m_transform)) &&
        layer->m_inner_shadows.size() == other.m_inner_shadows.size() &&
        layer->m_drop_shadows.size() == other.m_drop_shadows.size() &&
        layer->m_blur == other.m_blur && layer->m_clip == other.m_clip;
    } else if(auto blur = std::get_if<BackdropBlurCommand>(&left)) {
      auto& other = std::get<BackdropBlurCommand>(right);
      return *blur->m_path == *other.m_path &&
        blur->m_transform == other.m_transform &&
        blur->m_radius == other.m_radius &&
        blur->m_brightness == other.m_brightness &&
        blur->m_clip == other.m_clip;
    }
    return true;
  }

  bool is_equal(const DrawList& left, const DrawList& right) {
    if(left.size() != right.size()) {
      return false;
    }
    for(auto i = std::size_t(0); i != left.size(); ++i) {
      if(!is_equal(left[i], right[i])) {
        return false;
      }
    }
    return true;
  }

  DrawList flatten(const Scene& scene) {
    auto list = DrawList();
    for(auto& fragment : scene.get_drawing()->get_lists()) {
      list.insert(list.end(), fragment->begin(), fragment->end());
    }
    return list;
  }

  bool is_rebuilt_equal(const Document& document, const Scene& scene) {
    auto builder = SceneBuilder(document, scene.get_tolerance());
    return is_equal(flatten(scene), builder.build());
  }

  bool is_touching(const std::vector<DrawRegion>& regions, const Rect& rect) {
    for(auto& region : regions) {
      if(inflate(region.m_bounds, 1).intersects(rect)) {
        return true;
      }
    }
    return false;
  }
}

TEST_SUITE("Scene") {
  TEST_CASE("build") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto first = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 100, 100)));
    transaction.append(first, make_header(make_translation(10, 10)),
      make_shape(Rect(0, 0, 20, 20), make_color(255, 0, 0)));
    transaction.append(NodeId(), make_header(make_translation(200, 0)),
      make_shape(Rect(0, 0, 30, 30), make_color(0, 0, 255)));
    transaction.commit();
    auto scene = Scene(document, TOLERANCE);
    REQUIRE(!scene.is_current());
    scene.update();
    REQUIRE(scene.is_current());
    REQUIRE(scene.get_drawing()->get_lists().size() == 2);
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(scene.get_drawing()->get_bounds() == Rect(0, 0, 230, 100));
    REQUIRE(scene.update().empty());
  }

  TEST_CASE("edit") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto first = transaction->append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 100, 100)));
    transaction->append(first, make_header(make_translation(10, 10)),
      make_shape(Rect(0, 0, 20, 20), make_color(255, 0, 0)));
    auto second = transaction->append(
      NodeId(), make_header(), make_artboard(Rect(200, 0, 100, 100)));
    transaction->append(second, make_header(make_translation(10, 10)),
      make_shape(Rect(0, 0, 20, 20), make_color(0, 255, 0)));
    auto shape = transaction->append(second,
      make_header(make_translation(50, 50)),
      make_shape(Rect(0, 0, 10, 10), make_color(0, 0, 255)));
    transaction->commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    auto lists = scene.get_drawing()->get_lists();
    transaction.emplace(document);
    transaction->replace_payload(
      shape, make_shape(Rect(0, 0, 10, 10), make_color(255, 255, 0)));
    transaction->commit();
    REQUIRE(!scene.is_current());
    auto damage = scene.update();
    REQUIRE(scene.is_current());
    REQUIRE(is_rebuilt_equal(document, scene));
    auto& edited = scene.get_drawing()->get_lists();
    REQUIRE(edited[0] == lists[0]);
    REQUIRE(edited[1] != lists[1]);
    REQUIRE(is_touching(damage, Rect(250, 50, 10, 10)));
    REQUIRE(!is_touching(damage, Rect(0, 0, 100, 100)));
    REQUIRE(!is_touching(damage, Rect(210, 10, 20, 20)));
    document.undo();
    scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
  }

  TEST_CASE("reference_source") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto master = transaction->insert_root(make_header(), Group());
    auto source = transaction->append(master,
      make_header(make_translation(1, 1)),
      make_shape(Rect(0, 0, 4, 4), make_color(255, 0, 0)));
    auto instance = transaction->append(
      NodeId(), make_header(make_translation(10, 10)), Group());
    transaction->append(instance, make_header(), Reference(source));
    transaction->append(NodeId(), make_header(make_translation(100, 100)),
      make_shape(Rect(0, 0, 4, 4), make_color(0, 255, 0)));
    transaction->commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    auto lists = scene.get_drawing()->get_lists();
    transaction.emplace(document);
    transaction->replace_payload(
      source, make_shape(Rect(0, 0, 8, 8), make_color(0, 0, 255)));
    transaction->commit();
    auto damage = scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    auto& edited = scene.get_drawing()->get_lists();
    REQUIRE(edited[0] != lists[0]);
    REQUIRE(edited[1] == lists[1]);
    REQUIRE(is_touching(damage, Rect(11, 11, 8, 8)));
    transaction.emplace(document);
    transaction->append(master, make_header(make_translation(2, 2)),
      make_shape(Rect(0, 0, 1, 1), make_color(0, 0, 0)));
    transaction->commit();
    scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
  }

  TEST_CASE("scroll_group") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto group = Group();
    group.m_scroll_group =
      ScrollGroup(ScrollDirection::PANNING, Size(5, 5), Point(0, 0));
    auto master = transaction->insert_root(make_header(), Group());
    auto source = transaction->append(master, make_header(), group);
    transaction->append(source, make_header(),
      make_shape(Rect(0, 0, 20, 20), make_color(255, 0, 0)));
    auto instance = transaction->append(
      NodeId(), make_header(make_translation(10, 10)), Group());
    transaction->append(instance, make_header(), Reference(source));
    transaction->commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(scene.get_drawing()->get_bounds() == Rect(10, 10, 5, 5));
    transaction.emplace(document);
    group.m_scroll_group->m_viewport = Size(8, 6);
    transaction->replace_payload(source, group);
    transaction->commit();
    auto damage = scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(scene.get_drawing()->get_bounds() == Rect(10, 10, 8, 6));
    REQUIRE(is_touching(damage, Rect(16, 11, 1, 4)));
  }

  TEST_CASE("broken_reference") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto master = transaction->insert_root(make_header(), Group());
    auto source = transaction->append(master, make_header(),
      make_shape(Rect(0, 0, 4, 4), make_color(255, 0, 0)));
    auto instance = transaction->append(NodeId(), make_header(), Group());
    transaction->append(instance, make_header(), Reference(source));
    transaction->commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    transaction.emplace(document);
    transaction->remove(source);
    transaction->commit();
    scene.update();
    REQUIRE(flatten(scene).empty());
    document.undo();
    scene.update();
    REQUIRE(flatten(scene).size() == 1);
    REQUIRE(is_rebuilt_equal(document, scene));
  }

  TEST_CASE("text") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto text = transaction->append(NodeId(),
      make_header(make_translation(5, 30)), make_text(u"Text"));
    transaction->append(NodeId(), make_header(make_translation(100, 0)),
      make_shape(Rect(0, 0, 4, 4), make_color(255, 0, 0)));
    transaction->commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    auto lists = scene.get_drawing()->get_lists();
    transaction.emplace(document);
    transaction->replace_payload(text, make_text(u"Longer text"));
    transaction->commit();
    scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    auto& edited = scene.get_drawing()->get_lists();
    REQUIRE(edited[0] != lists[0]);
    REQUIRE(edited[1] == lists[1]);
  }

  TEST_CASE("canvas") {
    auto document = Document();
    auto transaction = std::optional<Transaction>(std::in_place, document);
    auto first = transaction->append(NodeId(), make_header(),
      make_shape(Rect(0, 0, 4, 4), make_color(255, 0, 0)));
    auto second = transaction->append(NodeId(), make_header(),
      make_shape(Rect(10, 0, 4, 4), make_color(0, 255, 0)));
    auto group = transaction->append(NodeId(), make_header(), Group());
    transaction->commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    transaction.emplace(document);
    transaction->move(first, NodeId(), 2);
    transaction->commit();
    auto damage = scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(is_touching(damage, Rect(0, 0, 4, 4)));
    transaction.emplace(document);
    transaction->move(second, group, 0);
    transaction->append(NodeId(), make_header(),
      make_shape(Rect(20, 0, 4, 4), make_color(0, 0, 255)));
    transaction->remove(first);
    transaction->commit();
    damage = scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(is_touching(damage, Rect(0, 0, 4, 4)));
    REQUIRE(is_touching(damage, Rect(20, 0, 4, 4)));
  }

  TEST_CASE("tolerance") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto stroked = make_shape(Rect(0, 0, 10, 10), make_color(255, 0, 0));
    stroked.m_geometry = EllipseShape(Point(5, 5), 5, 5);
    stroked.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
    transaction.append(NodeId(), make_header(), stroked);
    transaction.append(NodeId(), make_header(),
      make_shape(Rect(20, 0, 10, 10), make_color(0, 255, 0)));
    transaction.commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    auto lists = scene.get_drawing()->get_lists();
    scene.set_tolerance(TOLERANCE * 2);
    REQUIRE(scene.is_current());
    scene.set_tolerance(TOLERANCE / 8);
    REQUIRE(!scene.is_current());
    scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    auto& refined = scene.get_drawing()->get_lists();
    REQUIRE(refined[0] != lists[0]);
    REQUIRE(refined[1] == lists[1]);
  }

  TEST_CASE("budget") {
    auto document = Document();
    auto transaction = Transaction(document);
    for(auto i = 0; i != 3; ++i) {
      auto shape = make_shape(Rect(0, 0, 10, 10), make_color(255, 0, 0));
      shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 2);
      transaction.append(
        NodeId(), make_header(make_translation(100 * i, 0)), shape);
    }
    transaction.commit();
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    auto lists = scene.get_drawing()->get_lists();
    scene.set_tolerance(TOLERANCE / 8);
    scene.update(Rect(195, 0, 20, 20), std::chrono::nanoseconds(0));
    REQUIRE(!scene.is_current());
    auto& partial = scene.get_drawing()->get_lists();
    REQUIRE(partial[0] == lists[0]);
    REQUIRE(partial[1] == lists[1]);
    REQUIRE(partial[2] != lists[2]);
    scene.update();
    REQUIRE(scene.is_current());
    REQUIRE(is_rebuilt_equal(document, scene));
  }
}

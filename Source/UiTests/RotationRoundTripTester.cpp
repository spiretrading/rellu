#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QJsonArray>
#include <QJsonObject>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/History.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  struct Scene {
    NodeId m_artboard;
    NodeId m_turned;
    NodeId m_snapped;
    NodeId m_flipped;
    NodeId m_inverted;
    NodeId m_wrapped;
    NodeId m_group;
    NodeId m_first;
    NodeId m_second;
  };

  NodeHeader make_header(
      std::string name, const Matrix& transform, double rotation) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_has_custom_name = true;
    header.m_transform = transform;
    header.m_rotation = rotation;
    return header;
  }

  Shape make_rectangle(double width, double height) {
    auto shape = Shape();
    shape.m_geometry =
      RectangleShape(Rect(0, 0, width, height), CornerRadii(0, 0, 0, 0));
    shape.m_style.m_fill = SolidFill(make_color(255, 255, 255));
    return shape;
  }

  Scene build(Document& document) {
    auto scene = Scene();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(0, 0, 1920, 1080);
    scene.m_artboard = transaction.append(
      NodeId(), make_header("Web 1920 - 1", Matrix(), 0), artboard);
    scene.m_turned = transaction.append(scene.m_artboard,
      make_header("RA", make_translation(200, 200), 0),
      make_rectangle(200, 100));
    scene.m_snapped = transaction.append(scene.m_artboard,
      make_header("RB", Matrix(0.984807753012208, 0.17364817766693033,
        -0.17364817766693033, 0.984807753012208, 610.2016335821257,
        183.39479458269656), 10), make_rectangle(200, 100));
    scene.m_flipped = transaction.append(scene.m_artboard,
      make_header("RC", Matrix(0.8660254037844387, -0.49999999999999994,
        0.49999999999999994, 0.8660254037844387, 988.3974596215561,
        256.69872981077805), -30), make_rectangle(200, 100));
    scene.m_inverted = transaction.append(scene.m_artboard,
      make_header("RD", Matrix(0.8660254037844387, 0.49999999999999994,
        -0.49999999999999994, 0.8660254037844387, 1038.3974596215562,
        606.6987298107781), 30), make_rectangle(200, 100));
    scene.m_wrapped = transaction.append(scene.m_artboard,
      make_header("RX", make_translation(1550, 225), 0),
      make_rectangle(100, 50));
    scene.m_group = transaction.append(scene.m_artboard,
      make_header("G30", Matrix(0.8660254037844387, 0.49999999999999994,
        -0.49999999999999994, 0.8660254037844387, 295.0961894323342,
        581.6987298107781), 30), Group());
    scene.m_first = transaction.append(scene.m_group,
      make_header("GA", Matrix(), 0), make_rectangle(100, 100));
    scene.m_second = transaction.append(scene.m_group,
      make_header("GB", make_translation(200, 0), 0),
      make_rectangle(100, 100));
    transaction.commit();
    document.clear_history();
    return scene;
  }

  QJsonObject find_node(const QJsonValue& value, const NodeId& id) {
    if(value.isObject()) {
      auto object = value.toObject();
      if(object.value("id"_L1).toString().toStdString() == to_string(id)) {
        return object;
      }
      for(auto member : object) {
        if(auto found = find_node(member, id); !found.isEmpty()) {
          return found;
        }
      }
    } else if(value.isArray()) {
      for(auto element : value.toArray()) {
        if(auto found = find_node(element, id); !found.isEmpty()) {
          return found;
        }
      }
    }
    return QJsonObject();
  }

  QJsonObject find_written(const Document& document, const NodeId& id) {
    auto package = XdPackage(write_xd(document));
    for(auto& entry : package.get_entries()) {
      if(entry.ends_with(".agc")) {
        if(auto node = find_node(package.read_json(entry), id);
            !node.isEmpty()) {
          return node;
        }
      }
    }
    return QJsonObject();
  }

  QJsonObject get_ux(const QJsonObject& node) {
    return node.value("meta"_L1).toObject().value("ux"_L1).toObject();
  }

  void require_transform(const QJsonObject& node, const Matrix& expected) {
    auto transform = node.value("transform"_L1).toObject();
    REQUIRE(transform.value("a"_L1).toDouble() ==
      doctest::Approx(expected.m_a).epsilon(1e-6));
    REQUIRE(transform.value("b"_L1).toDouble() ==
      doctest::Approx(expected.m_b).epsilon(1e-6));
    REQUIRE(transform.value("c"_L1).toDouble() ==
      doctest::Approx(expected.m_c).epsilon(1e-6));
    REQUIRE(transform.value("d"_L1).toDouble() ==
      doctest::Approx(expected.m_d).epsilon(1e-6));
    REQUIRE(transform.value("tx"_L1).toDouble() ==
      doctest::Approx(expected.m_tx).epsilon(1e-6));
    REQUIRE(transform.value("ty"_L1).toDouble() ==
      doctest::Approx(expected.m_ty).epsilon(1e-6));
  }

  Point get_center(const Document& document, const NodeId& id) {
    return get_world_transform(document, id).map(get_local_bounds(
      document, id, BoundsType::GEOMETRY, estimate_text_bounds)->get_center());
  }

  void drag_rotate(Document& document, const NodeId& id,
      const std::vector<double>& angles, bool is_snapped) {
    auto& header = document.get(id).m_header;
    auto drag = RotationDrag(std::vector{header.m_rotation},
      std::vector{get_rotation(header.m_transform)});
    for(auto& angle : angles) {
      drag.update(angle, is_snapped);
    }
    auto history = History(document);
    history.execute(make_rotate_command(std::vector{id},
      drag.get_turn() * std::numbers::pi / 180, get_center(document, id),
      drag.get_rotations()));
  }
}

TEST_SUITE("RotationRoundTrip") {
  TEST_CASE("rotate_drag") {
    auto document = Document();
    auto scene = build(document);
    drag_rotate(document, scene.m_turned, {37.4}, false);
    auto turned = find_written(document, scene.m_turned);
    REQUIRE(get_ux(turned).value("rotation"_L1).toDouble() == 37);
    require_transform(turned, Matrix(0.7986354827880859, 0.6018150448799133,
      -0.6018150448799133, 0.7986354827880859, 250.22720336914062,
      149.88671875));
    drag_rotate(document, scene.m_snapped, {33}, true);
    auto snapped = find_written(document, scene.m_snapped);
    REQUIRE(get_ux(snapped).value("rotation"_L1).toDouble() == 45);
    require_transform(snapped, Matrix(0.7071067690849304, 0.7071067690849304,
      -0.7071067690849304, 0.7071067690849304, 664.6447143554688,
      143.93397521972656));
    drag_rotate(document, scene.m_wrapped, {90, 180, 270, 350, 20}, false);
    auto& wrapped = document.get(scene.m_wrapped).m_header;
    REQUIRE(wrapped.m_rotation == 20);
    REQUIRE(wrapped.m_transform.m_a ==
      doctest::Approx(0.9396926164627075).epsilon(1e-6));
    REQUIRE(wrapped.m_transform.m_b ==
      doctest::Approx(0.3420201539993286).epsilon(1e-6));
  }

  TEST_CASE("flip_negative_rotation") {
    auto document = Document();
    auto scene = build(document);
    auto registry = CommandRegistry();
    add_header_commands(registry, estimate_text_bounds);
    auto history = History(document);
    REQUIRE(history.execute(registry.make("object.transform.flip_horizontally",
      document, std::vector{scene.m_flipped})));
    auto flipped = find_written(document, scene.m_flipped);
    REQUIRE(get_ux(flipped).value("rotation"_L1).toDouble() == -150);
    require_transform(flipped, Matrix(-0.8660253882408142, -0.5, 0.5,
      -0.8660253882408142, 1161.6025390625, 343.30126953125));
    auto shape = flipped.value("shape"_L1).toObject();
    REQUIRE(shape.value("width"_L1).toDouble() == 200);
    REQUIRE(shape.value("height"_L1).toDouble() == 100);
  }

  TEST_CASE("group_in_rotated_group") {
    auto document = Document();
    auto scene = build(document);
    auto history = History(document);
    auto command = history.execute(
      std::make_unique<GroupCommand>(std::vector{scene.m_first}));
    REQUIRE(command);
    auto group = static_cast<const GroupCommand&>(*command).get_group();
    REQUIRE(document.get(group).m_parent == scene.m_group);
    auto written = find_written(document, group);
    REQUIRE(!written.contains("transform"_L1));
    REQUIRE(!get_ux(written).contains("rotation"_L1));
    REQUIRE(get_ux(written).value("nameL10N"_L1) == "SHAPE_GROUP"_L1);
    auto first = find_written(document, scene.m_first);
    REQUIRE(!get_ux(first).contains("rotation"_L1));
    auto& child = document.get(scene.m_first).m_header;
    REQUIRE(child.m_rotation == 0);
    REQUIRE(std::abs(child.m_transform.m_a - 1) < 1e-9);
    REQUIRE(std::abs(child.m_transform.m_b) < 1e-9);
    REQUIRE(std::abs(child.m_transform.m_tx) < 1e-9);
    REQUIRE(std::abs(child.m_transform.m_ty) < 1e-9);
    auto outer = find_written(document, scene.m_group);
    REQUIRE(get_ux(outer).value("rotation"_L1).toDouble() == 30);
  }

  TEST_CASE("resize_past_opposite_corner") {
    auto document = Document();
    auto scene = build(document);
    auto transform = document.get(scene.m_inverted).m_header.m_transform;
    auto mapping = transform * make_rect_mapping(Rect(0, 0, 200, 100),
      Rect(0, 0, -104, -60)) * *invert(transform);
    auto history = History(document);
    REQUIRE(history.execute(std::make_unique<TransformCommand>(
      std::vector{scene.m_inverted}, mapping, "Resize", TransformMode::DRAG,
      ResizeRule::RESPONSIVE, estimate_text_bounds)));
    auto inverted = find_written(document, scene.m_inverted);
    REQUIRE(get_ux(inverted).value("rotation"_L1).toDouble() == 30);
    require_transform(inverted, Matrix(0.8660253882408142, 0.5, -0.5,
      0.8660253882408142, 978.330810546875, 502.7372131347656));
    auto shape = inverted.value("shape"_L1).toObject();
    REQUIRE(shape.value("x"_L1).toDouble() == 0);
    REQUIRE(shape.value("y"_L1).toDouble() == 0);
    REQUIRE(shape.value("width"_L1).toDouble() == doctest::Approx(104));
    REQUIRE(shape.value("height"_L1).toDouble() == doctest::Approx(60));
  }
}

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <QBuffer>
#include <QImage>
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
    return (image.m_image == other.m_image || (image.m_image &&
      other.m_image && *image.m_image == *other.m_image)) &&
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

  constexpr auto CELL_COUNT = 520;
  constexpr auto COLUMN_COUNT = 26;

  struct Sample {
    NodeId m_master;
    NodeId m_icon;
    NodeId m_artboard;
    NodeId m_content;
    NodeId m_grid;
    NodeId m_masked;
    NodeId m_faded;
    NodeId m_scroll;
    NodeId m_rotated;
    NodeId m_tilted;
    NodeId m_isolated;
    NodeId m_instances;
    NodeId m_overridden;
    NodeId m_text;
    NodeId m_small;
  };

  Rect get_cell(int index) {
    return Rect(10 * (index % COLUMN_COUNT), 10 * (index / COLUMN_COUNT), 8,
      8);
  }

  Shape make_cell(int index) {
    auto shape = make_shape(Rect(0, 0, 8, 8),
      make_color(index % 256, (7 * index) % 256, (13 * index) % 256));
    if(index % 50 == 0) {
      shape.m_geometry = EllipseShape(Point(4, 4), 4, 4);
      shape.m_style.m_stroke = Stroke(make_color(0, 0, 0), 1);
    }
    return shape;
  }

  void add_cells(Transaction& transaction, const NodeId& parent, int count) {
    for(auto i = 0; i != count; ++i) {
      auto cell = get_cell(i);
      transaction.append(parent,
        make_header(make_translation(cell.m_x, cell.m_y)), make_cell(i));
    }
  }

  Sample make_sample(Document& document) {
    auto sample = Sample();
    auto transaction = Transaction(document);
    sample.m_master = transaction.insert_root(make_header(), Group());
    sample.m_icon = transaction.append(sample.m_master, make_header(),
      Group());
    transaction.append(sample.m_icon, make_header(make_translation(1, 1)),
      make_shape(Rect(0, 0, 4, 4), make_color(255, 0, 0)));
    transaction.append(sample.m_icon, make_header(make_translation(5, 1)),
      make_cell(0));
    sample.m_artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 2000, 2000)));
    sample.m_content =
      transaction.append(sample.m_artboard, make_header(), Group());
    sample.m_grid = transaction.append(
      sample.m_content, make_header(make_translation(10, 10)), Group());
    add_cells(transaction, sample.m_grid, CELL_COUNT);
    auto masked = Group();
    masked.m_mask = Mask(std::vector<MaskShape>(
      {MaskShape(Matrix(), EllipseShape(Point(130, 100), 130, 100))}));
    sample.m_masked = transaction.append(
      sample.m_content, make_header(make_translation(500, 10)), masked);
    add_cells(transaction, sample.m_masked, CELL_COUNT);
    auto faded = make_header(make_translation(1000, 10));
    faded.m_opacity = 0.5;
    sample.m_faded = transaction.append(sample.m_content, faded, Group());
    add_cells(transaction, sample.m_faded, CELL_COUNT);
    auto scroll = Group();
    scroll.m_scroll_group =
      ScrollGroup(ScrollDirection::VERTICAL, Size(260, 100), Point(0, 50));
    sample.m_scroll = transaction.append(
      sample.m_content, make_header(make_translation(10, 500)), scroll);
    add_cells(transaction, sample.m_scroll, CELL_COUNT);
    sample.m_rotated = transaction.append(sample.m_content,
      make_header(make_translation(600, 500) * make_rotation(0.3)), scroll);
    add_cells(transaction, sample.m_rotated, CELL_COUNT);
    auto tilted = make_header(make_translation(1000, 500));
    auto& transform = tilted.m_transform_3d.emplace();
    transform.m_matrix = std::array<double, 16>(
      {1, 0, 0, 10, 0, 0.9, 0.4, 20, 0, -0.4, 0.9, 8, 0, 0, 0, 1});
    transform.m_perspective_center = Point(130, 100);
    sample.m_tilted = transaction.append(sample.m_content, tilted, Group());
    add_cells(transaction, sample.m_tilted, CELL_COUNT);
    auto isolated = make_header(make_translation(10, 1000));
    isolated.m_is_isolated = true;
    sample.m_isolated =
      transaction.append(sample.m_content, isolated, Group());
    add_cells(transaction, sample.m_isolated, CELL_COUNT);
    auto multiply = make_header(make_translation(20, 20));
    multiply.m_blend_mode = BlendMode::MULTIPLY;
    transaction.append(sample.m_isolated, multiply,
      make_shape(Rect(0, 0, 50, 50), make_color(0, 128, 255)));
    sample.m_instances = transaction.append(
      sample.m_content, make_header(make_translation(500, 1000)), Group());
    for(auto i = 0; i != CELL_COUNT; ++i) {
      auto cell = get_cell(i);
      transaction.append(sample.m_instances,
        make_header(make_translation(cell.m_x, cell.m_y)),
        Reference(sample.m_icon));
    }
    auto overridden = Reference(sample.m_icon);
    overridden.m_has_children = true;
    sample.m_overridden = transaction.append(sample.m_content,
      make_header(make_translation(1000, 1000)), overridden);
    add_cells(transaction, sample.m_overridden, CELL_COUNT);
    auto shadowed = make_cell(1);
    shadowed.m_style.m_effects.push_back(
      DropShadow(Point(0, 3), 2, make_color(0, 0, 0, 0.5), true));
    shadowed.m_style.m_effects.push_back(
      InnerShadow(Point(1, 1), 1, make_color(0, 0, 0, 0.5), true));
    transaction.append(sample.m_grid, make_header(make_translation(0, 300)),
      shadowed);
    sample.m_text = transaction.append(sample.m_content,
      make_header(make_translation(1500, 1500)), make_text(u"Text"));
    sample.m_small = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(2100, 0, 100, 100)));
    transaction.append(sample.m_small, make_header(make_translation(10, 10)),
      make_cell(2));
    transaction.commit();
    return sample;
  }

  void displace(Document& document, const NodeId& id, Point offset) {
    auto header = document.get(id).m_header;
    header.m_transform =
      make_translation(offset.m_x, offset.m_y) * header.m_transform;
    auto transaction = Transaction(document);
    transaction.replace_header(header);
    transaction.commit();
  }

  void recolor(Document& document, const NodeId& id, Color color) {
    auto shape = *document.get(id).get_shape();
    shape.m_style.m_fill = SolidFill(color);
    auto transaction = Transaction(document);
    transaction.replace_payload(id, shape);
    transaction.commit();
  }

  const NodeId& get_child(
      const Document& document, const NodeId& id, std::size_t index) {
    return document.get(id).m_children.at(index);
  }

  ImageResource make_resource(const ImageId& id) {
    auto image = QImage(QSize(4, 2), QImage::Format_RGBA8888);
    image.fill(QColor(10, 20, 30));
    auto bytes = QByteArray();
    auto buffer = QBuffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    auto data = std::vector<std::byte>(bytes.size());
    std::memcpy(data.data(), bytes.constData(), bytes.size());
    auto resource = ImageResource();
    resource.m_id = id;
    resource.m_media_type = "image/png";
    resource.m_data = Shared(std::move(data));
    return resource;
  }

  bool has_image(const Scene& scene) {
    for(auto& command : flatten(scene)) {
      if(auto fill = std::get_if<FillCommand>(&command)) {
        if(std::holds_alternative<ImagePaint>(fill->m_paint)) {
          return true;
        }
      }
    }
    return false;
  }

  std::unordered_set<const DrawList*> get_lists(const Scene& scene) {
    auto lists = std::unordered_set<const DrawList*>();
    for(auto& list : scene.get_drawing()->get_lists()) {
      lists.insert(list.get());
    }
    return lists;
  }

  std::size_t count_new(const Scene& scene,
      const std::unordered_set<const DrawList*>& previous) {
    auto count = std::size_t(0);
    for(auto& list : scene.get_drawing()->get_lists()) {
      if(!previous.contains(list.get())) {
        ++count;
      }
    }
    return count;
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

  TEST_CASE("retained_edits") {
    auto document = Document();
    auto sample = make_sample(document);
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(scene.get_drawing()->get_lists().size() > 4);
    auto check = [&] {
      scene.update();
      REQUIRE(scene.is_current());
      REQUIRE(is_rebuilt_equal(document, scene));
    };
    displace(document, get_child(document, sample.m_grid, 300), Point(3, 2));
    check();
    recolor(document, get_child(document, sample.m_masked, 10),
      make_color(0, 255, 0));
    check();
    {
      auto masked = *document.get(sample.m_masked).get_group();
      masked.m_mask->m_shapes[0].m_geometry =
        EllipseShape(Point(100, 100), 90, 60);
      auto transaction = Transaction(document);
      transaction.replace_payload(sample.m_masked, masked);
      transaction.commit();
    }
    check();
    displace(document, get_child(document, sample.m_masked, 400), Point(-2, 1));
    check();
    {
      auto header = document.get(sample.m_faded).m_header;
      header.m_opacity = 0.75;
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.commit();
    }
    check();
    displace(document, get_child(document, sample.m_faded, 5), Point(1, 1));
    check();
    {
      auto scroll = *document.get(sample.m_scroll).get_group();
      scroll.m_scroll_group->m_offset = Point(0, 80);
      auto transaction = Transaction(document);
      transaction.replace_payload(sample.m_scroll, scroll);
      transaction.commit();
    }
    check();
    displace(document, get_child(document, sample.m_scroll, 200), Point(0, 4));
    check();
    {
      auto header = document.get(sample.m_rotated).m_header;
      header.m_transform = header.m_transform * make_rotation(0.1);
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.commit();
    }
    check();
    recolor(document, get_child(document, sample.m_rotated, 7),
      make_color(10, 20, 30));
    check();
    {
      auto header = document.get(sample.m_tilted).m_header;
      header.m_transform_3d->m_matrix[3] = 30;
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.commit();
    }
    check();
    displace(document, get_child(document, sample.m_tilted, 99), Point(5, 5));
    check();
    {
      auto& children = document.get(sample.m_isolated).m_children;
      auto header = document.get(children.back()).m_header;
      header.m_blend_mode = BlendMode::NORMAL;
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.commit();
    }
    check();
    recolor(document, get_child(document, sample.m_icon, 0),
      make_color(0, 0, 0));
    check();
    displace(document, get_child(document, sample.m_instances, 260),
      Point(2, 2));
    check();
    displace(document, get_child(document, sample.m_overridden, 3),
      Point(2, 2));
    check();
    {
      auto text = *document.get(sample.m_text).get_text();
      text.m_content = Shared(std::u16string(u"Longer text"));
      auto transaction = Transaction(document);
      transaction.replace_payload(sample.m_text, text);
      transaction.commit();
    }
    check();
    {
      auto& cell = get_child(document, sample.m_grid, 120);
      auto shape = *document.get(cell).get_shape();
      shape.m_style.m_effects.push_back(Blur(2, 0, 1, false, true));
      auto transaction = Transaction(document);
      transaction.replace_payload(cell, shape);
      transaction.commit();
    }
    check();
    {
      auto transaction = Transaction(document);
      transaction.insert(sample.m_grid, 10, make_header(), make_cell(3));
      transaction.remove(get_child(document, sample.m_grid, 400));
      transaction.move(
        get_child(document, sample.m_grid, 20), sample.m_faded, 0);
      transaction.move(get_child(document, sample.m_grid, 30), sample.m_grid,
        200);
      transaction.commit();
    }
    check();
    {
      auto header = document.get(sample.m_grid).m_header;
      header.m_is_visible = false;
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.commit();
    }
    check();
    document.undo();
    check();
    displace(document, sample.m_content, Point(7, 0));
    check();
    scene.set_tolerance(TOLERANCE / 8);
    check();
    scene.set_tolerance(TOLERANCE);
    check();
    while(document.undo()) {
      check();
    }
    while(document.redo()) {
      check();
    }
  }

  TEST_CASE("retained_images") {
    auto document = Document();
    auto sample = make_sample(document);
    auto image = generate_node_id();
    {
      auto shape = make_cell(4);
      auto fill = ImageFill();
      fill.m_image = image;
      fill.m_size = Size(4, 2);
      shape.m_style.m_fill = fill;
      auto transaction = Transaction(document);
      transaction.insert(sample.m_grid, 100, make_header(), shape);
      transaction.commit();
    }
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    REQUIRE(!has_image(scene));
    auto lists = get_lists(scene);
    {
      auto transaction = Transaction(document);
      transaction.add_image(make_resource(image));
      transaction.commit();
    }
    scene.update();
    REQUIRE(has_image(scene));
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(count_new(scene, lists) == 1);
    document.undo();
    scene.update();
    REQUIRE(!has_image(scene));
    REQUIRE(is_rebuilt_equal(document, scene));
  }

  TEST_CASE("splice") {
    auto document = Document();
    auto sample = make_sample(document);
    auto scene = Scene(document, TOLERANCE);
    scene.update();
    auto lists = get_lists(scene);
    auto& cell = get_child(document, sample.m_grid, 300);
    recolor(document, cell, make_color(1, 2, 3));
    auto damage = scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(scene.get_drawing()->get_lists().size() == lists.size());
    REQUIRE(count_new(scene, lists) == 1);
    auto bounds = get_cell(300);
    REQUIRE(is_touching(damage, Rect(bounds.m_x + 10, bounds.m_y + 10,
      bounds.m_width, bounds.m_height)));
    REQUIRE(!is_touching(damage, Rect(10, 10, 100, 100)));
    REQUIRE(!is_touching(damage, Rect(500, 10, 260, 200)));
    lists = get_lists(scene);
    recolor(document, get_child(document, sample.m_faded, 300),
      make_color(1, 2, 3));
    damage = scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(count_new(scene, lists) == 1);
    REQUIRE(!is_touching(damage, Rect(10, 10, 260, 200)));
    lists = get_lists(scene);
    displace(document, get_child(document, sample.m_small, 0), Point(5, 0));
    damage = scene.update();
    REQUIRE(count_new(scene, lists) == 1);
    REQUIRE(is_touching(damage, Rect(2115, 10, 8, 8)));
    REQUIRE(!is_touching(damage, Rect(0, 0, 2000, 2000)));
    lists = get_lists(scene);
    recolor(document, get_child(document, sample.m_icon, 0),
      make_color(9, 9, 9));
    damage = scene.update();
    REQUIRE(is_rebuilt_equal(document, scene));
    REQUIRE(!is_touching(damage, Rect(10, 10, 260, 200)));
    REQUIRE(is_touching(damage, Rect(501, 1001, 4, 4)));
  }
}

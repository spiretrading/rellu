#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QFont>
#include <QPersistentModelIndex>
#include <QTreeView>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Ui/LayersModel.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  QString get_name(const LayersModel& model, const QModelIndex& index) {
    return model.data(index, Qt::DisplayRole).toString();
  }

  void require_rows(LayersModel& model, const Document& document,
      const QModelIndex& parent, std::vector<NodeId> children) {
    std::reverse(children.begin(), children.end());
    for(auto row = 0; row != static_cast<int>(children.size()); ++row) {
      auto index = model.index(row, LayersModel::NAME, parent);
      REQUIRE(index.isValid());
      REQUIRE(index.row() == row);
      REQUIRE(model.parent(index) == parent);
      REQUIRE(model.get_id(index) == children[row]);
      REQUIRE(model.index(row, LayersModel::LOCK, parent).internalPointer() ==
        index.internalPointer());
      auto& node = document.get(children[row]);
      REQUIRE(model.hasChildren(index) == !node.m_children.empty());
      REQUIRE(model.rowCount(index) ==
        static_cast<int>(node.m_children.size()));
      require_rows(model, document, index, node.m_children);
    }
  }

  void require_matches(LayersModel& model, const Document& document) {
    auto artboards = std::vector<NodeId>();
    auto others = std::vector<NodeId>();
    for(auto& id : document.get_canvas()) {
      if(document.get(id).get_artboard()) {
        artboards.push_back(id);
      } else {
        others.push_back(id);
      }
    }
    if(artboards.empty() || others.empty()) {
      REQUIRE(model.rowCount() ==
        static_cast<int>(document.get_canvas().size()));
      require_rows(model, document, QModelIndex(), document.get_canvas());
      return;
    }
    auto row = static_cast<int>(artboards.size());
    REQUIRE(model.rowCount() == row + 1);
    auto pasteboard = model.index(row, LayersModel::NAME);
    REQUIRE(model.get_id(pasteboard) == LayersModel::PASTEBOARD);
    REQUIRE(model.parent(pasteboard) == QModelIndex());
    REQUIRE(model.find(LayersModel::PASTEBOARD) == pasteboard);
    REQUIRE(model.hasChildren(pasteboard));
    REQUIRE(model.rowCount(pasteboard) == static_cast<int>(others.size()));
    require_rows(model, document, QModelIndex(), artboards);
    require_rows(model, document, pasteboard, others);
  }

  double get_milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  }

  struct Dimensions {
    int m_artboards;
    int m_groups;
    int m_shapes;
    int m_wide;
  };

  struct Timings {
    std::size_t m_nodes;
    double m_build;
    double m_open;
    double m_expand;
    double m_wide;
    double m_find;
    double m_rename;
    double m_edit;
    double m_remove;
  };

  Timings test_large_document(const Dimensions& dimensions) {
    auto times = Timings();
    auto document = Document();
    auto artboards = std::vector<NodeId>();
    auto last_group = NodeId();
    auto last_shape = NodeId();
    auto wide = NodeId();
    auto start = std::chrono::steady_clock::now();
    {
      auto transaction = Transaction(document);
      auto artboard = Artboard();
      artboard.m_frame = Rect(0, 0, 100, 100);
      auto shape = make_rectangle_shape(10, 10);
      for(auto i = 0; i != dimensions.m_artboards; ++i) {
        artboards.push_back(transaction.append(
          NodeId(), make_header("Artboard " + std::to_string(i)), artboard));
        for(auto j = 0; j != dimensions.m_groups; ++j) {
          last_group = transaction.append(
            artboards.back(), make_header("Group"), Group());
          for(auto k = 0; k != dimensions.m_shapes; ++k) {
            last_shape = transaction.append(
              last_group, make_header("Shape"), shape);
          }
        }
      }
      wide = transaction.append(NodeId(), make_header("Wide"), Group());
      for(auto i = 0; i != dimensions.m_wide; ++i) {
        transaction.append(wide, make_header("Item"), shape);
      }
      transaction.commit();
    }
    times.m_nodes = document.get_nodes().get_size();
    times.m_build = get_milliseconds(start);
    auto model = LayersModel();
    start = std::chrono::steady_clock::now();
    model.set_document(&document);
    auto rows = model.rowCount();
    for(auto row = 0; row != rows; ++row) {
      model.data(model.index(row, LayersModel::NAME), Qt::DisplayRole);
    }
    times.m_open = get_milliseconds(start);
    REQUIRE(rows == dimensions.m_artboards + 1);
    auto view = QTreeView();
    view.setUniformRowHeights(true);
    view.setModel(&model);
    view.resize(300, 800);
    view.show();
    start = std::chrono::steady_clock::now();
    auto artboard = model.index(dimensions.m_artboards - 1, LayersModel::NAME);
    view.expand(artboard);
    view.expand(
      model.index(dimensions.m_groups - 1, LayersModel::NAME, artboard));
    view.scrollToBottom();
    view.grab();
    times.m_expand = get_milliseconds(start);
    start = std::chrono::steady_clock::now();
    auto wide_index = model.find(wide);
    view.expand(wide_index);
    view.scrollTo(
      model.index(dimensions.m_wide / 2, LayersModel::NAME, wide_index));
    view.grab();
    times.m_wide = get_milliseconds(start);
    start = std::chrono::steady_clock::now();
    auto shape = model.find(last_shape);
    times.m_find = get_milliseconds(start);
    REQUIRE(model.get_id(shape) == last_shape);
    start = std::chrono::steady_clock::now();
    {
      auto transaction = Transaction(document);
      auto header = document.get(last_shape).m_header;
      header.m_name = "Renamed";
      transaction.replace_header(header);
      transaction.commit();
    }
    times.m_rename = get_milliseconds(start);
    REQUIRE(model.data(shape, Qt::DisplayRole).toString() == "Renamed");
    start = std::chrono::steady_clock::now();
    {
      auto transaction = Transaction(document);
      transaction.insert(
        wide, 0, make_header("Inserted"), make_rectangle_shape(1, 1));
      transaction.remove(document.get_children(wide)[dimensions.m_wide / 2]);
      transaction.commit();
    }
    times.m_edit = get_milliseconds(start);
    REQUIRE(model.rowCount(wide_index) == dimensions.m_wide);
    REQUIRE(model.data(model.index(dimensions.m_wide - 1, LayersModel::NAME,
      wide_index), Qt::DisplayRole).toString() == "Inserted");
    start = std::chrono::steady_clock::now();
    {
      auto transaction = Transaction(document);
      transaction.remove(artboards.front());
      transaction.commit();
    }
    times.m_remove = get_milliseconds(start);
    REQUIRE(model.rowCount() == dimensions.m_artboards);
    view.hide();
    return times;
  }

  struct Recorder {
    std::vector<std::string> m_events;

    explicit Recorder(LayersModel& model) {
      auto describe = [&model] (const QModelIndex& parent) {
        if(!parent.isValid()) {
          return std::string("canvas");
        }
        return model.data(parent, Qt::DisplayRole).toString().toStdString();
      };
      QObject::connect(&model, &QAbstractItemModel::rowsInserted, &model,
        [=, this] (const QModelIndex& parent, int first, int last) {
          m_events.push_back("insert " + describe(parent) + " " +
            std::to_string(first) + " " + std::to_string(last));
        });
      QObject::connect(&model, &QAbstractItemModel::rowsRemoved, &model,
        [=, this] (const QModelIndex& parent, int first, int last) {
          m_events.push_back("remove " + describe(parent) + " " +
            std::to_string(first) + " " + std::to_string(last));
        });
      QObject::connect(&model, &QAbstractItemModel::layoutChanged, &model,
        [this] {
          m_events.push_back("layout");
        });
      QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
        [=, this] (const QModelIndex& top_left) {
          m_events.push_back("data " + describe(top_left));
        });
    }
  };
}

TEST_SUITE("LayersModel") {
  TEST_CASE("rows") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    REQUIRE(model.rowCount() == 0);
    model.set_document(&document);
    REQUIRE(model.get_document() == &document);
    REQUIRE(model.columnCount() == LayersModel::COLUMN_COUNT);
    require_matches(model, document);
    auto home = model.index(0, LayersModel::NAME);
    REQUIRE(get_name(model, home) == "Home");
    auto pasteboard = model.index(1, LayersModel::NAME);
    REQUIRE(get_name(model, pasteboard) == "Pasteboard");
    REQUIRE(get_name(model, model.index(0, LayersModel::NAME, pasteboard)) ==
      "Loose");
    REQUIRE(model.rowCount(home) == 3);
    REQUIRE(model.rowCount(model.index(0, LayersModel::LOCK)) == 0);
    REQUIRE(!model.index(3, LayersModel::NAME, home).isValid());
    REQUIRE(!model.index(0, LayersModel::COLUMN_COUNT, home).isValid());
    model.set_document(nullptr);
    REQUIRE(model.rowCount() == 0);
  }

  TEST_CASE("names_and_kinds") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    auto require_node = [&] (const NodeId& id, const QString& name,
        NodeKind kind, bool is_reference) {
      auto index = model.find(id);
      REQUIRE(index.isValid());
      REQUIRE(get_name(model, index) == name);
      REQUIRE(model.data(index, LayersModel::KIND_ROLE).toInt() ==
        static_cast<int>(kind));
      REQUIRE(model.data(index, LayersModel::REFERENCE_ROLE).toBool() ==
        is_reference);
      REQUIRE(model.data(index, LayersModel::ID_ROLE).toString() ==
        QString::fromStdString(to_string(id)));
      REQUIRE(!model.data(index, Qt::DecorationRole).isNull());
    };
    require_node(ids.m_artboard, "Home", NodeKind::ARTBOARD, false);
    require_node(ids.m_card, "Card", NodeKind::GROUP, false);
    require_node(ids.m_background, "Background", NodeKind::SHAPE, false);
    require_node(ids.m_label, "Hello world", NodeKind::TEXT, false);
    require_node(ids.m_ellipse, "Ellipse", NodeKind::SHAPE, false);
    require_node(ids.m_instance, "Button", NodeKind::GROUP, false);
    require_node(ids.m_reference, "Fill", NodeKind::SHAPE, true);
    require_node(ids.m_loose, "Loose", NodeKind::SHAPE, false);
    REQUIRE(model.data(model.find(ids.m_reference), Qt::FontRole).value<
      QFont>().italic());
    REQUIRE(model.headerData(LayersModel::NAME, Qt::Horizontal,
      Qt::DisplayRole).toString() == "Name");
  }

  TEST_CASE("component_group_names") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    auto index = QPersistentModelIndex(model.find(ids.m_instance));
    REQUIRE(get_name(model, index) == "Button");
    auto recorder = Recorder(model);
    {
      auto transaction = Transaction(document);
      file_asset(document, transaction, ids.m_master, {"Forms", "Inputs"});
      transaction.commit();
    }
    REQUIRE(get_name(model, index) == "Forms / Inputs / Button");
    REQUIRE(model.data(index, Qt::EditRole).toString() ==
      "Forms / Inputs / Button");
    REQUIRE(std::find(recorder.m_events.begin(), recorder.m_events.end(),
      "data Forms / Inputs / Button") != recorder.m_events.end());
    REQUIRE(get_name(model, model.find(ids.m_card)) == "Card");
  }

  TEST_CASE("find") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    auto label = model.find(ids.m_label, LayersModel::VISIBILITY);
    REQUIRE(label.row() == 0);
    REQUIRE(label.column() == LayersModel::VISIBILITY);
    REQUIRE(model.get_id(model.parent(label)) == ids.m_card);
    REQUIRE(model.get_id(model.parent(model.parent(label))) == ids.m_artboard);
    REQUIRE(!model.find(ids.m_master_shape).isValid());
    REQUIRE(!model.find(ids.m_master).isValid());
    REQUIRE(!model.find(generate_node_id()).isValid());
    REQUIRE(!model.find(NodeId()).isValid());
    REQUIRE(model.get_id(QModelIndex()).is_nil());
  }

  TEST_CASE("header_changes") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    auto background = model.find(ids.m_background);
    REQUIRE(model.data(background, LayersModel::VISIBLE_ROLE).toBool());
    REQUIRE(!model.data(background, LayersModel::LOCKED_ROLE).toBool());
    REQUIRE(model.data(model.find(ids.m_background, LayersModel::LOCK),
      Qt::DecorationRole).isNull());
    auto recorder = Recorder(model);
    {
      auto transaction = Transaction(document);
      auto header = document.get(ids.m_background).m_header;
      header.m_name = "Fill";
      header.m_is_visible = false;
      header.m_is_locked = true;
      transaction.replace_header(header);
      transaction.commit();
    }
    REQUIRE(recorder.m_events == std::vector<std::string>({"data Fill"}));
    REQUIRE(get_name(model, background) == "Fill");
    REQUIRE(!model.data(background, LayersModel::VISIBLE_ROLE).toBool());
    REQUIRE(model.data(background, LayersModel::LOCKED_ROLE).toBool());
    REQUIRE(!model.data(model.find(ids.m_background, LayersModel::LOCK),
      Qt::DecorationRole).isNull());
    REQUIRE(!model.data(background, Qt::ForegroundRole).isNull());
  }

  TEST_CASE("insertion") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    require_matches(model, document);
    auto home = QPersistentModelIndex(model.find(ids.m_artboard));
    auto label = QPersistentModelIndex(model.find(ids.m_label));
    auto recorder = Recorder(model);
    {
      auto transaction = Transaction(document);
      transaction.append(
        ids.m_card, make_header("Top"), make_rectangle_shape(1, 1));
      transaction.insert(
        ids.m_card, 0, make_header("Bottom"), make_rectangle_shape(1, 1));
      transaction.insert(
        NodeId(), 0, make_header("First"), make_rectangle_shape(1, 1));
      transaction.commit();
    }
    REQUIRE(recorder.m_events == std::vector<std::string>({
      "insert Pasteboard 1 1", "insert Card 0 0", "insert Card 3 3",
      "data Card"}));
    REQUIRE(home.row() == 0);
    REQUIRE(label.row() == 1);
    REQUIRE(get_name(model, model.index(1, LayersModel::NAME,
      model.find(LayersModel::PASTEBOARD))) == "First");
    require_matches(model, document);
  }

  TEST_CASE("removal") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    require_matches(model, document);
    auto label = QPersistentModelIndex(model.find(ids.m_label));
    auto instance = QPersistentModelIndex(model.find(ids.m_instance));
    auto recorder = Recorder(model);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_card);
      transaction.remove(ids.m_loose);
      transaction.commit();
    }
    REQUIRE(recorder.m_events == std::vector<std::string>({
      "remove canvas 1 1", "remove Home 2 2", "data Home"}));
    REQUIRE(!label.isValid());
    REQUIRE(instance.row() == 0);
    require_matches(model, document);
  }

  TEST_CASE("moves") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    require_matches(model, document);
    auto card = QPersistentModelIndex(model.find(ids.m_card));
    auto ellipse = QPersistentModelIndex(model.find(ids.m_ellipse));
    auto instance = QPersistentModelIndex(model.find(ids.m_instance));
    auto recorder = Recorder(model);
    {
      auto transaction = Transaction(document);
      transaction.move(ids.m_instance, ids.m_artboard, 0);
      transaction.commit();
    }
    REQUIRE(recorder.m_events == std::vector<std::string>({
      "layout", "data Home"}));
    REQUIRE(instance.row() == 2);
    REQUIRE(card.row() == 1);
    REQUIRE(ellipse.row() == 0);
    require_matches(model, document);
    recorder.m_events.clear();
    {
      auto transaction = Transaction(document);
      transaction.move(ids.m_ellipse, ids.m_card, 1);
      transaction.commit();
    }
    REQUIRE(recorder.m_events == std::vector<std::string>({
      "remove Home 0 0", "insert Card 1 1", "data Home", "data Card"}));
    require_matches(model, document);
    REQUIRE(model.get_id(model.index(1, LayersModel::NAME, card)) ==
      ids.m_ellipse);
  }

  TEST_CASE("undo") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    require_matches(model, document);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_card);
      transaction.move(ids.m_loose, ids.m_artboard, 0);
      transaction.commit();
    }
    require_matches(model, document);
    REQUIRE(document.undo());
    require_matches(model, document);
    REQUIRE(get_name(model, model.find(ids.m_label)) == "Hello world");
    REQUIRE(document.redo());
    require_matches(model, document);
  }

  TEST_CASE("unexpanded_changes") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    REQUIRE(model.rowCount() == 2);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_label);
      transaction.append(
        ids.m_artboard, make_header("New"), make_rectangle_shape(1, 1));
      transaction.commit();
    }
    require_matches(model, document);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_background);
      transaction.commit();
    }
    require_matches(model, document);
  }

  TEST_CASE("scattered_changes") {
    auto document = Document();
    auto group = NodeId();
    auto children = std::vector<NodeId>();
    {
      auto transaction = Transaction(document);
      group = transaction.append(NodeId(), make_header("Group"), Group());
      for(auto i = 0; i != 100; ++i) {
        children.push_back(transaction.append(group,
          make_header(std::to_string(i)), make_rectangle_shape(1, 1)));
      }
      transaction.commit();
    }
    auto model = LayersModel();
    model.set_document(&document);
    require_matches(model, document);
    {
      auto transaction = Transaction(document);
      for(auto i = 0; i < 100; i += 2) {
        transaction.remove(children[i]);
      }
      for(auto i = 0; i != 40; ++i) {
        transaction.insert(group, 2 * i, make_header("n" + std::to_string(i)),
          make_rectangle_shape(1, 1));
      }
      transaction.commit();
    }
    require_matches(model, document);
    REQUIRE(model.rowCount(model.find(group)) == 90);
    {
      auto transaction = Transaction(document);
      transaction.move(children[99], group, 0);
      transaction.move(children[1], group, 89);
      transaction.commit();
    }
    require_matches(model, document);
  }

  TEST_CASE("large_document") {
    test_large_document(Dimensions(4, 4, 4, 1000));
  }

  TEST_CASE("large_document_performance" * doctest::skip()) {
    auto dimensions = Dimensions(100, 100, 100, 100000);
    auto times = test_large_document(dimensions);
    MESSAGE("Built ", times.m_nodes, " nodes in ", times.m_build, " ms");
    MESSAGE("Listed ", dimensions.m_artboards + 1, " top-level rows in ",
      times.m_open, " ms");
    MESSAGE("Expanded and painted a group of ", dimensions.m_shapes, " in ",
      times.m_expand, " ms");
    MESSAGE("Expanded and painted a group of ", dimensions.m_wide, " in ",
      times.m_wide, " ms");
    MESSAGE("Found a node at depth 3 in ", times.m_find, " ms");
    MESSAGE("Renamed a node in ", times.m_rename, " ms");
    MESSAGE("Inserted and removed rows of the group of ", dimensions.m_wide,
      " in ", times.m_edit, " ms");
    MESSAGE("Removed an artboard of ",
      dimensions.m_groups * (dimensions.m_shapes + 1) + 1, " nodes in ",
      times.m_remove, " ms");
    REQUIRE(times.m_open < 100);
    REQUIRE(times.m_expand < 1000);
    REQUIRE(times.m_find < 100);
    REQUIRE(times.m_rename < 100);
  }

  TEST_CASE("pasteboard_row") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto second = NodeId();
    {
      auto transaction = Transaction(document);
      second = transaction.append(
        NodeId(), make_header("Second"), make_rectangle_shape(1, 1));
      transaction.commit();
    }
    auto model = LayersModel();
    model.set_document(&document);
    auto pasteboard = model.find(LayersModel::PASTEBOARD);
    REQUIRE(pasteboard.row() == 1);
    REQUIRE(get_name(model, pasteboard) == "Pasteboard");
    REQUIRE(model.data(pasteboard, LayersModel::ID_ROLE).isNull());
    REQUIRE(model.find(ids.m_loose).parent() == pasteboard);
    REQUIRE(model.find(second).row() == 0);
    auto moves = std::vector<std::pair<NodeId, std::size_t>>();
    model.set_move_handler([&] (const auto& nodes, const auto& parent,
        auto index) {
      moves.emplace_back(parent, index);
      return true;
    });
    auto data = std::unique_ptr<QMimeData>(
      model.mimeData({model.find(ids.m_ellipse)}));
    REQUIRE(model.canDropMimeData(data.get(), Qt::MoveAction, 0, 0,
      pasteboard));
    REQUIRE(model.dropMimeData(data.get(), Qt::MoveAction, 0, 0, pasteboard));
    REQUIRE(model.dropMimeData(data.get(), Qt::MoveAction, 2, 0, pasteboard));
    REQUIRE(model.dropMimeData(
      data.get(), Qt::MoveAction, 1, 0, QModelIndex()));
    REQUIRE(moves == std::vector<std::pair<NodeId, std::size_t>>({
      std::pair(NodeId(), 3), std::pair(NodeId(), 1),
      std::pair(NodeId(), 0)}));
    auto artboard = std::unique_ptr<QMimeData>(
      model.mimeData({model.find(ids.m_artboard)}));
    REQUIRE(!model.canDropMimeData(
      artboard.get(), Qt::MoveAction, 0, 0, pasteboard));
    REQUIRE(model.canDropMimeData(
      artboard.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_loose);
      transaction.remove(second);
      transaction.commit();
    }
    REQUIRE(!model.find(LayersModel::PASTEBOARD).isValid());
    require_matches(model, document);
    {
      auto transaction = Transaction(document);
      transaction.append(
        NodeId(), make_header("Third"), make_rectangle_shape(1, 1));
      transaction.commit();
    }
    require_matches(model, document);
  }

  TEST_CASE("drag_and_drop") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto model = LayersModel();
    model.set_document(&document);
    auto card = model.find(ids.m_card);
    REQUIRE(!model.flags(card).testFlag(Qt::ItemIsDragEnabled));
    auto moves = std::vector<std::pair<NodeId, std::size_t>>();
    model.set_move_handler([&] (const auto& nodes, const auto& parent,
        auto index) {
      REQUIRE(nodes == std::vector{ids.m_ellipse});
      moves.emplace_back(parent, index);
      return true;
    });
    REQUIRE(model.flags(card).testFlag(Qt::ItemIsDragEnabled));
    REQUIRE(model.flags(card).testFlag(Qt::ItemIsDropEnabled));
    REQUIRE(!model.flags(model.find(ids.m_instance)).testFlag(
      Qt::ItemIsDropEnabled));
    auto data = std::unique_ptr<QMimeData>(
      model.mimeData({model.find(ids.m_ellipse)}));
    REQUIRE(data->hasFormat(LayersModel::MIME_TYPE));
    REQUIRE(model.canDropMimeData(data.get(), Qt::MoveAction, 0, 0, card));
    REQUIRE(!model.canDropMimeData(data.get(), Qt::MoveAction, 0, 0,
      model.find(ids.m_instance)));
    REQUIRE(model.dropMimeData(data.get(), Qt::MoveAction, 0, 0, card));
    REQUIRE(model.dropMimeData(data.get(), Qt::MoveAction, 2, 0, card));
    REQUIRE(model.dropMimeData(data.get(), Qt::MoveAction, -1, 0, card));
    REQUIRE(moves == std::vector<std::pair<NodeId, std::size_t>>({
      std::pair(ids.m_card, 2), std::pair(ids.m_card, 0),
      std::pair(ids.m_card, 2)}));
    auto artboard = std::unique_ptr<QMimeData>(
      model.mimeData({model.find(ids.m_artboard)}));
    REQUIRE(!model.canDropMimeData(
      artboard.get(), Qt::MoveAction, 0, 0, card));
  }
}
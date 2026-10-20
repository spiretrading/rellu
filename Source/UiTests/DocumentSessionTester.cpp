#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TransformCommands.hpp"
#include "Rellu/Geometry/Shapes.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdOpaque.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Io/XdWriter.hpp"
#include "Rellu/Model/DocumentComparison.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "TestDocument.hpp"
#include "TestOptions.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  std::filesystem::path to_path(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
  }

  NodeId find_node(const Document& document, const NodeId& parent,
      auto predicate) {
    for(auto& child : document.get_children(parent)) {
      if(predicate(document.get(child))) {
        return child;
      }
      if(auto found = find_node(document, child, predicate);
          !found.is_nil()) {
        return found;
      }
    }
    return NodeId();
  }

  NodeId find_named(const Document& document, const std::string& name) {
    return find_node(document, NodeId(), [&] (const auto& node) {
      return node.m_header.m_name == name;
    });
  }

  NodeId find_text(const Document& document) {
    return find_node(document, NodeId(), [] (const auto& node) {
      return static_cast<bool>(node.get_text());
    });
  }

  void make_dirty(DocumentSession& session, const NodeId& id) {
    session.get_editor().execute(std::make_unique<SetOpacityCommand>(
      std::vector<NodeId>({id}), 0.5));
  }

  std::string read_manifest(const QString& path, const QString& key) {
    return XdPackage(to_path(path)).get_manifest().value(key).toString().
      toStdString();
  }

  std::string read_attribute(const std::string& xml, const std::string& name) {
    auto key = name + "=\"";
    auto start = xml.find(key);
    if(start == std::string::npos) {
      return std::string();
    }
    start += key.size();
    return xml.substr(start, xml.find('"', start) - start);
  }

  std::string read_document_id(const QString& path) {
    auto metadata = XdPackage(to_path(path)).read("META-INF/metadata.xml").
      toStdString();
    auto id = read_attribute(metadata, "xmpMM:DocumentID");
    if(read_attribute(metadata, "xmpMM:OriginalDocumentID") != id) {
      return std::string();
    }
    return id;
  }

  void collect_folder_ids(const QJsonArray& children,
      std::unordered_set<std::string>& ids) {
    for(auto child : children) {
      auto object = child.toObject();
      ids.insert(object.value("id").toString().toStdString());
      collect_folder_ids(object.value("children").toArray(), ids);
      for(auto component : object.value("components").toArray()) {
        ids.insert(
          component.toObject().value("id").toString().toStdString());
      }
    }
  }

  std::unordered_set<std::string> read_folder_ids(const QString& path) {
    auto ids = std::unordered_set<std::string>();
    collect_folder_ids(XdPackage(to_path(path)).get_manifest().value(
      "children").toArray(), ids);
    return ids;
  }

  void require_new_id(const std::string& id) {
    auto parsed = parse_node_id(id);
    REQUIRE(parsed);
    REQUIRE(to_string(*parsed) == id);
    REQUIRE(id[14] == '4');
  }

  void require_round_trip(const Document& document, const QString& path) {
    auto reopened = Document();
    import_xd(to_path(path), reopened);
    REQUIRE(is_equal(document, reopened));
  }
}

TEST_SUITE("DocumentSession") {
  TEST_CASE("names") {
    auto document = Document();
    auto untitled = DocumentSession(document, QString(), XdTextLayout());
    REQUIRE(untitled.get_name() == "Untitled");
    REQUIRE(untitled.get_path().isEmpty());
    REQUIRE(!untitled.is_dirty());
    REQUIRE(&untitled.get_document() == &document);
    REQUIRE(&untitled.get_editor().get_document() == &document);
    auto other = Document();
    auto opened = DocumentSession(other, "C:/designs/home.page.xd",
      XdTextLayout());
    REQUIRE(opened.get_name() == "home.page");
    REQUIRE(to_xd_path("C:/designs/home") == "C:/designs/home.xd");
    REQUIRE(to_xd_path("C:/designs/home.XD") == "C:/designs/home.XD");
    REQUIRE(to_xd_path("C:/designs/home.v2") == "C:/designs/home.v2.xd");
  }

  TEST_CASE("unsaved_changes") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto session = DocumentSession(document, QString(), XdTextLayout());
    auto prompts = 0;
    auto saves = 0;
    auto choice = UnsavedChoice::CANCEL;
    auto is_saved = false;
    auto prompt = [&] {
      ++prompts;
      return choice;
    };
    auto save = [&] {
      ++saves;
      return is_saved;
    };
    REQUIRE(resolve_unsaved_changes(session, prompt, save));
    REQUIRE(prompts == 0);
    make_dirty(session, ids.m_background);
    REQUIRE(session.is_dirty());
    REQUIRE(!resolve_unsaved_changes(session, prompt, save));
    REQUIRE(prompts == 1);
    REQUIRE(saves == 0);
    choice = UnsavedChoice::DISCARD;
    REQUIRE(resolve_unsaved_changes(session, prompt, save));
    REQUIRE(saves == 0);
    choice = UnsavedChoice::SAVE;
    REQUIRE(!resolve_unsaved_changes(session, prompt, save));
    REQUIRE(saves == 1);
    is_saved = true;
    REQUIRE(resolve_unsaved_changes(session, prompt, save));
    REQUIRE(saves == 2);
    REQUIRE(prompts == 4);
    session.get_editor().undo();
    REQUIRE(!session.is_dirty());
  }

  TEST_CASE("new_document") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto document = Document();
    auto session = DocumentSession(document, QString(),
      make_xd_text_layout(get_default_font_library()));
    auto path = directory.filePath("Blank.xd");
    session.save(path);
    REQUIRE(session.get_path() == path);
    REQUIRE(session.get_name() == "Blank");
    REQUIRE(QFileInfo::exists(path));
    auto reopened = Document();
    import_xd(to_path(path), reopened);
    REQUIRE(reopened.get_canvas().empty());
  }

  TEST_CASE("launched_document") {
    auto document = Document();
    auto session = std::unique_ptr<DocumentSession>();
    auto opened = QStringList();
    auto open = [&] (const QString& path) {
      opened.push_back(path);
      return false;
    };
    auto make_new = [&] {
      session =
        std::make_unique<DocumentSession>(document, QString(), XdTextLayout());
      return true;
    };
    REQUIRE(open_launched_document(QStringList({"Rellu"}), open, make_new));
    REQUIRE(session);
    REQUIRE(session->get_name() == "Untitled");
    REQUIRE(opened.isEmpty());
    session.reset();
    REQUIRE(!open_launched_document(
      QStringList({"Rellu", "C:/designs/home.xd"}), open, make_new));
    REQUIRE(opened == QStringList({"C:/designs/home.xd"}));
    REQUIRE(!session);
  }

  TEST_CASE("id") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto document = Document();
    build_test_document(document);
    auto session = DocumentSession(document, QString(), XdTextLayout());
    REQUIRE(session.get_id().empty());
    auto path = directory.filePath("Design.xd");
    session.save(path);
    auto id = session.get_id();
    require_new_id(id);
    REQUIRE(read_manifest(path, "uxdesign#id") == id);
    REQUIRE(read_manifest(path, "uxdesign#parentDocId") == id);
    session.save(path);
    REQUIRE(session.get_id() == id);
    REQUIRE(read_manifest(path, "uxdesign#id") == id);
    REQUIRE(read_manifest(path, "uxdesign#parentDocId") == id);
  }

  TEST_CASE("save_as_id") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto path = directory.filePath("Design.xd");
    {
      auto built = Document();
      build_test_document(built);
      write_xd(built, to_path(path));
    }
    auto original = read_manifest(path, "uxdesign#id");
    auto composite = read_manifest(path, "id");
    auto serialization = read_manifest(path, "uxdesign#serializationId");
    REQUIRE(read_manifest(path, "uxdesign#parentDocId").empty());
    REQUIRE(read_manifest(path, "uxdesign#compositeID") == composite);
    REQUIRE(read_document_id(path) == composite);
    auto document = Document();
    import_xd(to_path(path), document);
    auto session = DocumentSession(document, path,
      make_xd_text_layout(get_default_font_library()));
    REQUIRE(session.get_id() == original);
    session.save(path);
    REQUIRE(session.get_id() == original);
    auto manifest = XdPackage(to_path(path)).get_manifest();
    REQUIRE(read_manifest(path, "id") == composite);
    REQUIRE(read_manifest(path, "uxdesign#compositeID") == composite);
    REQUIRE(read_manifest(path, "name") == "Design\x01");
    require_new_id(read_manifest(path, "uxdesign#serializationId"));
    REQUIRE(read_manifest(path, "uxdesign#serializationId") != serialization);
    REQUIRE(read_document_id(path) == composite);
    auto folders = read_folder_ids(path);
    auto copy = directory.filePath("Copy.xd");
    session.save(copy);
    auto id = session.get_id();
    require_new_id(id);
    REQUIRE(id != original);
    REQUIRE(read_manifest(copy, "uxdesign#id") == id);
    REQUIRE(read_manifest(copy, "uxdesign#parentDocId") == id);
    REQUIRE(read_manifest(copy, "name") == "Copy\x01");
    auto copy_composite = read_manifest(copy, "id");
    REQUIRE(parse_node_id(copy_composite));
    REQUIRE(copy_composite != composite);
    REQUIRE(read_manifest(copy, "uxdesign#compositeID") == copy_composite);
    REQUIRE(read_document_id(copy) == copy_composite);
    auto copy_folders = read_folder_ids(copy);
    REQUIRE(copy_folders.size() == folders.size());
    for(auto& folder : copy_folders) {
      REQUIRE(!folders.contains(folder));
    }
    REQUIRE(XdPackage(to_path(path)).get_manifest() == manifest);
    session.save(copy);
    REQUIRE(session.get_id() == id);
    REQUIRE(read_manifest(copy, "id") == copy_composite);
    REQUIRE(read_document_id(copy) == copy_composite);
    REQUIRE(read_folder_ids(copy) == copy_folders);
    auto other = directory.filePath("Other.xd");
    session.save(other);
    require_new_id(session.get_id());
    REQUIRE(session.get_id() != id);
    REQUIRE(read_manifest(other, "uxdesign#id") == session.get_id());
    REQUIRE(read_manifest(other, "uxdesign#parentDocId") == id);
    REQUIRE(read_manifest(other, "id") != copy_composite);
  }

  TEST_CASE("failed_save") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto document = Document();
    auto ids = build_test_document(document);
    auto session = DocumentSession(document, QString(), XdTextLayout());
    make_dirty(session, ids.m_background);
    auto path = directory.filePath("missing/folder/Design.xd");
    REQUIRE_THROWS(session.save(path));
    REQUIRE(session.is_dirty());
    REQUIRE(session.get_path().isEmpty());
    REQUIRE(!QFileInfo::exists(path));
  }

  TEST_CASE("round_trip") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto source = directory.filePath("Source.xd");
    {
      auto built = Document();
      build_test_document(built);
      write_xd(built, to_path(source));
    }
    auto document = Document();
    import_xd(to_path(source), document);
    auto session = DocumentSession(document, source,
      make_xd_text_layout(get_default_font_library()));
    auto background = find_named(document, "Background");
    auto card = find_named(document, "Card");
    auto label = find_text(document);
    REQUIRE(!background.is_nil());
    REQUIRE(!card.is_nil());
    REQUIRE(!label.is_nil());
    auto origin = get_world_transform(document, card);
    auto& editor = session.get_editor();
    {
      auto transaction = Transaction(document);
      auto text = *document.get(label).get_text();
      text.m_content = Shared(std::u16string(u"Hello world\nSecond line"));
      text.m_layout = std::nullopt;
      transaction.replace_payload(label, std::move(text));
      transaction.commit();
    }
    make_dirty(session, background);
    editor.execute(std::make_unique<SetFillCommand>(
      std::vector<NodeId>({background}), SolidFill(make_color(0, 255, 0))));
    editor.execute(std::make_unique<RenameCommand>(card, "Renamed"));
    editor.execute(std::make_unique<TranslateCommand>(
      std::vector<NodeId>({card}), Point(5, 7)));
    REQUIRE(session.is_dirty());
    auto target = directory.filePath("Edited.xd");
    session.save(target);
    REQUIRE(!session.is_dirty());
    REQUIRE(session.get_path() == target);
    auto reopened = Document();
    import_xd(to_path(target), reopened);
    REQUIRE(reopened.get_nodes().get_size() == document.get_nodes().get_size());
    REQUIRE(reopened.get(background).m_header.m_opacity == 0.5);
    REQUIRE(reopened.get(background).get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 255, 0))));
    REQUIRE(reopened.get(card).m_header.m_name == "Renamed");
    auto moved = get_world_transform(reopened, card);
    REQUIRE(moved.m_tx == origin.m_tx + 5);
    REQUIRE(moved.m_ty == origin.m_ty + 7);
    auto& layout = reopened.get(label).get_text()->m_layout;
    REQUIRE(layout);
    REQUIRE((*layout)->m_paragraphs.size() == 2);
    auto& first = (*layout)->m_paragraphs.front().m_lines.front().m_runs;
    REQUIRE(first.front().m_from == 0);
    REQUIRE(first.back().m_to == 11);
    REQUIRE(first.front().m_position.m_y > 0);
    auto again = directory.filePath("Again.xd");
    write_xd(reopened, to_path(again));
    require_round_trip(reopened, again);
    auto original = Document();
    import_xd(to_path(source), original);
    REQUIRE(original.get(background).m_header.m_opacity == 1);
  }

  TEST_CASE("compound_operands") {
    auto document = Document();
    auto stored = make_rectangle(Rect(0, 0, 1, 1));
    auto compounds = std::vector<NodeId>();
    auto operands = std::vector<NodeId>();
    {
      auto transaction = Transaction(document);
      for(auto i = 0; i != 2; ++i) {
        auto header = NodeHeader();
        if(i == 1) {
          auto data = XdNodeData();
          data.m_orphans.emplace_back(NodeId(), "{}");
          header.m_opaque = OpaqueData(encode_node_data(data));
        }
        auto shape = Shape();
        shape.m_geometry =
          CompoundShape(CompoundOperation::UNION, Shared(stored));
        compounds.push_back(transaction.append(NodeId(), header, shape));
        auto operand = Shape();
        operand.m_geometry = RectangleShape(Rect(0, 0, 10, 10));
        operands.push_back(
          transaction.append(compounds.back(), NodeHeader(), operand));
      }
      transaction.commit();
    }
    auto session = DocumentSession(document, QString(), XdTextLayout());
    session.get_editor().execute(
      std::make_unique<TranslateCommand>(operands, Point(5, 0)));
    auto get_result = [&] (const NodeId& id) {
      return *std::get<CompoundShape>(
        document.get(id).get_shape()->m_geometry).m_path;
    };
    REQUIRE(get_result(compounds[0]) != stored);
    REQUIRE(get_result(compounds[1]) == stored);
  }

  TEST_CASE("xd_document") {
    auto& source = get_xd_document_path();
    if(source.empty()) {
      return;
    }
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto document = Document();
    import_xd(std::filesystem::path(source), document);
    auto session = DocumentSession(document, QString::fromStdString(source),
      make_xd_text_layout(get_default_font_library()));
    auto target_node = NodeId();
    for(auto& id : document.get_canvas()) {
      auto& children = document.get(id).m_children;
      if(!children.empty()) {
        target_node = children.front();
        break;
      }
    }
    REQUIRE(!target_node.is_nil());
    make_dirty(session, target_node);
    session.get_editor().execute(std::make_unique<TranslateCommand>(
      std::vector<NodeId>({target_node}), Point(3, 4)));
    auto target = directory.filePath("Edited.xd");
    session.save(target);
    auto reopened = Document();
    import_xd(to_path(target), reopened);
    REQUIRE(reopened.get_nodes().get_size() == document.get_nodes().get_size());
    REQUIRE(reopened.get(target_node).m_header.m_opacity == 0.5);
    REQUIRE(get_world_transform(reopened, target_node) ==
      get_world_transform(document, target_node));
    auto again = directory.filePath("Again.xd");
    write_xd(reopened, to_path(again));
    require_round_trip(reopened, again);
  }
}

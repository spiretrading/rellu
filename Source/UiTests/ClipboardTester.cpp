#include <cmath>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include <QImage>
#include <QMimeData>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Ui/Clipboard.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

TEST_SUITE("Clipboard") {
  TEST_CASE("copyable") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.is_empty());
    REQUIRE(!clipboard.make_paste_command());
    REQUIRE(!Clipboard::is_copyable(document, std::vector<NodeId>()));
    REQUIRE(!Clipboard::is_copyable(document, {ids.m_reference}));
    REQUIRE(!Clipboard::is_copyable(document, {ids.m_master}));
    REQUIRE(Clipboard::is_copyable(document, {ids.m_reference, ids.m_card}));
    REQUIRE(!clipboard.copy(document, {ids.m_master}));
    REQUIRE(clipboard.is_empty());
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    REQUIRE(!clipboard.is_empty());
    clipboard.clear();
    REQUIRE(clipboard.is_empty());
  }

  TEST_CASE("paste") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_instance, ids.m_card}));
    auto command = clipboard.make_paste_command();
    REQUIRE(command->get_label() == "Paste");
    REQUIRE(editor.execute(std::move(command)));
    auto& copies = editor.get_selection().get_nodes();
    REQUIRE(copies.size() == 2);
    auto& card = document.get(copies[0]);
    auto& instance = document.get(copies[1]);
    REQUIRE(card.m_header.m_name == "Card");
    REQUIRE(card.m_parent == ids.m_artboard);
    REQUIRE(card.m_children.size() == 2);
    REQUIRE(card.m_children[0] != ids.m_background);
    REQUIRE(document.get(card.m_children[0]).m_header.m_name ==
      "Background");
    REQUIRE(get_world_transform(document, card.get_id()) ==
      get_world_transform(document, ids.m_card));
    REQUIRE(instance.get_group()->m_component_instance);
    REQUIRE(instance.m_children.size() == 1);
    REQUIRE(document.get(instance.m_children[0]).get_reference()->m_source ==
      ids.m_master_shape);
    auto& children = document.get_children(ids.m_artboard);
    REQUIRE(children[children.size() - 2] == card.get_id());
    REQUIRE(children.back() == instance.get_id());
    REQUIRE(editor.undo());
    REQUIRE(document.get_children(ids.m_artboard).size() == 3);
  }

  TEST_CASE("paste_without_container") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_background}));
    auto world = get_world_transform(document, ids.m_background);
    {
      auto transaction = Transaction(document);
      transaction.remove(ids.m_card);
      transaction.commit();
    }
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    auto copy = editor.get_selection().get_primary();
    REQUIRE(document.get(copy).m_parent == ids.m_artboard);
    REQUIRE(get_world_transform(document, copy) == world);
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    REQUIRE(editor.get_selection().get_primary() != copy);
    REQUIRE(document.get_children(ids.m_artboard).size() == 4);
  }

  TEST_CASE("placement") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto other = Artboard();
    other.m_frame = Rect(600, 50, 375, 812);
    auto second = NodeId();
    {
      auto transaction = Transaction(document);
      second = transaction.append(NodeId(), make_header("Second"), other);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_ellipse}));
    auto paste = [&] (const PasteTarget& target) {
      REQUIRE(editor.execute(clipboard.make_paste_command(target)));
      return editor.get_selection().get_primary();
    };
    auto in_place =
      paste(PasteTarget(NodeId(), NodeId(), Rect(0, 0, 1000, 1000)));
    REQUIRE(document.get(in_place).m_parent == ids.m_artboard);
    REQUIRE(get_world_transform(document, in_place) ==
      get_world_transform(document, ids.m_ellipse));
    auto into = paste(PasteTarget(second, NodeId(), Rect(0, 0, 1000, 1000)));
    REQUIRE(document.get(into).m_parent == second);
    REQUIRE(get_world_transform(document, into) == make_translation(600, 50));
    auto hidden = paste(PasteTarget(second, NodeId(), Rect(0, 0, 400, 400)));
    REQUIRE(document.get(hidden).m_parent == ids.m_artboard);
    auto centered = paste(
      PasteTarget(ids.m_artboard, NodeId(), Rect(2000, 0, 101, 81)));
    REQUIRE(document.is_on_canvas(centered));
    auto bounds = *get_world_bounds(document, centered);
    REQUIRE(bounds.m_x == std::round(2050.5 - bounds.m_width / 2));
    REQUIRE(bounds.m_y == std::round(40.5 - bounds.m_height / 2));
    auto over =
      paste(PasteTarget(NodeId(), NodeId(), Rect(400, 400, 400, 200)));
    REQUIRE(document.get(over).m_parent == second);
  }

  TEST_CASE("names") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto header = document.get(ids.m_card).m_header;
      header.m_name = "Group 3";
      auto transaction = Transaction(document);
      transaction.replace_header(header);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto paste = [&] {
      REQUIRE(editor.execute(clipboard.make_paste_command()));
      return editor.get_selection().get_primary();
    };
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    REQUIRE(document.get(paste()).m_header.m_name == "Group 4");
    REQUIRE(clipboard.copy(document, {ids.m_artboard}));
    auto board = paste();
    REQUIRE(document.get(board).m_header.m_name == "Home \xE2\x80\x93 1");
    REQUIRE(clipboard.copy(document, {board}));
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    REQUIRE(document.get(editor.get_selection().get_primary()).m_header.
      m_name == "Home \xE2\x80\x93 2");
  }

  TEST_CASE("main_component") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto group = *document.get(ids.m_instance).get_group();
      group.m_component_instance->m_is_main = true;
      auto transaction = Transaction(document);
      transaction.replace_payload(ids.m_instance, group);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_instance}));
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    auto& copy = document.get(editor.get_selection().get_primary());
    REQUIRE(!copy.get_group()->m_component_instance->m_is_main);
    REQUIRE(copy.get_group()->m_component_instance->m_component ==
      ids.m_component);
  }

  TEST_CASE("other_document") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_instance, ids.m_loose}));
    auto other = Document();
    auto editor = Editor(other);
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    auto& copies = editor.get_selection().get_nodes();
    REQUIRE(copies.size() == 2);
    REQUIRE(other.get_resources().m_components.contains(ids.m_component));
    REQUIRE(other.find(ids.m_master_shape));
    REQUIRE(get_world_transform(other, copies[0]) ==
      get_world_transform(document, ids.m_instance));
    REQUIRE(get_world_transform(other, copies[1]) ==
      get_world_transform(document, ids.m_loose));
    REQUIRE(editor.undo());
    REQUIRE(!other.find(ids.m_master_shape));
  }

  TEST_CASE("system_clipboard") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto clipboard = Clipboard();
    REQUIRE(!clipboard.make_mime_data());
    REQUIRE(clipboard.copy(document, {ids.m_card, ids.m_loose}));
    auto data = clipboard.make_mime_data();
    REQUIRE(Clipboard::is_loadable(*data));
    auto receiver = Clipboard();
    REQUIRE(receiver.load(*data));
    auto xd = QMimeData();
    for(auto& format : data->formats()) {
      if(format.contains("com.adobe.xd")) {
        xd.setData(format, data->data(format));
      }
    }
    REQUIRE(!xd.formats().isEmpty());
    auto foreign = Clipboard();
    REQUIRE(foreign.load(xd));
    auto other = Document();
    auto editor = Editor(other);
    REQUIRE(editor.execute(foreign.make_paste_command()));
    auto& copies = editor.get_selection().get_nodes();
    REQUIRE(copies.size() == 2);
    REQUIRE(other.get(copies[0]).m_header.m_name == "Card");
    REQUIRE(other.get(copies[0]).m_children.size() == 2);
    REQUIRE(get_world_transform(other, copies[0]) ==
      get_world_transform(document, ids.m_card));
    REQUIRE(get_world_transform(other, copies[1]) ==
      get_world_transform(document, ids.m_loose));
    auto artboard = Artboard();
    artboard.m_frame = Rect(1000, 0, 300, 300);
    auto board = NodeId();
    {
      auto transaction = Transaction(other);
      board = transaction.append(NodeId(), make_header("Board"), artboard);
      transaction.commit();
    }
    REQUIRE(editor.execute(
      foreign.make_paste_command(PasteTarget(NodeId(), board, std::nullopt))));
    auto& focused = editor.get_selection().get_nodes();
    REQUIRE(other.get(focused[0]).m_parent == board);
    REQUIRE(get_world_transform(other, focused[0]) ==
      make_translation(1010, 20));
    REQUIRE(other.is_on_canvas(focused[1]));
  }

  TEST_CASE("image_and_text") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto image = QImage(50, 40, QImage::Format_RGBA8888);
    image.fill(QColor(255, 128, 0));
    auto data = QMimeData();
    data.setImageData(image);
    REQUIRE(clipboard.load(data));
    auto view = Rect(0, 0, 600, 600);
    REQUIRE(editor.execute(
      clipboard.make_paste_command(PasteTarget(NodeId(), NodeId(), view))));
    auto& picture = document.get(editor.get_selection().get_primary());
    REQUIRE(picture.m_header.m_name == "Image 1");
    REQUIRE(picture.m_parent == ids.m_artboard);
    auto& fill = std::get<ImageFill>(picture.get_shape()->m_style.m_fill);
    REQUIRE(document.get_resources().m_images.contains(fill.m_image));
    REQUIRE(*get_world_bounds(document, picture.get_id()) ==
      Rect(275, 280, 50, 40));
    auto text = QMimeData();
    text.setText("Pasted words");
    REQUIRE(clipboard.load(text));
    REQUIRE(editor.execute(
      clipboard.make_paste_command(PasteTarget(NodeId(), NodeId(), view))));
    auto& label = document.get(editor.get_selection().get_primary());
    REQUIRE(label.m_header.m_name == "Pasted words");
    REQUIRE(*label.get_text()->m_content == u"Pasted words");
    REQUIRE(get_world_transform(document, label.get_id()) ==
      make_translation(150, 150));
  }
}

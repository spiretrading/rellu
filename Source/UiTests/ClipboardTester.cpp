#include <vector>
#include <doctest/doctest.h>
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
    REQUIRE(document.is_on_canvas(copy));
    REQUIRE(get_world_transform(document, copy) == world);
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    REQUIRE(editor.get_selection().get_primary() != copy);
    REQUIRE(document.get_canvas().size() == 4);
  }
}

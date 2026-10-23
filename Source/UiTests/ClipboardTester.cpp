#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <doctest/doctest.h>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Ui/Clipboard.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  NodeId add_artboard(Document& document, std::string name, Rect frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    auto transaction = Transaction(document);
    auto id =
      transaction.append(NodeId(), make_header(std::move(name)), artboard);
    transaction.commit();
    return id;
  }

  void rename(Document& document, const NodeId& id, std::string name) {
    auto header = document.get(id).m_header;
    header.m_name = std::move(name);
    header.m_has_custom_name = false;
    auto transaction = Transaction(document);
    transaction.replace_header(header);
    transaction.commit();
  }

  PasteTarget make_target(
      std::vector<NodeId> selection, std::optional<Rect> view) {
    auto target = PasteTarget();
    target.m_selection = std::move(selection);
    target.m_view = view;
    return target;
  }
}

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
    auto second = add_artboard(document, "Second", Rect(600, 50, 375, 812));
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_ellipse}));
    auto paste = [&] (const PasteTarget& target) {
      REQUIRE(editor.execute(clipboard.make_paste_command(target)));
      return editor.get_selection().get_primary();
    };
    auto in_place = paste(make_target({}, Rect(0, 0, 1000, 1000)));
    REQUIRE(document.get(in_place).m_parent == ids.m_artboard);
    REQUIRE(get_world_transform(document, in_place) ==
      get_world_transform(document, ids.m_ellipse));
    auto into = paste(make_target({second}, Rect(0, 0, 1000, 1000)));
    REQUIRE(document.get(into).m_parent == second);
    REQUIRE(get_world_transform(document, into) == make_translation(600, 50));
    auto hidden = paste(make_target({second}, Rect(0, 0, 400, 400)));
    REQUIRE(document.get(hidden).m_parent == ids.m_artboard);
    auto centered =
      paste(make_target({ids.m_artboard}, Rect(2000, 0, 101, 81)));
    REQUIRE(document.is_on_canvas(centered));
    REQUIRE(*get_world_bounds(document, centered) == Rect(2046, 38, 10, 6));
    auto over = paste(make_target({}, Rect(400, 400, 400, 200)));
    REQUIRE(document.get(over).m_parent == second);
    auto focused = make_target({}, Rect(0, 0, 1000, 1000));
    focused.m_focus = second;
    auto focus = paste(focused);
    REQUIRE(document.get(focus).m_parent == second);
    REQUIRE(get_world_transform(document, focus) == make_translation(600, 50));
  }

  TEST_CASE("placement_by_document") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_loose}, estimate_text_bounds, {},
      XdClipboardSource("A", "C:\\Designs\\A.xd")));
    auto bounds = *get_world_bounds(document, ids.m_loose);
    auto paste = [&] (std::string id) {
      auto target = make_target({}, Rect(bounds.m_x - 50, bounds.m_y - 50,
        bounds.m_width + 60, bounds.m_height + 60));
      target.m_document = std::move(id);
      REQUIRE(editor.execute(clipboard.make_paste_command(target)));
      return *get_world_bounds(document, editor.get_selection().get_primary());
    };
    REQUIRE(paste("A") == bounds);
    REQUIRE(paste("B") != bounds);
  }

  TEST_CASE("pasteboard_into_artboard") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto second = add_artboard(document, "Second", Rect(600, 50, 375, 812));
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_loose}));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({second}, Rect(0, 0, 1000, 1000)))));
    auto copy = editor.get_selection().get_primary();
    REQUIRE(document.get(copy).m_parent == second);
    REQUIRE(*get_world_bounds(document, copy) == Rect(785, 454, 5, 5));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({second}, Rect(500, 0, 200, 100)))));
    REQUIRE(*get_world_bounds(document,
      editor.get_selection().get_primary()) == Rect(648, 73, 5, 5));
  }

  TEST_CASE("container") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_ellipse}));
    auto target = make_target({}, Rect(0, 0, 1000, 1000));
    target.m_focus = ids.m_artboard;
    target.m_container = ids.m_card;
    REQUIRE(editor.execute(clipboard.make_paste_command(target)));
    auto copy = editor.get_selection().get_primary();
    REQUIRE(document.get(copy).m_parent == ids.m_card);
    REQUIRE(document.get(ids.m_card).m_children.back() == copy);
    REQUIRE(get_world_transform(document, copy) ==
      get_world_transform(document, ids.m_ellipse));
  }

  TEST_CASE("selected_artboards") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto second = add_artboard(document, "Second", Rect(600, 50, 375, 812));
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_ellipse}));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({ids.m_artboard, second}, Rect(0, 0, 1000, 1000)))));
    auto& copies = editor.get_selection().get_nodes();
    REQUIRE(copies.size() == 2);
    REQUIRE(document.get(copies[0]).m_parent == ids.m_artboard);
    REQUIRE(document.get(copies[1]).m_parent == second);
    REQUIRE(get_world_transform(document, copies[1]) ==
      make_translation(600, 50));
  }

  TEST_CASE("overflow") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto second = add_artboard(document, "Second", Rect(600, 50, 375, 812));
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_ellipse, ids.m_loose}));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({second}, Rect(590, 40, 40, 40)))));
    auto& selection = editor.get_selection().get_nodes();
    REQUIRE(selection.size() == 1);
    auto& group = document.get(selection.front());
    REQUIRE(group.get_group());
    REQUIRE(group.m_header.m_name == "Group 1");
    REQUIRE(group.m_parent == second);
    REQUIRE(group.m_children.size() == 2);
  }

  TEST_CASE("names") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto second = add_artboard(document, "Second", Rect(600, 50, 375, 812));
    rename(document, ids.m_card, "Group 3");
    rename(document, ids.m_ellipse, "Ellipse 2");
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto paste = [&] (const PasteTarget& target) {
      REQUIRE(editor.execute(clipboard.make_paste_command(target)));
      return editor.get_selection().get_primary();
    };
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    auto group = paste(PasteTarget());
    REQUIRE(document.get(group).m_header.m_name == "Group 4");
    REQUIRE(document.get(document.get(group).m_children.front()).m_header.
      m_name == "Background");
    REQUIRE(clipboard.copy(document, {ids.m_ellipse}));
    REQUIRE(document.get(paste(PasteTarget())).m_header.m_name ==
      "Ellipse 3");
    REQUIRE(document.get(paste(make_target({second}, std::nullopt))).
      m_header.m_name == "Ellipse 2");
    REQUIRE(clipboard.copy(document, {ids.m_instance}));
    REQUIRE(document.get(paste(PasteTarget())).m_header.m_name ==
      "Button \xE2\x80\x93 1");
    REQUIRE(document.get(paste(PasteTarget())).m_header.m_name ==
      "Button \xE2\x80\x93 2");
    REQUIRE(clipboard.copy(document, {ids.m_artboard}));
    auto board = paste(PasteTarget());
    REQUIRE(document.get(board).m_header.m_name == "Home \xE2\x80\x93 1");
    REQUIRE(clipboard.copy(document, {board}));
    REQUIRE(document.get(paste(PasteTarget())).m_header.m_name ==
      "Home \xE2\x80\x93 2");
  }

  TEST_CASE("artboards") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_artboard, ids.m_loose}));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({ids.m_artboard}, Rect(0, 0, 2000, 1000)))));
    auto& copies = editor.get_selection().get_nodes();
    REQUIRE(copies.size() == 1);
    auto& copy = document.get(copies.front());
    REQUIRE(copy.get_artboard()->m_frame == Rect(545, 50, 375, 812));
    REQUIRE(copy.m_children.size() == 3);
    REQUIRE(document.get_canvas()[1] == copy.get_id());
    REQUIRE(clipboard.copy(document, {ids.m_artboard}));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({ids.m_artboard}, Rect(0, 0, 2000, 1000)))));
    REQUIRE(document.get(editor.get_selection().get_primary()).
      get_artboard()->m_frame == Rect(990, 50, 375, 812));
  }

  TEST_CASE("artboard_grids") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_artboard}));
    auto other = Document();
    auto layout = GridOverride();
    layout.m_maximum_width = 2000;
    layout.m_column_count = 3;
    layout.m_gutter = 20;
    auto defaults = GridDefaults();
    defaults.m_overrides = std::vector{layout};
    {
      auto transaction = Transaction(other);
      transaction.set_grid_defaults(defaults);
      transaction.commit();
    }
    auto editor = Editor(other);
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    auto pasted = editor.get_selection().get_primary();
    auto& artboard = *other.get(pasted).get_artboard();
    REQUIRE(artboard.m_layout_grid == make_initial_grid(artboard, defaults));
    REQUIRE(artboard.m_layout_grid->m_column_count == 3);
    REQUIRE(!document.get(ids.m_artboard).get_artboard()->m_layout_grid);
    auto target = PasteTarget();
    target.m_is_prototyping = true;
    REQUIRE(editor.execute(clipboard.make_paste_command(target)));
    REQUIRE(!other.get(editor.get_selection().get_primary()).get_artboard()->
      m_layout_grid);
    REQUIRE(clipboard.copy(other, {pasted}));
    auto grid = *other.get(pasted).get_artboard()->m_layout_grid;
    auto source_editor = Editor(document);
    REQUIRE(source_editor.execute(clipboard.make_paste_command()));
    REQUIRE(document.get(source_editor.get_selection().get_primary()).
      get_artboard()->m_layout_grid == grid);
  }

  TEST_CASE("interactions") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto transaction = Transaction(document);
      transaction.add_interaction(Interaction(NodeId(), ids.m_ellipse,
        TapTrigger(), ArtboardTransition(ids.m_artboard)));
      transaction.commit();
    }
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_ellipse}));
    REQUIRE(editor.execute(clipboard.make_paste_command()));
    REQUIRE(document.get_prototype().m_interactions.size() == 1);
    auto target = PasteTarget();
    target.m_is_prototyping = true;
    REQUIRE(editor.execute(clipboard.make_paste_command(target)));
    auto& interactions = document.get_prototype().m_interactions;
    REQUIRE(interactions.size() == 2);
    REQUIRE(interactions.back().m_owner ==
      editor.get_selection().get_primary());
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
    REQUIRE(other.get(copies[1]).m_header.m_name == "Loose");
    REQUIRE(editor.undo());
    REQUIRE(!other.find(ids.m_master_shape));
  }

  TEST_CASE("other_document_components") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto component = document.get_resources().m_components.at(
        ids.m_component);
      component.m_link = ComponentLink("C:\\Designs\\A.xd", "A", 0);
      auto transaction = Transaction(document);
      transaction.replace_component(component);
      transaction.commit();
    }
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_instance}, estimate_text_bounds,
      {}, XdClipboardSource("A", "C:\\Designs\\A.xd")));
    auto other = Document();
    auto editor = Editor(other);
    auto target = PasteTarget();
    target.m_document = "B";
    REQUIRE(editor.execute(clipboard.make_paste_command(target)));
    auto& copy = other.get(editor.get_selection().get_primary());
    REQUIRE(get_world_transform(other, copy.get_id()) ==
      get_world_transform(document, ids.m_instance));
    REQUIRE(copy.m_header.m_name == "Button \xE2\x80\x93 1");
    auto& instance = *copy.get_group()->m_component_instance;
    REQUIRE(instance.m_component != ids.m_component);
    auto& components = other.get_resources().m_components;
    REQUIRE(components.size() == 1);
    auto& component = components.at(instance.m_component);
    REQUIRE(!component.m_link);
    REQUIRE(component.m_name == "Button");
    REQUIRE(component.m_master != ids.m_master);
    REQUIRE(!other.find(ids.m_master_shape));
    auto& master = other.get(component.m_master);
    REQUIRE(master.m_children.size() == 1);
    auto& reference = other.get(copy.m_children.front());
    REQUIRE(reference.get_reference()->m_source == master.m_children.front());
    auto data = clipboard.make_mime_data();
    auto xd = QMimeData();
    for(auto& format : data->formats()) {
      if(format.contains("com.adobe.xd")) {
        xd.setData(format, data->data(format));
      }
    }
    auto foreign = Clipboard();
    REQUIRE(foreign.load(xd));
    auto pasted = Document();
    auto pasted_editor = Editor(pasted);
    REQUIRE(pasted_editor.execute(foreign.make_paste_command(target)));
    auto& pasted_components = pasted.get_resources().m_components;
    REQUIRE(pasted_components.size() == 1);
    REQUIRE(!pasted_components.contains(ids.m_component));
    REQUIRE(!pasted_components.begin()->second.m_link);
    auto same = Clipboard();
    REQUIRE(same.copy(document, {ids.m_instance}, estimate_text_bounds, {},
      XdClipboardSource("A", "C:\\Designs\\A.xd")));
    auto again = Document();
    auto again_editor = Editor(again);
    REQUIRE(again_editor.execute(same.make_paste_command()));
    REQUIRE(again.get_resources().m_components.contains(ids.m_component));
  }

  TEST_CASE("library_links") {
    auto is_kept = [] (std::string reference) {
      auto document = Document();
      auto ids = build_test_document(document);
      auto component =
        document.get_resources().m_components.at(ids.m_component);
      component.m_link = ComponentLink(std::move(reference), "Library", 0);
      auto transaction = Transaction(document);
      transaction.replace_component(component);
      transaction.commit();
      auto clipboard = Clipboard();
      REQUIRE(clipboard.copy(document, {ids.m_instance}, estimate_text_bounds,
        {}, XdClipboardSource("A", "C:\\Designs\\A.xd")));
      auto other = Document();
      auto editor = Editor(other);
      auto target = PasteTarget();
      target.m_document = "B";
      REQUIRE(editor.execute(clipboard.make_paste_command(target)));
      auto& components = other.get_resources().m_components;
      REQUIRE(components.size() == 1);
      return static_cast<bool>(components.begin()->second.m_link);
    };
    REQUIRE(is_kept("cloud-asset://library/element"));
    REQUIRE(is_kept(R"({"libraryUrn":"urn:aaid:sc:US:1"})"));
    REQUIRE(is_kept("xdcloud:///cloud-asset%3A%2F%2Flibrary"));
    REQUIRE(!is_kept("{library"));
    REQUIRE(!is_kept("https://xd.adobe.com/view/document"));
    REQUIRE(!is_kept(
      "data:text/plain;path=C%3A%5CA.xd;relativePath=A.xd,"));
  }

  TEST_CASE("linked_documents") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto path = std::string("C:/Designs/A.xd");
    auto clipboard = Clipboard();
    REQUIRE(clipboard.get_linked_documents(XdClipboardSource()).empty());
    REQUIRE(clipboard.copy(document, {ids.m_instance}, estimate_text_bounds,
      {}, XdClipboardSource("A", path)));
    auto target = XdClipboardSource("B", "C:/Designs/B.xd");
    REQUIRE(clipboard.get_linked_documents(target) == std::vector{path});
    REQUIRE(clipboard.get_linked_documents(
      XdClipboardSource("A", path)).empty());
    REQUIRE(clipboard.get_linked_documents(
      XdClipboardSource("C", path)).empty());
    auto data = clipboard.make_mime_data();
    auto xd = QMimeData();
    for(auto& format : data->formats()) {
      if(format.contains("com.adobe.xd")) {
        xd.setData(format, data->data(format));
      }
    }
    auto foreign = Clipboard();
    REQUIRE(foreign.load(xd));
    REQUIRE(foreign.get_linked_documents(target) == std::vector{path});
    auto unsaved = Clipboard();
    REQUIRE(unsaved.copy(document, {ids.m_instance}, estimate_text_bounds,
      {}, XdClipboardSource("A", "")));
    REQUIRE(unsaved.get_linked_documents(target).empty());
    {
      auto component = document.get_resources().m_components.at(
        ids.m_component);
      component.m_link = ComponentLink("{\"id\":\"1\"}", "Library", 0);
      auto transaction = Transaction(document);
      transaction.replace_component(component);
      transaction.commit();
    }
    auto library = Clipboard();
    REQUIRE(library.copy(document, {ids.m_instance}, estimate_text_bounds,
      {}, XdClipboardSource("A", path)));
    REQUIRE(library.get_linked_documents(target).empty());
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
    auto get_xd = [] (const QMimeData& data) {
      auto xd = std::make_unique<QMimeData>();
      for(auto& format : data.formats()) {
        if(format.contains("com.adobe.xd")) {
          xd->setData(format, data.data(format));
        }
      }
      return xd;
    };
    auto xd = get_xd(*data);
    REQUIRE(!xd->formats().isEmpty());
    auto foreign = Clipboard();
    REQUIRE(foreign.load(*xd));
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
    auto board = add_artboard(other, "Board", Rect(1000, 0, 300, 300));
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    REQUIRE(foreign.load(*get_xd(*clipboard.make_mime_data())));
    auto target = PasteTarget();
    target.m_focus = board;
    REQUIRE(editor.execute(foreign.make_paste_command(target)));
    auto focused = editor.get_selection().get_primary();
    REQUIRE(other.get(focused).m_parent == board);
    REQUIRE(get_world_transform(other, focused) ==
      make_translation(1010, 20));
  }

  TEST_CASE("paste_appearance") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto source = make_rectangle_shape(40, 40);
    std::get<RectangleShape>(source.m_geometry).m_radii =
      CornerRadii(5, 5, 5, 5);
    source.m_style.m_fill = SolidFill(make_color(0, 0, 255));
    source.m_style.m_stroke = Stroke(make_color(255, 0, 0), 3);
    source.m_style.m_effects.push_back(DropShadow(Point(0, 3), 6,
      make_color(0, 0, 0), true));
    auto header = make_header("Source");
    header.m_opacity = 0.5;
    auto rectangle = NodeId();
    {
      auto transaction = Transaction(document);
      rectangle = transaction.append(ids.m_artboard, header, source);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(!clipboard.make_paste_appearance_command({ids.m_ellipse}));
    REQUIRE(clipboard.copy(document, {rectangle}));
    REQUIRE(!clipboard.make_paste_appearance_command({}));
    auto command = clipboard.make_paste_appearance_command(
      {ids.m_ellipse, ids.m_background});
    REQUIRE(command->get_label() == "Paste Appearance");
    REQUIRE(editor.execute(std::move(command)));
    REQUIRE(editor.execute(
      clipboard.make_paste_appearance_command({ids.m_artboard})));
    auto& ellipse = document.get(ids.m_ellipse);
    REQUIRE(ellipse.m_header.m_opacity == 0.5);
    REQUIRE(ellipse.get_shape()->m_style == source.m_style);
    REQUIRE(std::holds_alternative<EllipseShape>(
      ellipse.get_shape()->m_geometry));
    auto& background = *document.get(ids.m_background).get_shape();
    REQUIRE(std::get<RectangleShape>(background.m_geometry).m_radii ==
      CornerRadii(5, 5, 5, 5));
    REQUIRE(document.get(ids.m_artboard).get_artboard()->m_background ==
      source.m_style.m_fill);
    REQUIRE(editor.execute(
      clipboard.make_paste_appearance_command({ids.m_label})));
    auto& label = *document.get(ids.m_label).get_text();
    REQUIRE(get_runs(label).front().m_style.m_color ==
      make_color(0, 0, 255));
    REQUIRE(!label.m_style.m_stroke->m_is_enabled);
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    REQUIRE(editor.execute(
      clipboard.make_paste_appearance_command({ids.m_loose})));
    auto& loose = *document.get(ids.m_loose).get_shape();
    REQUIRE(loose.m_style.m_fill == Fill(SolidFill(make_color(0, 0, 255))));
    REQUIRE(clipboard.copy(document, {ids.m_ellipse, ids.m_loose}));
    REQUIRE(!clipboard.make_paste_appearance_command({ids.m_card}));
    auto text = QMimeData();
    text.setText("Words");
    REQUIRE(clipboard.load(text));
    REQUIRE(!clipboard.make_paste_appearance_command({ids.m_card}));
  }

  TEST_CASE("group_appearance") {
    auto document = Document();
    auto ids = build_test_document(document);
    {
      auto loose = *document.get(ids.m_loose).get_shape();
      std::get<RectangleShape>(loose.m_geometry).m_radii =
        CornerRadii(3, 3, 3, 3);
      loose.m_style.m_stroke = Stroke(make_color(0, 255, 0), 4);
      loose.m_style.m_effects.push_back(DropShadow(Point(0, 3), 6,
        make_color(0, 0, 0), true));
      auto label = *document.get(ids.m_label).get_text();
      label.m_style.m_fill = SolidFill(make_color(255, 0, 0));
      auto transaction = Transaction(document);
      transaction.replace_payload(ids.m_loose, loose);
      transaction.replace_payload(ids.m_label, label);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    REQUIRE(editor.execute(
      clipboard.make_paste_appearance_command({ids.m_loose})));
    auto& loose = *document.get(ids.m_loose).get_shape();
    REQUIRE(loose.m_style.m_fill == Fill(SolidFill(make_color(255, 0, 0))));
    REQUIRE(loose.m_style.m_effects.empty());
    REQUIRE(std::get<RectangleShape>(loose.m_geometry).m_radii ==
      CornerRadii(0, 0, 0, 0));
    REQUIRE(loose.m_style.m_stroke->m_is_enabled);
    REQUIRE(loose.m_style.m_stroke->m_color == make_color(0, 255, 0));
  }

  TEST_CASE("paste_into_visible_artboard") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_ellipse}));
    auto target = make_target({}, Rect(300, 400, 400, 200));
    target.m_focus = ids.m_artboard;
    REQUIRE(editor.execute(clipboard.make_paste_command(target)));
    auto& copy = document.get(editor.get_selection().get_primary());
    REQUIRE(copy.m_parent == ids.m_artboard);
    REQUIRE(*get_world_bounds(document, copy.get_id()) ==
      Rect(383, 497, 10, 6));
  }

  TEST_CASE("xd_components_and_images") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto image = ImageResource();
    image.m_id = generate_node_id();
    image.m_media_type = "image/png";
    image.m_data = Shared(std::vector<std::byte>(
      {std::byte(1), std::byte(2), std::byte(3)}));
    {
      auto transaction = Transaction(document);
      transaction.add_image(image);
      auto shape = *document.get(ids.m_master_shape).get_shape();
      auto fill = ImageFill();
      fill.m_image = image.m_id;
      fill.m_size = Size(8, 8);
      shape.m_style.m_fill = fill;
      transaction.replace_payload(ids.m_master_shape, Box<Shape>(shape));
      transaction.commit();
    }
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_instance}));
    auto data = clipboard.make_mime_data();
    auto xd = std::make_unique<QMimeData>();
    for(auto& format : data->formats()) {
      if(format.contains("com.adobe.xd")) {
        xd->setData(format, data->data(format));
      }
    }
    auto foreign = Clipboard();
    REQUIRE(foreign.load(*xd));
    auto other = Document();
    auto editor = Editor(other);
    REQUIRE(editor.execute(foreign.make_paste_command()));
    auto& copy = other.get(editor.get_selection().get_primary());
    auto& instance = *copy.get_group()->m_component_instance;
    REQUIRE(instance.m_component == ids.m_component);
    auto& components = other.get_resources().m_components;
    REQUIRE(components.contains(ids.m_component));
    auto& master = other.get(components.at(ids.m_component).m_master);
    REQUIRE(master.m_children.size() == 1);
    auto& fill = std::get<ImageFill>(
      other.get(master.m_children.front()).get_shape()->m_style.m_fill);
    REQUIRE(fill.m_image == image.m_id);
    REQUIRE(other.get_resources().m_images.at(image.m_id).m_data ==
      image.m_data);
  }

  TEST_CASE("image_files") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto image = ImageResource();
    image.m_id = generate_node_id();
    image.m_media_type = "image/png";
    image.m_data = Shared(std::vector<std::byte>({std::byte(9)}));
    {
      auto transaction = Transaction(document);
      transaction.add_image(image);
      auto shape = *document.get(ids.m_background).get_shape();
      auto fill = ImageFill();
      fill.m_image = image.m_id;
      fill.m_size = Size(1, 1);
      shape.m_style.m_fill = fill;
      transaction.replace_payload(ids.m_background, Box<Shape>(shape));
      transaction.commit();
    }
    auto temporary = QTemporaryDir();
    REQUIRE(temporary.isValid());
    auto directory = std::filesystem::path(temporary.path().toStdU16String());
    auto list = [&] {
      auto paths = std::vector<std::filesystem::path>();
      for(auto& entry : std::filesystem::directory_iterator(directory)) {
        paths.push_back(entry.path());
      }
      return paths;
    };
    auto clipboard = Clipboard(directory);
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    auto first = list();
    REQUIRE(first.size() == 1);
    REQUIRE(std::filesystem::is_regular_file(
      first.front() / to_uid(image.m_id)));
    auto other = Clipboard(directory);
    REQUIRE(other.copy(document, {ids.m_card}));
    auto second = list();
    REQUIRE(second.size() == 1);
    REQUIRE(second.front() != first.front());
    REQUIRE(clipboard.copy(document, {ids.m_loose}));
    REQUIRE(list().empty());
    REQUIRE(clipboard.copy(document, {ids.m_card}));
    auto current = list().front();
    auto data = clipboard.make_mime_data();
    auto now = std::filesystem::last_write_time(current);
    auto older = directory / "older";
    std::filesystem::create_directory(older);
    std::filesystem::last_write_time(older, now - std::chrono::hours(1));
    auto stray = directory / "stray";
    std::ofstream(stray).put('x');
    std::filesystem::last_write_time(stray, now - std::chrono::hours(1));
    auto later = directory / "later";
    std::filesystem::create_directory(later);
    std::filesystem::last_write_time(later, now + std::chrono::hours(1));
    Clipboard::remove_stale_images(directory, data.get());
    REQUIRE(std::filesystem::exists(current));
    REQUIRE(std::filesystem::exists(later));
    REQUIRE(!std::filesystem::exists(older));
    REQUIRE(!std::filesystem::exists(stray));
    auto foreign = QMimeData();
    foreign.setText("Other");
    Clipboard::remove_stale_images(directory, &foreign);
    REQUIRE(list().empty());
  }

  TEST_CASE("svg") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto text = QMimeData();
    text.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><rect
      width="40" height="20" fill="#0f0"/><circle cx="5" cy="5"
      r="5"/></svg>)svg");
    REQUIRE(Clipboard::is_loadable(text));
    REQUIRE(clipboard.load(text));
    auto view = Rect(0, 0, 600, 600);
    REQUIRE(editor.execute(
      clipboard.make_paste_command(make_target({}, view))));
    auto& group = document.get(editor.get_selection().get_primary());
    REQUIRE(group.m_parent == ids.m_artboard);
    REQUIRE(group.get_group());
    REQUIRE(group.m_children.size() == 2);
    REQUIRE(group.m_header.m_name == "Group 1");
    auto& box = document.get(group.m_children.front());
    REQUIRE(box.m_header.m_name == "Rectangle 1");
    REQUIRE(box.get_shape()->m_style.m_fill ==
      Fill(SolidFill(make_color(0, 255, 0))));
    REQUIRE(document.get(group.m_children.back()).m_header.m_name ==
      "Ellipse 1");
    auto markup = QMimeData();
    markup.setData(
      "application/x-qt-windows-mime;value=\"Scalable Vector Graphics\"",
      R"svg(<svg
      xmlns="http://www.w3.org/2000/svg"><path id="Mark"
      d="M0 0L5 5"/></svg>)svg");
    REQUIRE(clipboard.load(markup));
    REQUIRE(editor.execute(
      clipboard.make_paste_command(make_target({}, view))));
    auto& mark = document.get(editor.get_selection().get_primary());
    REQUIRE(mark.m_header.m_name == "Mark");
    REQUIRE(std::holds_alternative<PathShape>(
      mark.get_shape()->m_geometry));
  }

  TEST_CASE("svg_failures") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {ids.m_loose}));
    auto text = QMimeData();
    text.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs><rect
      id="r" width="1" height="1"/></defs><use href="#r"/><circle r="5"/>
      </svg>)svg");
    REQUIRE(!clipboard.load(text));
    REQUIRE(!clipboard.make_paste_command());
    auto failed = QMimeData();
    failed.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs><filter
      id="u"><feOffset dx="5px" dy="2"/><feGaussianBlur stdDeviation="2"/>
      <feFlood/><feComposite operator="in"/></filter></defs><rect width="1"
      height="1" filter="url(#u)"/></svg>)svg");
    REQUIRE(clipboard.load(failed));
    auto editor = Editor(document);
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({}, Rect(0, 0, 600, 600)))));
    auto& empty = document.get(editor.get_selection().get_primary());
    REQUIRE(empty.get_group());
    REQUIRE(empty.m_children.empty());
    REQUIRE(get_world_transform(document, empty.get_id()) == Matrix());
    auto mixed = QMimeData();
    mixed.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><defs><filter
      id="u"><feOffset dx="5px" dy="2"/><feGaussianBlur stdDeviation="2"/>
      <feFlood/><feComposite operator="in"/></filter></defs><g><rect
      width="50" height="50" filter="url(#u)"/></g><circle cx="200" cy="25"
      r="25"/></svg>)svg");
    REQUIRE(clipboard.load(mixed));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({}, Rect(0, 0, 600, 600)))));
    auto& placed = document.get(editor.get_selection().get_primary());
    REQUIRE(placed.m_children.size() == 2);
    auto circle = *get_world_bounds(document, placed.m_children[1]);
    auto& anchored = document.get(placed.m_children[0]);
    REQUIRE(anchored.m_children.empty());
    REQUIRE(get_world_transform(document, anchored.get_id()) == Matrix());
    auto lone = QMimeData();
    lone.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><circle
      cx="200" cy="25" r="25"/></svg>)svg");
    REQUIRE(clipboard.load(lone));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({}, Rect(0, 0, 600, 600)))));
    REQUIRE(*get_world_bounds(document, editor.get_selection().get_primary())
      == circle);
  }

  TEST_CASE("svg_fonts") {
    auto document = Document();
    build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto text = QMimeData();
    text.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text x="10"
      y="30" font-family="NoSuchFont, Arial">Two</text><text x="10" y="70"
      font-family="'Courier New', Arial">One</text></svg>)svg");
    REQUIRE(clipboard.load(text));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({}, Rect(0, 0, 600, 600)))));
    auto& group = document.get(editor.get_selection().get_primary());
    REQUIRE(group.m_children.size() == 2);
    auto& missing = document.get(group.m_children[0]).get_text()->
      m_base_style.m_font;
    REQUIRE(missing.m_family == "Segoe UI");
    REQUIRE(missing.m_postscript_name == "SegoeUI");
    auto& courier = document.get(group.m_children[1]).get_text()->
      m_base_style.m_font;
    REQUIRE(courier.m_family == "Courier New");
    REQUIRE(courier.m_postscript_name == "CourierNewPSMT");
  }

  TEST_CASE("svg_line_spacing") {
    auto document = Document();
    build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto text = QMimeData();
    text.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text
      font-family="Arial" font-size="20"><tspan x="10" y="30">A</tspan><tspan
      x="10" y="53">B</tspan><tspan x="10" y="106">C</tspan></text>
      </svg>)svg");
    REQUIRE(clipboard.load(text));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({}, Rect(0, 0, 600, 600)))));
    auto& node = document.get(editor.get_selection().get_primary());
    auto& pasted = *node.get_text();
    REQUIRE(*pasted.m_content == u"A\nB\n\nC");
    REQUIRE(pasted.m_runs.size() == 3);
    REQUIRE(pasted.m_runs[1].m_style.m_font.m_size == 27.42729949951172);
    auto spacer = [&] (const QString& family, int size, int y2, int y3) {
      auto markup = QMimeData();
      markup.setText(QString(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
        ><text font-family="%1" font-size="%2"><tspan x="10" y="30">A</tspan
        ><tspan x="10" y="%3">B</tspan><tspan x="10" y="%4">C</tspan></text>
        </svg>)svg").arg(family).arg(size).arg(y2).arg(y3));
      REQUIRE(clipboard.load(markup));
      REQUIRE(editor.execute(clipboard.make_paste_command(
        make_target({}, Rect(0, 0, 600, 600)))));
      auto& spaced = *document.get(
        editor.get_selection().get_primary()).get_text();
      REQUIRE(spaced.m_runs.size() == 3);
      return spaced.m_runs[1].m_style.m_font.m_size;
    };
    REQUIRE(spacer("Georgia", 20, 53, 103) == 24.014089584350586);
    REQUIRE(spacer("Arial", 18, 51, 97) == 23.19403076171875);
    REQUIRE(spacer("Arial", 15, 47, 86) == 19.904529571533203);
  }

  TEST_CASE("svg_split_positions") {
    auto document = Document();
    build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto positions = [&] (const QString& markup) {
      auto text = QMimeData();
      text.setText(markup);
      REQUIRE(clipboard.load(text));
      REQUIRE(editor.execute(clipboard.make_paste_command(
        make_target({}, Rect(0, 0, 600, 600)))));
      auto& group = document.get(editor.get_selection().get_primary());
      auto result = std::vector<double>();
      for(auto& child : group.m_children) {
        result.push_back(document.get(child).m_header.m_transform.m_tx);
      }
      return result;
    };
    auto make = [] (const QString& family, const QString& size,
        const QString& x, const QStringList& runs) {
      auto spans = QString();
      for(auto i = 0; i != runs.size(); ++i) {
        spans += QString(R"(<tspan%1>%2</tspan>)").arg(
          i % 2 == 0 ? R"( stroke="#f00")" : "", runs[i]);
      }
      return QString(R"svg(<svg xmlns="http://www.w3.org/2000/svg"><text
        x="%1" y="30" font-family="%2" font-size="%3">%4</text></svg>)svg").
        arg(x, family, size, spans);
    };
    REQUIRE(positions(make("Arial", "20", "10", {"W", "i", "m"})) ==
      std::vector<double>({10, 29, 33}));
    REQUIRE(positions(make("Times New Roman", "17", "10.4", {"A", "m", "i"}))
      == std::vector<double>({10.399999618530273, 22.399999618530273,
        35.400001525878906}));
    REQUIRE(positions(make("Georgia", "31", "10", {"m", "W", "i"})) ==
      std::vector<double>({10, 37, 68}));
    REQUIRE(positions(make("Verdana", "13", "10.6", {"i", "i", "i", "i"})) ==
      std::vector<double>({10.600000381469727, 14.600000381469727,
        18.600000381469727, 22.600000381469727}));
    REQUIRE(positions(make("Arial", "23", "10", {"mmm", "A", "W"})) ==
      std::vector<double>({10, 67, 82}));
  }

  TEST_CASE("svg_group") {
    auto document = Document();
    build_test_document(document);
    auto artboard =
      add_artboard(document, "Square", Rect(1000, 0, 400, 400));
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto text = QMimeData();
    text.setText(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200"
      height="150"><g transform="translate(50,30)"><rect x="10" y="20"
      width="40" height="30" fill="#ff8800"/><rect x="70" y="60" width="30"
      height="20" fill="#0088ff"/></g></svg>)svg");
    REQUIRE(clipboard.load(text));
    REQUIRE(editor.execute(clipboard.make_paste_command(
      make_target({artboard}, Rect(0, 0, 2000, 2000)))));
    auto& selection = editor.get_selection().get_nodes();
    REQUIRE(selection.size() == 1);
    auto& group = document.get(selection.front());
    REQUIRE(group.m_parent == artboard);
    REQUIRE(group.m_header.m_name == "Group 1");
    REQUIRE(group.m_header.m_transform == make_translation(145, 150));
    REQUIRE(group.m_children.size() == 2);
    auto& first = document.get(group.m_children[0]);
    REQUIRE(first.m_header.m_name == "Rectangle 1");
    REQUIRE(first.m_header.m_transform == make_translation(10, 20));
    auto& second = document.get(group.m_children[1]);
    REQUIRE(second.m_header.m_name == "Rectangle 2");
    REQUIRE(second.m_header.m_transform == make_translation(70, 60));
  }

  TEST_CASE("svg_features") {
    auto document = Document();
    build_test_document(document);
    auto artboard =
      add_artboard(document, "Board", Rect(1000, 0, 420, 320));
    auto editor = Editor(document);
    auto paste = [&] (const QString& markup) -> const Node& {
      auto clipboard = Clipboard();
      auto text = QMimeData();
      text.setText(markup);
      REQUIRE(clipboard.load(text));
      REQUIRE(editor.execute(clipboard.make_paste_command(
        make_target({artboard}, Rect(0, 0, 3000, 3000)))));
      return document.get(editor.get_selection().get_primary());
    };
    auto& clipped = paste(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      width="200" height="200"><defs><clipPath id="c"><circle cx="100"
      cy="100" r="60"/></clipPath></defs><rect x="20" y="20" width="160"
      height="160" fill="#22aa66" clip-path="url(#c)"/></svg>)svg");
    REQUIRE(clipped.m_header.m_name == "Group 1");
    REQUIRE(clipped.get_group()->m_mask);
    REQUIRE(clipped.m_header.m_transform == make_translation(130, 80));
    auto& switched = paste(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      width="160" height="100"><switch><foreignObject width="1"
      height="1"/><g><rect x="20" y="20" width="100" height="60"
      fill="#996633"/></g></switch></svg>)svg");
    REQUIRE(switched.m_header.m_name == "Group 2");
    REQUIRE(switched.m_parent == artboard);
    REQUIRE(switched.m_children.empty());
    REQUIRE(switched.m_header.m_transform == make_translation(210, 160));
    auto& text = paste(R"svg(<svg xmlns="http://www.w3.org/2000/svg"
      width="300" height="80"><text x="10" y="40" font-family="Arial"
      font-size="24" fill="#333">Hello <tspan
      font-weight="bold">World</tspan></text></svg>)svg");
    REQUIRE(text.m_header.m_name == "Hello World");
    auto& content = *text.get_text();
    REQUIRE(content.m_base_style.m_font.m_postscript_name == "Arial-BoldMT");
    REQUIRE(content.m_runs.front().m_style.m_font.m_postscript_name ==
      "ArialMT");
    auto markup = QString(R"svg(<svg
      xmlns="http://www.w3.org/2000/svg" width="200" height="200"><defs>
      <clipPath id="n"><circle cx="50" cy="50" r="40"/></clipPath></defs>
      <rect width="100" height="100" clip-path="url(#n)"/><circle cx="150"
      cy="150" r="20"/></svg>)svg");
    auto& nested = paste(markup);
    REQUIRE(nested.m_header.m_name == "Group 4");
    REQUIRE(document.get(nested.m_children.front()).m_header.m_name ==
      "Group 3");
    set_language(Language::GERMAN);
    auto& localized = paste(markup);
    set_language(Language::ENGLISH);
    REQUIRE(localized.m_header.m_name == "Gruppe 2");
    REQUIRE(document.get(localized.m_children.front()).m_header.m_name ==
      "Gruppe 1");
    REQUIRE(document.get(localized.m_children.back()).m_header.m_name ==
      "Ellipse 2");
  }

  TEST_CASE("image_on_pasteboard") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto image = QImage(50, 40, QImage::Format_RGBA8888);
    image.fill(QColor(255, 128, 0));
    auto data = QMimeData();
    data.setImageData(image);
    REQUIRE(clipboard.load(data));
    auto target = make_target({}, Rect(2000, 2000, 600, 600));
    target.m_focus = ids.m_artboard;
    REQUIRE(editor.execute(clipboard.make_paste_command(target)));
    auto& picture = document.get(editor.get_selection().get_primary());
    REQUIRE(picture.m_parent.is_nil());
    REQUIRE(document.is_on_canvas(picture.get_id()));
    REQUIRE(*get_world_bounds(document, picture.get_id()) ==
      Rect(2275, 2280, 50, 40));
  }

  TEST_CASE("image_appearance") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto editor = Editor(document);
    auto clipboard = Clipboard();
    auto image = QImage(8, 8, QImage::Format_RGBA8888);
    image.fill(QColor(0, 128, 0));
    auto data = QMimeData();
    data.setImageData(image);
    REQUIRE(clipboard.load(data));
    REQUIRE(editor.execute(
      clipboard.make_paste_appearance_command({ids.m_card})));
    auto& background = *document.get(ids.m_background).get_shape();
    auto& fill = std::get<ImageFill>(background.m_style.m_fill);
    REQUIRE(fill.m_scale_behavior == ScaleBehavior::COVER);
    REQUIRE(document.get_resources().m_images.contains(fill.m_image));
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
      clipboard.make_paste_command(make_target({}, view))));
    auto& picture = document.get(editor.get_selection().get_primary());
    REQUIRE(picture.m_header.m_name == "Image 1");
    REQUIRE(picture.m_parent == ids.m_artboard);
    auto& fill = std::get<ImageFill>(picture.get_shape()->m_style.m_fill);
    REQUIRE(document.get_resources().m_images.contains(fill.m_image));
    REQUIRE(*get_world_bounds(document, picture.get_id()) ==
      Rect(275, 280, 50, 40));
    REQUIRE(editor.execute(
      clipboard.make_paste_command(make_target({ids.m_artboard}, view))));
    auto& framed = document.get(editor.get_selection().get_primary());
    REQUIRE(framed.m_header.m_name == "Image 2");
    REQUIRE(*get_world_bounds(document, framed.get_id()) ==
      Rect(263, 305, 50, 40));
    auto text = QMimeData();
    text.setText("Pasted words");
    REQUIRE(clipboard.load(text));
    REQUIRE(editor.execute(
      clipboard.make_paste_command(make_target({}, view))));
    auto& label = document.get(editor.get_selection().get_primary());
    REQUIRE(label.m_header.m_name == "Pasted words");
    REQUIRE(*label.get_text()->m_content == u"Pasted words");
    REQUIRE(get_world_transform(document, label.get_id()) ==
      make_translation(150, 150));
  }

  TEST_CASE("deferred_flavors") {
    auto document = Document();
    auto id = NodeId();
    {
      auto transaction = Transaction(document);
      id = transaction.append(NodeId(), make_header("Label"),
        make_text(u"Deferred"));
      transaction.commit();
    }
    auto render = [] (const Document& document,
        const std::vector<NodeId>& nodes, const Rect& bounds) {
      auto image = QImage(static_cast<int>(std::ceil(bounds.m_width)),
        static_cast<int>(std::ceil(bounds.m_height)),
        QImage::Format_ARGB32_Premultiplied);
      image.fill(qRgba(0, 0, static_cast<int>(nodes.size()), 255));
      return image;
    };
    auto svg = Clipboard::make_svg_code(document, {id});
    auto bounds = *Clipboard::get_bitmap_bounds(document, {id});
    auto bitmap = Clipboard::make_bitmap(render(document, {id}, bounds));
    auto measurements = 0;
    auto measurer = [&] (const Text& text) {
      ++measurements;
      return estimate_text_bounds(text);
    };
    auto renderings = 0;
    auto renderer = [&] (const Document& document,
        const std::vector<NodeId>& nodes, const Rect& bounds) {
      ++renderings;
      return render(document, nodes, bounds);
    };
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {id}, measurer));
    auto copied = measurements;
    auto data = clipboard.make_mime_data(renderer);
    REQUIRE(measurements == copied);
    REQUIRE(renderings == 0);
    {
      auto edit = Transaction(document);
      edit.remove(id);
      edit.commit();
    }
    auto expected = QByteArray::fromStdString(svg);
    expected.append('\0');
    auto svg_format = QString();
    auto bitmap_format = QString();
    for(auto& format : data->formats()) {
      if(format.contains("Scalable Vector Graphics")) {
        svg_format = format;
      } else if(format.contains("x-rellu-dib")) {
        bitmap_format = format;
      }
    }
    REQUIRE(!svg_format.isEmpty());
    REQUIRE(data->data(svg_format) == expected);
    REQUIRE(measurements > copied);
    REQUIRE(renderings == 0);
    if(bitmap_format.isEmpty()) {
      REQUIRE(Clipboard::make_bitmap(
        qvariant_cast<QImage>(data->imageData())) == bitmap);
    } else {
      REQUIRE(data->data(bitmap_format) == bitmap);
    }
    REQUIRE(renderings == 1);
  }

  TEST_CASE("detached_svg") {
    auto document = Document();
    auto child = NodeId();
    {
      auto transaction = Transaction(document);
      auto header = make_header("Outer");
      header.m_transform = make_rotation(std::numbers::pi / 6);
      auto group = transaction.append(NodeId(), std::move(header), Group());
      child = transaction.append(
        group, make_header("Child"), make_rectangle_shape(180, 120));
      transaction.commit();
    }
    auto expected = std::string(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"180\" "
      "height=\"120\" viewBox=\"0 0 180 120\">\r\n"
      "  <rect id=\"Child\" width=\"180\" height=\"120\" "
      "fill=\"red\"/>\r\n"
      "</svg>\r\n");
    REQUIRE(Clipboard::make_svg_code(document, {child}) == expected);
    auto clipboard = Clipboard();
    REQUIRE(clipboard.copy(document, {child}, estimate_text_bounds));
    auto data = clipboard.make_mime_data();
    auto is_found = false;
    for(auto& format : data->formats()) {
      if(format.contains("Scalable Vector Graphics")) {
        auto svg = QByteArray::fromStdString(expected);
        svg.append('\0');
        REQUIRE(data->data(format) == svg);
        is_found = true;
      }
    }
    REQUIRE(is_found);
  }

  TEST_CASE("svg_code_data") {
    auto data = Clipboard::make_svg_mime_data("<svg/>\r\n");
    REQUIRE(data->text() == "<svg/>\r\n");
    auto is_found = false;
    for(auto& format : data->formats()) {
      if(format.contains("Scalable Vector Graphics")) {
        REQUIRE(data->data(format) == QByteArray("<svg/>\r\n", 9));
        is_found = true;
      }
    }
    REQUIRE(is_found);
    REQUIRE(Clipboard::make_svg_code(Document(), {}).empty());
  }

  TEST_CASE("bitmap") {
    auto image = QImage(2, 2, QImage::Format_ARGB32_Premultiplied);
    image.setPixel(0, 0, qRgba(128, 0, 0, 128));
    image.setPixel(1, 0, qRgba(0, 0, 0, 0));
    image.setPixel(0, 1, qRgba(0, 255, 0, 255));
    image.setPixel(1, 1, qRgba(0, 0, 255, 255));
    auto bitmap = Clipboard::make_bitmap(image);
    auto expected = QByteArray::fromHex(
      "2800000002000000020000000100200003000000100000000000000000000000"
      "0000000000000000"
      "0000ff0000ff0000ff000000"
      "00ff00ffff0000ff"
      "0000808000000000");
    REQUIRE(bitmap == expected);
  }
}

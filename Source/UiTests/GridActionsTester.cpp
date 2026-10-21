#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Ui/GridActions.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

namespace {
  class AddArtboardCommand : public Command {
    public:
      std::string get_label() const override {
        return "Add";
      }

      void apply(const Document&, Transaction& transaction) override {
        auto artboard = Artboard();
        artboard.m_frame = Rect(1200, 50, 1920, 1080);
        m_id = transaction.append(NodeId(), make_header("Added"), artboard);
      }

      std::optional<std::vector<NodeId>> get_selection() const override {
        return std::vector{m_id};
      }

    private:
      NodeId m_id;
  };

  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    std::unique_ptr<GridActions> m_grid_actions;
    GridSettings m_published;

    Fixture()
        : m_actions(m_window) {
      add_xd_menus(m_actions);
      m_grid_actions = std::make_unique<GridActions>(m_actions);
      m_grid_actions->set_settings_handler([this] (const auto& settings) {
        m_published = settings;
      });
    }

    bool is_enabled(const std::string& name) const {
      return m_actions.get(name).isEnabled();
    }

    QString get_text(const std::string& name) const {
      return m_actions.get(name).text();
    }
  };
}

TEST_SUITE("GridActions") {
  TEST_CASE("toggles") {
    auto fixture = Fixture();
    auto document = Document();
    build_test_document(document);
    auto editor = Editor(document);
    auto& history = editor.get_history();
    REQUIRE(!fixture.is_enabled("view.square_grid"));
    fixture.m_grid_actions->set_editor(&editor);
    REQUIRE(fixture.is_enabled("view.square_grid"));
    REQUIRE(fixture.is_enabled("view.layout_grid"));
    REQUIRE(fixture.get_text("view.square_grid") == "Show Square Grid");
    REQUIRE(fixture.get_text("view.layout_grid") == "Show Layout Grid");
    fixture.m_actions.get("view.square_grid").trigger();
    REQUIRE(fixture.m_published.m_is_square_visible);
    REQUIRE(fixture.get_text("view.square_grid") == "Hide Square Grid");
    REQUIRE(history.get_undo_label() == "Show Square Grid");
    REQUIRE(history.is_dirty());
    fixture.m_actions.get("view.layout_grid").trigger();
    REQUIRE(fixture.m_published.m_is_layout_visible);
    REQUIRE(!fixture.m_published.m_is_square_visible);
    REQUIRE(fixture.get_text("view.layout_grid") == "Hide Layout Grid");
    REQUIRE(fixture.get_text("view.square_grid") == "Show Square Grid");
    REQUIRE(history.get_undo_label() == "Show Layout Grid");
    fixture.m_actions.get("view.layout_grid").trigger();
    REQUIRE(history.get_undo_label() == "Hide Layout Grid");
    REQUIRE(!is_grid_visible(fixture.m_published));
    REQUIRE(fixture.m_grid_actions->get_settings().m_last_type ==
      GridType::LAYOUT);
    REQUIRE(editor.undo());
    REQUIRE(fixture.m_published.m_is_layout_visible);
    REQUIRE(editor.undo());
    REQUIRE(fixture.m_published.m_is_square_visible);
    REQUIRE(!fixture.m_published.m_is_layout_visible);
    REQUIRE(editor.undo());
    REQUIRE(!is_grid_visible(fixture.m_published));
    REQUIRE(!history.is_dirty());
    REQUIRE(editor.redo());
    REQUIRE(fixture.m_published.m_is_square_visible);
    fixture.m_grid_actions->set_workspace(Workspace::PROTOTYPE);
    REQUIRE(!is_grid_visible(fixture.m_published));
    REQUIRE(fixture.m_grid_actions->get_settings().m_is_square_visible);
    REQUIRE(!fixture.is_enabled("view.square_grid"));
    fixture.m_grid_actions.reset();
    REQUIRE(editor.undo());
  }

  TEST_CASE("choose") {
    auto fixture = Fixture();
    auto document = Document();
    build_test_document(document);
    auto editor = Editor(document);
    auto& history = editor.get_history();
    fixture.m_grid_actions->set_editor(&editor);
    fixture.m_grid_actions->choose(GridType::LAYOUT);
    REQUIRE(fixture.m_published.m_is_layout_visible);
    REQUIRE(history.get_undo_label() == "Grid Type");
    fixture.m_grid_actions->choose(GridType::LAYOUT);
    REQUIRE(history.get_undo_count() == 1);
    fixture.m_grid_actions->choose(GridType::SQUARE);
    REQUIRE(fixture.m_published.m_is_square_visible);
    REQUIRE(!fixture.m_published.m_is_layout_visible);
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(editor.undo());
    REQUIRE(fixture.m_published.m_is_layout_visible);
    REQUIRE(fixture.m_grid_actions->get_settings().m_last_type ==
      GridType::LAYOUT);
  }

  TEST_CASE("initial_grids") {
    auto fixture = Fixture();
    auto document = Document();
    auto ids = build_test_document(document);
    auto gridded = NodeId();
    {
      auto artboard = Artboard();
      artboard.m_frame = Rect(600, 50, 400, 300);
      artboard.m_layout_grid = LayoutGrid();
      artboard.m_layout_grid->m_column_count = 5;
      auto transaction = Transaction(document);
      gridded =
        transaction.append(NodeId(), make_header("Gridded"), artboard);
      transaction.commit();
    }
    auto editor = Editor(document);
    auto& history = editor.get_history();
    auto get_grid = [&] (const NodeId& id) {
      return document.get(id).get_artboard()->m_layout_grid;
    };
    fixture.m_grid_actions->set_editor(&editor);
    REQUIRE(!get_grid(ids.m_artboard));
    fixture.m_actions.get("view.layout_grid").trigger();
    REQUIRE(get_grid(ids.m_artboard) ==
      make_initial_grid(*document.get(ids.m_artboard).get_artboard()));
    REQUIRE(get_grid(gridded)->m_column_count == 5);
    REQUIRE(history.get_undo_count() == 1);
    REQUIRE(editor.execute(std::make_unique<AddArtboardCommand>()));
    auto added = editor.get_selection().get_primary();
    REQUIRE(get_grid(added)->m_column_count == 12);
    REQUIRE(history.get_undo_count() == 2);
    REQUIRE(editor.undo());
    REQUIRE(!document.find(added));
    REQUIRE(editor.undo());
    REQUIRE(!is_grid_visible(fixture.m_published));
    REQUIRE(get_grid(ids.m_artboard));
  }

  TEST_CASE("store") {
    auto settings = GridSettings();
    settings.m_is_layout_visible = true;
    auto store = record_grid_settings(QByteArray(), "first", settings, 1000);
    auto entry = QJsonDocument::fromJson(store).object().value("first").
      toObject();
    REQUIRE(entry.value("data").toString() ==
      "{\"layoutGridVisible\":true,\"squareGridVisible\":false}");
    REQUIRE(entry.value("time").toString() == "1000");
    auto fallback = GridSettings();
    fallback.m_last_type = GridType::SQUARE;
    auto found = find_grid_settings(store, "first", fallback);
    REQUIRE(found);
    REQUIRE(found->m_is_layout_visible);
    REQUIRE(!found->m_is_square_visible);
    REQUIRE(found->m_last_type == GridType::SQUARE);
    REQUIRE(!find_grid_settings(store, "unknown", GridSettings()));
    REQUIRE(record_grid_settings(store, "unknown", GridSettings(), 2000) ==
      store);
    auto copied = record_grid_settings(store, "copy", *found, 2000);
    REQUIRE(find_grid_settings(copied, "first", GridSettings()) ==
      find_grid_settings(copied, "copy", GridSettings()));
    copied = record_grid_settings(copied, "copy", GridSettings(), 3000);
    REQUIRE(!find_grid_settings(copied, "copy", GridSettings()));
    REQUIRE(find_grid_settings(copied, "first", GridSettings()));
    REQUIRE(QJsonDocument::fromJson(copied).object().keys() ==
      QStringList({"first"}));
  }
}

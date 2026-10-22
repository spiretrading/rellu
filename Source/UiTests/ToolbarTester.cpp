#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QCoreApplication>
#include <QToolButton>
#include "Rellu/Ui/ToolTips.hpp"
#include "Rellu/Ui/Toolbar.hpp"
#include "Rellu/Ui/XdMenus.hpp"

using namespace Rellu;

namespace {
  std::vector<std::string> get_names(const std::vector<ToolEntry>& entries) {
    auto names = std::vector<std::string>();
    for(auto& entry : entries) {
      names.push_back(entry.m_name);
    }
    return names;
  }

  std::vector<QString> get_shortcuts(const std::vector<ToolEntry>& entries) {
    auto shortcuts = std::vector<QString>();
    for(auto& entry : entries) {
      shortcuts.push_back(to_xd_shortcut_text(entry.m_shortcut));
    }
    return shortcuts;
  }
}

TEST_SUITE("Toolbar") {
  TEST_CASE("xd_tools") {
    auto tools = make_xd_tools();
    REQUIRE(get_names(tools) == std::vector<std::string>({"select",
      "rectangle", "ellipse", "polygon", "line", "pen", "text", "artboard",
      "zoom"}));
    REQUIRE(get_shortcuts(tools) == std::vector<QString>(
      {"V", "R", "E", "Y", "L", "P", "T", "A", "Z"}));
    REQUIRE(tools[0].m_label == "Select");
    REQUIRE(tools[7].m_label == "Artboard");
    for(auto& tool : tools) {
      REQUIRE(!tool.m_icon.isNull());
    }
    auto panels = make_xd_panels();
    REQUIRE(get_names(panels) ==
      std::vector<std::string>({"assets", "layers", "plugins"}));
    REQUIRE(get_shortcuts(panels) == std::vector<QString>(
      {"Shift+Ctrl+Y", "Ctrl+Y", "Shift+Ctrl+P"}));
  }

  TEST_CASE("tool_selection") {
    auto toolbar = Toolbar(make_xd_tools(), make_xd_panels());
    REQUIRE(toolbar.width() == Toolbar::WIDTH);
    auto selections = std::vector<std::string>();
    toolbar.set_tool_handler([&] (const auto& name) {
      selections.push_back(name);
    });
    REQUIRE(toolbar.get_tool() == "select");
    REQUIRE(toolbar.find_button("select")->isChecked());
    toolbar.find_button("rectangle")->click();
    REQUIRE(selections == std::vector<std::string>({"rectangle"}));
    REQUIRE(toolbar.get_tool() == "rectangle");
    REQUIRE(toolbar.find_button("rectangle")->isChecked());
    REQUIRE(!toolbar.find_button("select")->isChecked());
    toolbar.set_tool("text");
    REQUIRE(toolbar.get_tool() == "text");
    REQUIRE(toolbar.find_button("text")->isChecked());
    REQUIRE(!toolbar.find_button("rectangle")->isChecked());
    REQUIRE(selections.size() == 1);
    toolbar.find_button("text")->click();
    REQUIRE(toolbar.find_button("text")->isChecked());
    toolbar.set_tool("unknown");
    REQUIRE(toolbar.get_tool() == "text");
    REQUIRE(!toolbar.find_button("unknown"));
    REQUIRE(toolbar.find_button("select")->toolTip() == "Select (V)");
  }

  TEST_CASE("panel_selection") {
    auto toolbar = Toolbar(make_xd_tools(), make_xd_panels());
    auto selections = std::vector<std::string>();
    toolbar.set_panel_handler([&] (const auto& name) {
      selections.push_back(name);
    });
    REQUIRE(toolbar.get_panel().empty());
    toolbar.find_button("layers")->click();
    REQUIRE(toolbar.get_panel() == "layers");
    REQUIRE(toolbar.find_button("layers")->isChecked());
    toolbar.find_button("assets")->click();
    REQUIRE(toolbar.get_panel() == "assets");
    REQUIRE(!toolbar.find_button("layers")->isChecked());
    toolbar.find_button("assets")->click();
    REQUIRE(toolbar.get_panel().empty());
    REQUIRE(!toolbar.find_button("assets")->isChecked());
    REQUIRE(selections ==
      std::vector<std::string>({"layers", "assets", ""}));
    toolbar.set_panel("plugins");
    REQUIRE(toolbar.find_button("plugins")->isChecked());
    REQUIRE(selections.size() == 3);
  }

  TEST_CASE("tool_visibility") {
    auto toolbar = Toolbar(make_xd_tools(), make_xd_panels());
    toolbar.set_tool_visible("pen", false);
    REQUIRE(toolbar.find_button("pen")->isHidden());
    toolbar.set_tool_visible("pen", true);
    REQUIRE(!toolbar.find_button("pen")->isHidden());
  }
  TEST_CASE("layout") {
    auto toolbar = Toolbar(make_xd_tools(), make_xd_panels());
    toolbar.resize(Toolbar::WIDTH, 800);
    toolbar.show();
    QCoreApplication::processEvents();
    auto select = toolbar.find_button("select");
    REQUIRE(select->geometry() == QRect(4, 4, 40, 40));
    REQUIRE(select->iconSize() == QSize(22, 22));
    REQUIRE(toolbar.find_button("rectangle")->geometry() ==
      QRect(4, 44, 40, 40));
    REQUIRE(toolbar.find_button("ellipse")->geometry().y() == 84);
    auto& panels = toolbar.get_panels();
    REQUIRE(toolbar.find_button(panels.back().m_name)->geometry().bottom() ==
      800 - 4 - 1);
    REQUIRE(get_tool_tip_placement(*toolbar.find_button("rectangle")) ==
      ToolTipPlacement::RIGHT);
    toolbar.hide();
  }
}

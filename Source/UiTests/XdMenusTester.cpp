#include <vector>
#include <doctest/doctest.h>
#include <QMenu>
#include <QMenuBar>
#include <QWidget>
#include "Rellu/Ui/XdMenus.hpp"

using namespace Rellu;

namespace {
  QString strip_mnemonic(QString text) {
    return text.replace("&&", QString(QChar(1))).remove('&').replace(
      QString(QChar(1)), "&");
  }

  QMenu* find_menu(QWidget& container, const QString& title) {
    for(auto action : container.actions()) {
      if(action->menu() && strip_mnemonic(action->text()) == title) {
        return action->menu();
      }
    }
    return nullptr;
  }

  std::vector<QString> describe(QMenu& menu) {
    auto items = std::vector<QString>();
    for(auto action : menu.actions()) {
      if(action->isSeparator()) {
        items.push_back("-");
      } else if(action->menu()) {
        items.push_back(strip_mnemonic(action->text()) + " >");
      } else {
        auto text = strip_mnemonic(action->text());
        auto shortcut = to_xd_shortcut_text(action->shortcut());
        if(!shortcut.isEmpty()) {
          text += "  " + shortcut;
        }
        if(action->isCheckable()) {
          text += action->isChecked() ? " [x]" : " [ ]";
        }
        items.push_back(text);
      }
    }
    return items;
  }

  struct Fixture {
    QWidget m_window;
    ActionRegistry m_actions;
    QMenuBar m_menu_bar;

    Fixture()
        : m_actions(m_window) {
      add_xd_menus(m_actions);
      m_actions.build(m_menu_bar);
    }

    std::vector<QString> get(std::vector<QString> path) {
      auto menu = find_menu(m_menu_bar, path.front());
      REQUIRE(menu);
      for(auto i = std::size_t(1); i != path.size(); ++i) {
        menu = find_menu(*menu, path[i]);
        REQUIRE(menu);
      }
      return describe(*menu);
    }
  };
}

TEST_SUITE("XdMenus") {
  TEST_CASE("menu_bar") {
    auto fixture = Fixture();
    auto titles = std::vector<QString>();
    for(auto action : fixture.m_menu_bar.actions()) {
      titles.push_back(strip_mnemonic(action->text()));
    }
    REQUIRE(titles == std::vector<QString>({"File", "Edit", "Object",
      "Plugins", "View", "Window", "Help"}));
  }

  TEST_CASE("file") {
    auto fixture = Fixture();
    REQUIRE(fixture.get({"File"}) == std::vector<QString>({"New  Ctrl+N",
      "Open...  Ctrl+O", "Open from Your Computer...  Shift+Ctrl+O",
      "Open Recent >", "Get UI Kits...", "Manage Libraries...", "-",
      "Close  Alt+F4", "Rename...", "Save  Ctrl+S", "Save As...  Shift+Ctrl+S",
      "Save As Local Document...  Shift+Ctrl+Alt+S", "Revert to Saved",
      "Show Document History", "-", "Export >", "Import...  Shift+Ctrl+I"}));
    REQUIRE(fixture.get({"File", "Open Recent"}) ==
      std::vector<QString>({"Clear Menu"}));
    REQUIRE(fixture.get({"File", "Export"}) == std::vector<QString>({
      "Batch...  Shift+Ctrl+E", "Selected...  Ctrl+E", "All Artboards...",
      "-", "After Effects...  Ctrl+Alt+F"}));
  }

  TEST_CASE("edit") {
    auto fixture = Fixture();
    REQUIRE(fixture.get({"Edit"}) == std::vector<QString>({"Undo  Ctrl+Z",
      "Redo  Shift+Ctrl+Z", "-", "Cut  Ctrl+X", "Copy  Ctrl+C",
      "Copy SVG Code", "Paste  Ctrl+V", "Paste Appearance  Ctrl+Alt+V",
      "Duplicate  Ctrl+D", "Rename  Ctrl+Alt+R", "-", "Delete  Del",
      "Select All  Ctrl+A", "Deselect All  Shift+Ctrl+A", "-",
      "Color Profile >", "-", "Turn Spellcheck On", "Preferences >"}));
    REQUIRE(fixture.get({"Edit", "Color Profile"}) ==
      std::vector<QString>({"sRGB [ ]", "Unmanaged [x]"}));
    REQUIRE(fixture.get({"Edit", "Preferences"}) == std::vector<QString>({
      "Language >", "-", "Show Productivity Tips [x]", "-",
      "Auto-create Link when Sharing [x]"}));
  }

  TEST_CASE("object") {
    auto fixture = Fixture();
    REQUIRE(fixture.get({"Object"}) == std::vector<QString>({"Group  Ctrl+G",
      "Ungroup  Shift+Ctrl+G", "Lock  Ctrl+L", "Hide  Ctrl+,", "-",
      "Add Color to Assets  Shift+Ctrl+C",
      "Add Character Style to Assets  Shift+Ctrl+T", "Make Component  Ctrl+K",
      "Edit Main Component  Shift+Ctrl+K",
      "Back to Previous Instance  Shift+Alt+K", "Reset to Main State",
      "Mark for Export  Shift+E", "-", "Reveal Color in Assets",
      "Reveal Character Style in Assets", "Reveal Component in Assets", "-",
      "Mask with Shape  Shift+Ctrl+M", "Make Repeat Grid  Ctrl+R", "-",
      "Make Scroll Group >", "-", "Path >", "-", "Text >", "-", "Arrange >",
      "-", "Transform >", "-", "Align to Pixel Grid", "Align >",
      "Distribute >"}));
    REQUIRE(fixture.get({"Object", "Make Scroll Group"}) ==
      std::vector<QString>({"Horizontal  Shift+Alt+H",
      "Vertical  Shift+Alt+V", "Horizontal & Vertical  Shift+Alt+D"}));
    REQUIRE(fixture.get({"Object", "Path"}) == std::vector<QString>({
      "Add  Ctrl+Alt+U", "Subtract  Ctrl+Alt+S", "Intersect  Ctrl+Alt+I",
      "Exclude Overlap  Ctrl+Alt+X", "Convert to Path  Ctrl+8",
      "Outline Stroke  Ctrl+Alt+O"}));
    REQUIRE(fixture.get({"Object", "Text"}) == std::vector<QString>({
      "Bold  Ctrl+B", "Italic  Ctrl+I", "Underline  Ctrl+U [ ]",
      "Strikethrough [ ]", "-", "Uppercase [ ]", "Lowercase [ ]",
      "Title Case [ ]", "-", "Superscript [ ]", "Subscript [ ]", "-",
      "Increase Font Size  Shift+Ctrl+>", "Decrease Font Size  Shift+Ctrl+<"}));
    REQUIRE(fixture.get({"Object", "Arrange"}) == std::vector<QString>({
      "Bring to Front  Shift+Ctrl+]", "Bring Forward  Ctrl+]",
      "Send Backward  Ctrl+[", "Send to Back  Shift+Ctrl+["}));
    REQUIRE(fixture.get({"Object", "Transform"}) == std::vector<QString>({
      "Flip Horizontally", "Flip Vertically",
      "Reset 3D Transforms  Ctrl+Alt+T"}));
    REQUIRE(fixture.get({"Object", "Align"}) == std::vector<QString>({
      "Left  Shift+Ctrl+Left", "Center (Horizontally)  Shift+C",
      "Right  Shift+Ctrl+Right", "-", "Top  Shift+Ctrl+Up",
      "Middle (Vertically)  Shift+M", "Bottom  Shift+Ctrl+Down"}));
    REQUIRE(fixture.get({"Object", "Distribute"}) == std::vector<QString>({
      "Horizontally  Shift+Ctrl+H", "Vertically  Shift+Ctrl+V"}));
  }

  TEST_CASE("plugins") {
    auto fixture = Fixture();
    REQUIRE(fixture.get({"Plugins"}) == std::vector<QString>({
      "Browse Plugins...", "Manage Plugins...", "-", "Development >"}));
    REQUIRE(fixture.get({"Plugins", "Development"}) == std::vector<QString>({
      "Create a Plugin...", "Get Developer Tools...", "-",
      "Reload Plugins (Legacy)  Shift+Ctrl+R", "Developer Console (Legacy)",
      "Show Develop Folder (Legacy)"}));
  }

  TEST_CASE("view") {
    auto fixture = Fixture();
    REQUIRE(fixture.get({"View"}) == std::vector<QString>({"Zoom In  Ctrl++",
      "Zoom Out  Ctrl+-", "-", "100%  Ctrl+1", "200%  Ctrl+2",
      "Zoom to Selection  Ctrl+3", "Zoom to Fit All  Ctrl+0", "-",
      "Libraries  Shift+Ctrl+Y [ ]", "Layers  Ctrl+Y [ ]",
      "Plugins  Shift+Ctrl+P [ ]", "-", "Guides >", "-",
      "Show Layout Grid  Shift+Ctrl+'", "Show Square Grid  Ctrl+'", "-",
      "Show 3D Transforms  Ctrl+T", "-", "Hide Live Cursors"}));
    REQUIRE(fixture.get({"View", "Guides"}) == std::vector<QString>({
      "Copy Guides", "Paste Guides  Ctrl+V", "Clear Guides",
      "Hide All Guides  Ctrl+;", "Lock All Guides  Shift+Ctrl+;"}));
  }

  TEST_CASE("window") {
    auto fixture = Fixture();
    REQUIRE(fixture.get({"Window"}) ==
      std::vector<QString>({"Preview  Ctrl+Enter [ ]"}));
  }

  TEST_CASE("help") {
    auto fixture = Fixture();
    REQUIRE(fixture.get({"Help"}) == std::vector<QString>({"Learn Rellu...",
      "Productivity Tips...", "User Guide...", "-", "What's New...",
      "Release Notes...", "Provide Feedback...", "-", "About Rellu",
      "Manage My Account...", "Sign Out...", "Updates..."}));
  }

  TEST_CASE("disabled_placeholders") {
    auto fixture = Fixture();
    auto& actions = fixture.m_actions;
    for(auto& name : actions.get_names()) {
      REQUIRE(!actions.get(name).isEnabled());
    }
    actions.set_handler("file.new", [] {});
    REQUIRE(actions.get("file.new").isEnabled());
  }

  TEST_CASE("shortcut_text") {
    REQUIRE(to_xd_shortcut_text(QKeySequence("Ctrl+Shift+Z")) ==
      "Shift+Ctrl+Z");
    REQUIRE(to_xd_shortcut_text(QKeySequence("Ctrl+Alt+Shift+S")) ==
      "Shift+Ctrl+Alt+S");
    REQUIRE(to_xd_shortcut_text(QKeySequence("Alt+Shift+K")) ==
      "Shift+Alt+K");
    REQUIRE(to_xd_shortcut_text(QKeySequence(Qt::Key_Delete)) == "Del");
    REQUIRE(to_xd_shortcut_text(QKeySequence("Ctrl+Return")) ==
      "Ctrl+Enter");
    REQUIRE(to_xd_shortcut_text(QKeySequence("Ctrl++")) == "Ctrl++");
    REQUIRE(to_xd_shortcut_text(QKeySequence()).isEmpty());
  }
}

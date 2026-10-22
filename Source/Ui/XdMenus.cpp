#include <array>
#include <string_view>
#include <utility>
#include <QKeyCombination>
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"

using namespace Rellu;

namespace {
  enum class ItemType : unsigned char {
    MENU,
    ACTION,
    TOGGLE,
    CHECKED
  };

  struct Item {
    ItemType m_type;
    const char* m_name;
    const char* m_text;
    const char* m_shortcut;
    const char* m_parent;
    const char* m_section;
  };

  constexpr auto MENU = ItemType::MENU;
  constexpr auto ACTION = ItemType::ACTION;
  constexpr auto TOGGLE = ItemType::TOGGLE;
  constexpr auto CHECKED = ItemType::CHECKED;

  constexpr auto ITEMS = std::to_array<Item>({
    Item(MENU, "file", "&File", "", "", "file"),
    Item(ACTION, "file.new", "New", "Ctrl+N", "file", "open"),
    Item(ACTION, "file.open", "Open...", "Ctrl+O", "file", "open"),
    Item(ACTION, "file.open_local", "Open from Your Computer...",
      "Shift+Ctrl+O", "file", "open"),
    Item(MENU, "file.recent", "Open Recent", "", "file", "open"),
    Item(ACTION, "file.get_ui_kits", "Get UI Kits...", "", "file", "open"),
    Item(ACTION, "file.manage_libraries", "Manage Libraries...", "", "file",
      "open"),
    Item(ACTION, "file.close", "Close", "Alt+F4", "file", "document"),
    Item(ACTION, "file.rename", "Rename...", "", "file", "document"),
    Item(ACTION, "file.save", "Save", "Ctrl+S", "file", "document"),
    Item(ACTION, "file.save_as", "Save As...", "Shift+Ctrl+S", "file",
      "document"),
    Item(ACTION, "file.save_as_local", "Save As Local Document...",
      "Shift+Ctrl+Alt+S", "file", "document"),
    Item(ACTION, "file.revert", "Revert to Saved", "", "file", "document"),
    Item(ACTION, "file.history", "Show Document History", "", "file",
      "document"),
    Item(MENU, "file.export", "Export", "", "file", "exchange"),
    Item(ACTION, "file.import", "Import...", "Shift+Ctrl+I", "file",
      "exchange"),
    Item(ACTION, "file.recent.clear", "Clear Menu", "", "file.recent", "clear"),
    Item(ACTION, "file.export.batch", "Batch...", "Shift+Ctrl+E", "file.export",
      "export"),
    Item(ACTION, "file.export.selected", "Selected...", "Ctrl+E", "file.export",
      "export"),
    Item(ACTION, "file.export.all", "All Artboards...", "", "file.export",
      "export"),
    Item(ACTION, "file.export.after_effects", "After Effects...", "Ctrl+Alt+F",
      "file.export", "after_effects"),
    Item(MENU, "edit", "&Edit", "", "", "edit"),
    Item(ACTION, "edit.undo", "Undo", "Ctrl+Z", "edit", "history"),
    Item(ACTION, "edit.redo", "Redo", "Shift+Ctrl+Z", "edit", "history"),
    Item(ACTION, "edit.cut", "Cut", "Ctrl+X", "edit", "clipboard"),
    Item(ACTION, "edit.copy", "Copy", "Ctrl+C", "edit", "clipboard"),
    Item(ACTION, "edit.copy_svg", "Copy SVG Code", "", "edit", "clipboard"),
    Item(ACTION, "edit.paste", "Paste", "Ctrl+V", "edit", "clipboard"),
    Item(ACTION, "edit.paste_appearance", "Paste Appearance", "Ctrl+Alt+V",
      "edit", "clipboard"),
    Item(ACTION, "edit.duplicate", "Duplicate", "Ctrl+D", "edit", "clipboard"),
    Item(ACTION, "edit.rename", "Rename", "Ctrl+Alt+R", "edit", "clipboard"),
    Item(ACTION, "edit.delete", "Delete", "Del", "edit", "selection"),
    Item(ACTION, "edit.select_all", "Select All", "Ctrl+A", "edit",
      "selection"),
    Item(ACTION, "edit.deselect_all", "Deselect All", "Shift+Ctrl+A", "edit",
      "selection"),
    Item(MENU, "edit.color_profile", "Color Profile", "", "edit", "color"),
    Item(ACTION, "edit.spellcheck", "Turn Spellcheck On", "", "edit",
      "preferences"),
    Item(MENU, "edit.preferences", "Preferences", "", "edit", "preferences"),
    Item(TOGGLE, "edit.color_profile.srgb", "sRGB", "", "edit.color_profile",
      "profiles"),
    Item(CHECKED, "edit.color_profile.unmanaged", "Unmanaged", "",
      "edit.color_profile", "profiles"),
    Item(MENU, "edit.preferences.language", "Language", "", "edit.preferences",
      "language"),
    Item(TOGGLE, "edit.preferences.language.german", "Deutsch", "",
      "edit.preferences.language", "languages"),
    Item(CHECKED, "edit.preferences.language.english", "English", "",
      "edit.preferences.language", "languages"),
    Item(TOGGLE, "edit.preferences.language.spanish", "Espa\xC3\xB1ol", "",
      "edit.preferences.language", "languages"),
    Item(TOGGLE, "edit.preferences.language.french", "Fran\xC3\xA7" "ais", "",
      "edit.preferences.language", "languages"),
    Item(TOGGLE, "edit.preferences.language.italian", "Italiano", "",
      "edit.preferences.language", "languages"),
    Item(TOGGLE, "edit.preferences.language.japanese",
      "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "", "edit.preferences.language",
      "languages"),
    Item(TOGGLE, "edit.preferences.language.korean",
      "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", "", "edit.preferences.language",
      "languages"),
    Item(TOGGLE, "edit.preferences.language.portuguese",
      "Portugu\xC3\xAA" "s do Brasil", "", "edit.preferences.language",
      "languages"),
    Item(TOGGLE, "edit.preferences.language.russian",
      "P\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", "",
      "edit.preferences.language", "languages"),
    Item(TOGGLE, "edit.preferences.language.chinese",
      "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87", "",
      "edit.preferences.language", "languages"),
    Item(CHECKED, "edit.preferences.productivity_tips",
      "Show Productivity Tips", "", "edit.preferences", "tips"),
    Item(CHECKED, "edit.preferences.auto_link", "Auto-create Link when Sharing",
      "", "edit.preferences", "sharing"),
    Item(MENU, "object", "&Object", "", "", "object"),
    Item(ACTION, "object.group", "Group", "Ctrl+G", "object", "group"),
    Item(ACTION, "object.ungroup", "Ungroup", "Shift+Ctrl+G", "object",
      "group"),
    Item(ACTION, "object.toggle_lock", "Lock", "Ctrl+L", "object", "group"),
    Item(ACTION, "object.toggle_visibility", "Hide", "Ctrl+,", "object",
      "group"),
    Item(ACTION, "object.add_color", "Add Color to Assets", "Shift+Ctrl+C",
      "object", "assets"),
    Item(ACTION, "object.add_character_style", "Add Character Style to Assets",
      "Shift+Ctrl+T", "object", "assets"),
    Item(ACTION, "object.make_component", "Make Component", "Ctrl+K", "object",
      "assets"),
    Item(ACTION, "object.edit_main_component", "Edit Main Component",
      "Shift+Ctrl+K", "object", "assets"),
    Item(ACTION, "object.previous_instance", "Back to Previous Instance",
      "Shift+Alt+K", "object", "assets"),
    Item(ACTION, "object.reset_main_state", "Reset to Main State", "", "object",
      "assets"),
    Item(ACTION, "object.mark_for_export", "Mark for Export", "Shift+E",
      "object", "assets"),
    Item(ACTION, "object.reveal_color", "Reveal Color in Assets", "", "object",
      "reveal"),
    Item(ACTION, "object.reveal_character_style",
      "Reveal Character Style in Assets", "", "object", "reveal"),
    Item(ACTION, "object.reveal_component", "Reveal Component in Assets", "",
      "object", "reveal"),
    Item(ACTION, "object.mask", "Mask with Shape", "Shift+Ctrl+M", "object",
      "grids"),
    Item(ACTION, "object.repeat_grid", "Make Repeat Grid", "Ctrl+R", "object",
      "grids"),
    Item(MENU, "object.scroll_group", "Make Scroll Group", "", "object",
      "scroll"),
    Item(MENU, "object.path", "Path", "", "object", "path"),
    Item(MENU, "object.text", "Text", "", "object", "text"),
    Item(MENU, "object.arrange", "Arrange", "", "object", "arrange"),
    Item(MENU, "object.transform", "Transform", "", "object", "transform"),
    Item(ACTION, "object.align_to_pixel_grid", "Align to Pixel Grid", "",
      "object", "align"),
    Item(MENU, "object.align", "Align", "", "object", "align"),
    Item(MENU, "object.distribute", "Distribute", "", "object", "align"),
    Item(ACTION, "object.scroll_group.horizontal", "Horizontal", "Shift+Alt+H",
      "object.scroll_group", "scroll"),
    Item(ACTION, "object.scroll_group.vertical", "Vertical", "Shift+Alt+V",
      "object.scroll_group", "scroll"),
    Item(ACTION, "object.scroll_group.panning", "Horizontal && Vertical",
      "Shift+Alt+D", "object.scroll_group", "scroll"),
    Item(ACTION, "object.path.add", "Add", "Ctrl+Alt+U", "object.path", "path"),
    Item(ACTION, "object.path.subtract", "Subtract", "Ctrl+Alt+S",
      "object.path", "path"),
    Item(ACTION, "object.path.intersect", "Intersect", "Ctrl+Alt+I",
      "object.path", "path"),
    Item(ACTION, "object.path.exclude", "Exclude Overlap", "Ctrl+Alt+X",
      "object.path", "path"),
    Item(ACTION, "object.path.convert", "Convert to Path", "Ctrl+8",
      "object.path", "path"),
    Item(ACTION, "object.path.outline_stroke", "Outline Stroke", "Ctrl+Alt+O",
      "object.path", "path"),
    Item(ACTION, "object.text.bold", "Bold", "Ctrl+B", "object.text", "style"),
    Item(ACTION, "object.text.italic", "Italic", "Ctrl+I", "object.text",
      "style"),
    Item(TOGGLE, "object.text.underline", "Underline", "Ctrl+U", "object.text",
      "style"),
    Item(TOGGLE, "object.text.strikethrough", "Strikethrough", "",
      "object.text", "style"),
    Item(TOGGLE, "object.text.uppercase", "Uppercase", "", "object.text",
      "case"),
    Item(TOGGLE, "object.text.lowercase", "Lowercase", "", "object.text",
      "case"),
    Item(TOGGLE, "object.text.title_case", "Title Case", "", "object.text",
      "case"),
    Item(TOGGLE, "object.text.superscript", "Superscript", "", "object.text",
      "script"),
    Item(TOGGLE, "object.text.subscript", "Subscript", "", "object.text",
      "script"),
    Item(ACTION, "object.text.increase_size", "Increase Font Size",
      "Shift+Ctrl+>", "object.text", "size"),
    Item(ACTION, "object.text.decrease_size", "Decrease Font Size",
      "Shift+Ctrl+<", "object.text", "size"),
    Item(ACTION, "arrange.bring_to_front", "Bring to Front", "Shift+Ctrl+]",
      "object.arrange", "arrange"),
    Item(ACTION, "arrange.bring_forward", "Bring Forward", "Ctrl+]",
      "object.arrange", "arrange"),
    Item(ACTION, "arrange.send_backward", "Send Backward", "Ctrl+[",
      "object.arrange", "arrange"),
    Item(ACTION, "arrange.send_to_back", "Send to Back", "Shift+Ctrl+[",
      "object.arrange", "arrange"),
    Item(ACTION, "object.transform.flip_horizontally", "Flip Horizontally", "",
      "object.transform", "transform"),
    Item(ACTION, "object.transform.flip_vertically", "Flip Vertically", "",
      "object.transform", "transform"),
    Item(ACTION, "object.transform.reset_3d", "Reset 3D Transforms",
      "Ctrl+Alt+T", "object.transform", "transform"),
    Item(ACTION, "object.align.left", "Left", "Shift+Ctrl+Left", "object.align",
      "horizontal"),
    Item(ACTION, "object.align.center", "Center (Horizontally)", "Shift+C",
      "object.align", "horizontal"),
    Item(ACTION, "object.align.right", "Right", "Shift+Ctrl+Right",
      "object.align", "horizontal"),
    Item(ACTION, "object.align.top", "Top", "Shift+Ctrl+Up", "object.align",
      "vertical"),
    Item(ACTION, "object.align.middle", "Middle (Vertically)", "Shift+M",
      "object.align", "vertical"),
    Item(ACTION, "object.align.bottom", "Bottom", "Shift+Ctrl+Down",
      "object.align", "vertical"),
    Item(ACTION, "object.distribute.horizontally", "Horizontally",
      "Shift+Ctrl+H", "object.distribute", "distribute"),
    Item(ACTION, "object.distribute.vertically", "Vertically", "Shift+Ctrl+V",
      "object.distribute", "distribute"),
    Item(MENU, "plugins", "&Plugins", "", "", "plugins"),
    Item(ACTION, "plugins.browse", "Browse Plugins...", "", "plugins",
      "manage"),
    Item(ACTION, "plugins.manage", "Manage Plugins...", "", "plugins",
      "manage"),
    Item(MENU, "plugins.development", "Development", "", "plugins",
      "development"),
    Item(ACTION, "plugins.development.create", "Create a Plugin...", "",
      "plugins.development", "create"),
    Item(ACTION, "plugins.development.tools", "Get Developer Tools...", "",
      "plugins.development", "create"),
    Item(ACTION, "plugins.development.reload", "Reload Plugins (Legacy)",
      "Shift+Ctrl+R", "plugins.development", "legacy"),
    Item(ACTION, "plugins.development.console", "Developer Console (Legacy)",
      "", "plugins.development", "legacy"),
    Item(ACTION, "plugins.development.folder", "Show Develop Folder (Legacy)",
      "", "plugins.development", "legacy"),
    Item(MENU, "view", "&View", "", "", "view"),
    Item(ACTION, "view.zoom_in", "Zoom In", "Ctrl++", "view", "zoom"),
    Item(ACTION, "view.zoom_out", "Zoom Out", "Ctrl+-", "view", "zoom"),
    Item(ACTION, "view.zoom_actual", "100%", "Ctrl+1", "view", "presets"),
    Item(ACTION, "view.zoom_double", "200%", "Ctrl+2", "view", "presets"),
    Item(ACTION, "view.zoom_selection", "Zoom to Selection", "Ctrl+3", "view",
      "presets"),
    Item(ACTION, "view.zoom_fit", "Zoom to Fit All", "Ctrl+0", "view",
      "presets"),
    Item(TOGGLE, "view.libraries", "Libraries", "Shift+Ctrl+Y", "view",
      "panels"),
    Item(TOGGLE, "view.layers", "Layers", "Ctrl+Y", "view", "panels"),
    Item(TOGGLE, "view.plugins", "Plugins", "Shift+Ctrl+P", "view", "panels"),
    Item(MENU, "view.guides", "Guides", "", "view", "guides"),
    Item(ACTION, "view.layout_grid", "Show Layout Grid", "Shift+Ctrl+'", "view",
      "grids"),
    Item(ACTION, "view.square_grid", "Show Square Grid", "Ctrl+'", "view",
      "grids"),
    Item(ACTION, "view.transforms_3d", "Show 3D Transforms", "Ctrl+T", "view",
      "transforms"),
    Item(ACTION, "view.live_cursors", "Hide Live Cursors", "", "view",
      "cursors"),
    Item(ACTION, "view.guides.copy", "Copy Guides", "", "view.guides",
      "guides"),
    Item(ACTION, "view.guides.paste", "Paste Guides", "Ctrl+V", "view.guides",
      "guides"),
    Item(ACTION, "view.guides.clear", "Clear Guides", "", "view.guides",
      "guides"),
    Item(ACTION, "view.guides.hide", "Hide All Guides", "Ctrl+;", "view.guides",
      "guides"),
    Item(ACTION, "view.guides.lock", "Lock All Guides", "Shift+Ctrl+;",
      "view.guides", "guides"),
    Item(MENU, "window", "&Window", "", "", "window"),
    Item(TOGGLE, "window.preview", "Preview", "Ctrl+Return", "window",
      "preview"),
    Item(MENU, "help", "&Help", "", "", "help"),
    Item(ACTION, "help.learn", "Learn Rellu...", "", "help", "learn"),
    Item(ACTION, "help.tips", "Productivity Tips...", "", "help", "learn"),
    Item(ACTION, "help.guide", "User Guide...", "", "help", "learn"),
    Item(ACTION, "help.whats_new", "What's New...", "", "help", "news"),
    Item(ACTION, "help.release_notes", "Release Notes...", "", "help", "news"),
    Item(ACTION, "help.feedback", "Provide Feedback...", "", "help", "news"),
    Item(ACTION, "help.about", "About Rellu", "", "help", "account"),
    Item(ACTION, "help.account", "Manage My Account...", "", "help", "account"),
    Item(ACTION, "help.sign_out", "Sign Out...", "", "help", "account"),
    Item(ACTION, "help.updates", "Updates...", "", "help", "account")
  });

  constexpr auto KEY_NAMES = std::to_array<std::pair<Qt::Key, const char*>>({
    std::pair(Qt::Key_Return, "Enter"), std::pair(Qt::Key_Enter, "Enter"),
    std::pair(Qt::Key_Delete, "Del"), std::pair(Qt::Key_Tab, "Tab"),
    std::pair(Qt::Key_Up, "Up"), std::pair(Qt::Key_Down, "Down"),
    std::pair(Qt::Key_Left, "Left"), std::pair(Qt::Key_Right, "Right")});

  constexpr auto VIEW_ACTIONS = std::to_array<std::string_view>({
    "view.zoom_in", "view.zoom_out", "view.zoom_actual", "view.zoom_double",
    "view.zoom_fit", "view.libraries", "view.layers", "view.plugins",
    "window.preview"});

  bool is_document_free(std::string_view name) {
    return name == "file.new" || name == "file.open" ||
      name == "file.open_local" || name == "file.close" ||
      name.starts_with("file.recent.") ||
      name.starts_with("edit.preferences.") || name.starts_with("help.");
  }

  QString format_shortcut(const QKeySequence& shortcut, Language language,
      const QString& separator) {
    if(shortcut.isEmpty()) {
      return QString();
    }
    auto name = [&] (std::string_view key) {
      auto text = localize("shortcut", key, language);
      return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
    };
    auto combination = shortcut[0];
    auto modifiers = combination.keyboardModifiers();
    auto text = QString();
    if(modifiers & Qt::ShiftModifier) {
      text += name("Shift") + separator;
    }
    if(modifiers & Qt::ControlModifier) {
      text += name("Ctrl") + separator;
    }
    if(modifiers & Qt::AltModifier) {
      text += name("Alt") + separator;
    }
    if(modifiers & Qt::MetaModifier) {
      text += name("Win") + separator;
    }
    auto key = combination.key();
    for(auto& entry : KEY_NAMES) {
      if(entry.first == key) {
        return text + name(entry.second);
      }
    }
    return text + QKeySequence(key).toString(QKeySequence::PortableText);
  }
}

void Rellu::add_xd_menus(ActionRegistry& actions) {
  for(auto& item : ITEMS) {
    auto text = translate("menu", QString::fromUtf8(item.m_text));
    if(item.m_type == MENU) {
      actions.add(
        MenuDefinition(item.m_name, text, item.m_parent, item.m_section));
      continue;
    }
    auto& action = actions.add(ActionDefinition(item.m_name, text,
      QKeySequence(item.m_shortcut), item.m_parent, item.m_section,
      item.m_type != ACTION));
    if(item.m_type == CHECKED) {
      action.setChecked(true);
    }
  }
}

void Rellu::update_document_actions(
    ActionRegistry& actions, Workspace workspace, bool has_document) {
  for(auto& name : actions.get_names()) {
    if(name.starts_with("tool.")) {
      actions.set_enabled(name, has_document &&
        (workspace == Workspace::DESIGN || name == "tool.select" ||
          name == "tool.zoom"));
    } else if(!has_document && !is_document_free(name)) {
      actions.set_enabled(name, false);
    }
  }
  if(!has_document) {
    return;
  }
  for(auto name : VIEW_ACTIONS) {
    if(actions.find(name)) {
      actions.set_enabled(name, true);
    }
  }
}

QString Rellu::to_xd_shortcut_text(
    const QKeySequence& shortcut, Language language) {
  return format_shortcut(shortcut, language, "+");
}

QString Rellu::to_xd_shortcut_text(const QKeySequence& shortcut) {
  return to_xd_shortcut_text(shortcut, get_language());
}

QString Rellu::to_xd_tool_tip_shortcut_text(
    const QKeySequence& shortcut, Language language) {
  return format_shortcut(shortcut, language, " + ");
}

QString Rellu::to_xd_tool_tip_shortcut_text(const QKeySequence& shortcut) {
  return to_xd_tool_tip_shortcut_text(shortcut, get_language());
}

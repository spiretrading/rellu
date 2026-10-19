#include <utility>
#include <vector>
#include "Rellu/Ui/ContextMenus.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  struct Entry {
    const char* m_name;
    const char* m_text;
    bool m_is_shown_if_disabled;
    bool m_is_shown_if_nothing_enabled;
  };

  const auto SEPARATOR = Entry(nullptr, nullptr, false, false);

  bool is_component_ungroup(const QAction& action) {
    return action.text().startsWith(translate("menu", "Ungroup Component")) ||
      action.text().startsWith(translate("menu", "Unlink Component"));
  }

  std::vector<Entry> make_entries(
      const ActionRegistry& actions, ContextMenuKind kind) {
    auto is_layers = kind == ContextMenuKind::LAYERS;
    auto ungroup = actions.find("object.ungroup");
    auto is_component = ungroup && is_component_ungroup(*ungroup);
    auto ungroup_entry = Entry("object.ungroup", nullptr, false, true);
    auto entries = std::vector<Entry>();
    if(!is_layers) {
      entries.push_back(Entry("edit.cut", nullptr, false, true));
    }
    entries.insert(entries.end(), {
      Entry("edit.copy", nullptr, false, true),
      Entry("edit.copy_svg", nullptr, false, true),
      Entry("edit.paste", nullptr, true, true),
      Entry("edit.paste_appearance", nullptr, false, true)});
    if(is_layers) {
      entries.push_back(Entry("edit.duplicate", nullptr, false, true));
    }
    entries.push_back(Entry("edit.delete", nullptr, false, true));
    if(is_layers) {
      entries.push_back(Entry("edit.rename", nullptr, false, true));
    }
    entries.insert(entries.end(), {SEPARATOR,
      Entry("object.transform.reset_3d", nullptr, false, false), SEPARATOR,
      Entry("object.toggle_lock", nullptr, false, true),
      Entry("object.toggle_visibility", nullptr, false, true),
      Entry("object.group", nullptr, false, true),
      Entry("object.mask", nullptr, false, true)});
    if(!is_component) {
      entries.push_back(ungroup_entry);
    }
    entries.insert(entries.end(), {
      Entry("object.path.outline_stroke", nullptr, false, false), SEPARATOR,
      Entry("object.make_component", nullptr, false, true),
      Entry("object.edit_main_component", nullptr, false, true),
      Entry("object.previous_instance", nullptr, false, true),
      Entry("object.reset_main_state", nullptr, false, true),
      Entry("object.reveal_component", nullptr, false, true)});
    if(is_component) {
      entries.push_back(ungroup_entry);
    }
    entries.insert(entries.end(), {SEPARATOR,
      Entry("object.add_color", nullptr, false, true),
      Entry("object.add_character_style", nullptr, false, true),
      Entry("object.reveal_color", nullptr, false, true),
      Entry("object.reveal_character_style", nullptr, false, true),
      SEPARATOR, Entry("object.mark_for_export", nullptr, false, true),
      SEPARATOR});
    if(!is_layers) {
      entries.insert(entries.end(), {
        Entry("arrange.bring_to_front", nullptr, false, true),
        Entry("arrange.bring_forward", nullptr, false, true),
        Entry("arrange.send_backward", nullptr, false, true),
        Entry("arrange.send_to_back", nullptr, false, true), SEPARATOR,
        Entry("object.align_to_pixel_grid", nullptr, false, true)});
    } else {
      entries.insert(entries.end(), {
        Entry("file.export.batch", "Export Batch...", true, true),
        Entry("file.export.selected", "Export Selected...", true, true)});
    }
    return entries;
  }

  bool is_enabled(QAction* action) {
    return action && action->isEnabled() && action->isVisible();
  }

  void add_entry(QMenu& menu, QAction& action, const char* text) {
    if(!text) {
      menu.addAction(&action);
      return;
    }
    auto entry = menu.addAction(translate("menu", QString::fromUtf8(text)));
    entry->setEnabled(action.isEnabled());
    QObject::connect(entry, &QAction::triggered, &action, [&action] {
      action.trigger();
    });
  }
}

QMenu* Rellu::make_context_menu(
    const ActionRegistry& actions, ContextMenuKind kind, QWidget* parent) {
  auto menu = new QMenu(parent);
  menu->setObjectName("context_menu");
  auto entries = make_entries(actions, kind);
  auto is_any_enabled = false;
  for(auto& entry : entries) {
    if(entry.m_name && is_enabled(actions.find(entry.m_name))) {
      is_any_enabled = true;
    }
  }
  auto is_separator_pending = false;
  for(auto& entry : entries) {
    if(!entry.m_name) {
      is_separator_pending = !menu->isEmpty();
      continue;
    }
    auto action = actions.find(entry.m_name);
    if(!action || !action->isVisible()) {
      continue;
    }
    auto is_shown = is_any_enabled ?
      action->isEnabled() || entry.m_is_shown_if_disabled :
      entry.m_is_shown_if_nothing_enabled;
    if(!is_shown) {
      continue;
    }
    if(is_separator_pending) {
      menu->addSeparator();
      is_separator_pending = false;
    }
    add_entry(*menu, *action, entry.m_text);
  }
  return menu;
}

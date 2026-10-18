#include <algorithm>
#include <stdexcept>
#include <utility>
#include <variant>
#include "Rellu/Ui/ActionRegistry.hpp"

using namespace Rellu;

ActionRegistry::ActionRegistry(QWidget& owner)
  : m_owner(&owner),
    m_sequence(0) {}

ActionRegistry::~ActionRegistry() {
  for(auto& menu : m_built_menus) {
    delete menu.data();
  }
}

void ActionRegistry::add(MenuDefinition definition) {
  auto i = std::find_if(m_menus.begin(), m_menus.end(),
    [&] (const auto& menu) {
      return menu.m_definition.m_name == definition.m_name;
    });
  if(i != m_menus.end()) {
    throw std::invalid_argument("Menu name in use.");
  }
  m_menus.push_back(MenuEntry(std::move(definition), m_sequence++));
  rebuild();
}

QAction& ActionRegistry::add(ActionDefinition definition) {
  if(find_entry(definition.m_name)) {
    throw std::invalid_argument("Action name in use.");
  }
  auto action = std::make_unique<QAction>(definition.m_text);
  action->setShortcut(definition.m_shortcut);
  action->setCheckable(definition.m_is_checkable);
  auto& entry = *m_entries.emplace_back(std::make_unique<Entry>(
    std::move(definition), std::move(action), std::function<void ()>(), true,
    m_sequence++));
  QObject::connect(
    entry.m_action.get(), &QAction::triggered, entry.m_action.get(),
    [entry = &entry] {
      if(auto handler = entry->m_handler) {
        handler();
      }
    });
  m_owner->addAction(entry.m_action.get());
  update(entry);
  rebuild();
  return *entry.m_action;
}

void ActionRegistry::remove(std::string_view name) {
  auto i = std::find_if(m_entries.begin(), m_entries.end(),
    [&] (const auto& entry) {
      return entry->m_definition.m_name == name;
    });
  if(i == m_entries.end()) {
    return;
  }
  auto action = (*i)->m_action.release();
  QObject::disconnect(action, &QAction::triggered, nullptr, nullptr);
  m_owner->removeAction(action);
  action->deleteLater();
  m_entries.erase(i);
  rebuild();
}

QAction* ActionRegistry::find(std::string_view name) const {
  if(auto entry = find_entry(name)) {
    return entry->m_action.get();
  }
  return nullptr;
}

QAction& ActionRegistry::get(std::string_view name) const {
  return *get_entry(name).m_action;
}

std::vector<std::string> ActionRegistry::get_names() const {
  auto names = std::vector<std::string>();
  for(auto& entry : m_entries) {
    names.push_back(entry->m_definition.m_name);
  }
  return names;
}

const QKeySequence& ActionRegistry::get_default_shortcut(
    std::string_view name) const {
  return get_entry(name).m_definition.m_shortcut;
}

void ActionRegistry::set_handler(
    std::string_view name, std::function<void ()> handler) {
  auto& entry = get_entry(name);
  entry.m_handler = std::move(handler);
  update(entry);
}

void ActionRegistry::set_enabled(std::string_view name, bool is_enabled) {
  auto& entry = get_entry(name);
  entry.m_is_enabled = is_enabled;
  update(entry);
}

bool ActionRegistry::trigger(std::string_view name) {
  auto& action = get(name);
  if(!action.isEnabled()) {
    return false;
  }
  action.trigger();
  return true;
}

void ActionRegistry::build(QMenuBar& menu_bar) {
  m_menu_bar = &menu_bar;
  rebuild();
}

ActionRegistry::Entry* ActionRegistry::find_entry(
    std::string_view name) const {
  for(auto& entry : m_entries) {
    if(entry->m_definition.m_name == name) {
      return entry.get();
    }
  }
  return nullptr;
}

ActionRegistry::Entry& ActionRegistry::get_entry(
    std::string_view name) const {
  if(auto entry = find_entry(name)) {
    return *entry;
  }
  throw std::out_of_range("Action not found.");
}

void ActionRegistry::update(Entry& entry) {
  entry.m_action->setEnabled(entry.m_is_enabled && entry.m_handler);
}

void ActionRegistry::rebuild() {
  for(auto& menu : m_built_menus) {
    if(menu) {
      menu->deleteLater();
    }
  }
  m_built_menus.clear();
  if(!m_menu_bar) {
    return;
  }
  m_menu_bar->clear();
  populate(*m_menu_bar, std::string());
}

bool ActionRegistry::populate(QWidget& container, const std::string& name) {
  using Item = std::variant<const MenuEntry*, const Entry*>;
  auto sections = std::vector<std::string>();
  auto items = std::vector<std::pair<int, Item>>();
  auto add_section = [&] (const std::string& section) {
    if(std::find(sections.begin(), sections.end(), section) ==
        sections.end()) {
      sections.push_back(section);
    }
  };
  for(auto& menu : m_menus) {
    if(menu.m_definition.m_parent == name) {
      items.emplace_back(menu.m_sequence, &menu);
    }
  }
  for(auto& entry : m_entries) {
    if(!entry->m_definition.m_menu.empty() &&
        entry->m_definition.m_menu == name) {
      items.emplace_back(entry->m_sequence, entry.get());
    }
  }
  std::sort(items.begin(), items.end(), [] (const auto& left,
      const auto& right) {
    return left.first < right.first;
  });
  auto get_section = [] (const Item& item) -> const std::string& {
    if(auto menu = std::get_if<const MenuEntry*>(&item)) {
      return (*menu)->m_definition.m_section;
    }
    return std::get<const Entry*>(item)->m_definition.m_section;
  };
  for(auto& item : items) {
    add_section(get_section(item.second));
  }
  auto is_menu = static_cast<bool>(qobject_cast<QMenu*>(&container));
  auto has_items = false;
  for(auto& section : sections) {
    auto is_section_started = false;
    for(auto& item : items) {
      if(get_section(item.second) != section) {
        continue;
      }
      if(!is_section_started && has_items && is_menu) {
        auto separator = new QAction(&container);
        separator->setSeparator(true);
        container.addAction(separator);
      }
      is_section_started = true;
      has_items = true;
      if(auto menu = std::get_if<const MenuEntry*>(&item.second)) {
        auto child = new QMenu((*menu)->m_definition.m_title, &container);
        m_built_menus.push_back(child);
        container.addAction(child->menuAction());
        child->menuAction()->setEnabled(
          populate(*child, (*menu)->m_definition.m_name));
      } else {
        container.addAction(
          std::get<const Entry*>(item.second)->m_action.get());
      }
    }
  }
  return has_items;
}

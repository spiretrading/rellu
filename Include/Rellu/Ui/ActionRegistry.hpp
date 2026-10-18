#ifndef RELLU_UI_ACTION_REGISTRY_HPP
#define RELLU_UI_ACTION_REGISTRY_HPP
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QString>
#include <QWidget>

namespace Rellu {

  /** Describes an action registered with an ActionRegistry. */
  struct ActionDefinition {

    /** The unique name, such as <code>edit.undo</code>. */
    std::string m_name;

    /** The text shown in menus, with an optional mnemonic. */
    QString m_text;

    /** The default shortcut. */
    QKeySequence m_shortcut;

    /** The name of the menu listing the action, or empty for none. */
    std::string m_menu;

    /**
     * The section of the menu holding the action. Sections appear in the
     * order they are first used and are separated from one another.
     */
    std::string m_section;

    /** Whether the action toggles between checked and unchecked. */
    bool m_is_checkable = false;
  };

  /** Describes a menu registered with an ActionRegistry. */
  struct MenuDefinition {

    /** The unique name, such as <code>file</code>. */
    std::string m_name;

    /** The title shown, with an optional mnemonic. */
    QString m_title;

    /** The name of the menu holding this one, or empty for the menu bar. */
    std::string m_parent;

    /** The section of the parent menu holding this one. */
    std::string m_section;
  };

  /**
   * Keeps the application's named actions with their shortcuts, enabled
   * state and handlers, and builds menus listing them. An action is enabled
   * only while it has a handler and has not been disabled.
   */
  class ActionRegistry {
    public:

      /**
       * Constructs an empty ActionRegistry.
       * @param owner The widget whose window the shortcuts apply to.
       */
      explicit ActionRegistry(QWidget& owner);

      ~ActionRegistry();

      /**
       * Adds a menu, throwing std::invalid_argument if its name is in use.
       * @param definition The menu's definition.
       */
      void add(MenuDefinition definition);

      /**
       * Adds an action without a handler, throwing std::invalid_argument if
       * its name is in use.
       * @param definition The action's definition.
       * @return The action.
       */
      QAction& add(ActionDefinition definition);

      /**
       * Removes an action.
       * @param name The action's name.
       */
      void remove(std::string_view name);

      /**
       * Finds an action.
       * @param name The action's name.
       * @return The action, or <code>nullptr</code> if there is none.
       */
      QAction* find(std::string_view name) const;

      /**
       * Returns an action, throwing std::out_of_range if there is none.
       * @param name The action's name.
       */
      QAction& get(std::string_view name) const;

      /** Returns the names of the actions in the order they were added. */
      std::vector<std::string> get_names() const;

      /**
       * Returns an action's default shortcut, throwing std::out_of_range if
       * there is no such action.
       * @param name The action's name.
       */
      const QKeySequence& get_default_shortcut(std::string_view name) const;

      /**
       * Sets the function an action calls when triggered, enabling the
       * action unless it was disabled.
       * @param name The action's name.
       * @param handler The function to call, or empty to disable the action.
       */
      void set_handler(std::string_view name, std::function<void ()> handler);

      /**
       * Sets whether an action may be triggered while it has a handler.
       * @param name The action's name.
       * @param is_enabled Whether the action may be triggered.
       */
      void set_enabled(std::string_view name, bool is_enabled);

      /**
       * Triggers an action if it is enabled.
       * @param name The action's name.
       * @return <code>true</code> iff the action was enabled.
       */
      bool trigger(std::string_view name);

      /**
       * Fills a menu bar with the registered menus, keeping it up to date as
       * menus and actions are added and removed.
       * @param menu_bar The menu bar to fill.
       */
      void build(QMenuBar& menu_bar);

    private:
      struct Entry {
        ActionDefinition m_definition;
        std::unique_ptr<QAction> m_action;
        std::function<void ()> m_handler;
        bool m_is_enabled;
        int m_sequence;
      };
      struct MenuEntry {
        MenuDefinition m_definition;
        int m_sequence;
      };
      QWidget* m_owner;
      std::vector<std::unique_ptr<Entry>> m_entries;
      std::vector<MenuEntry> m_menus;
      int m_sequence;
      QPointer<QMenuBar> m_menu_bar;
      std::vector<QPointer<QMenu>> m_built_menus;

      ActionRegistry(const ActionRegistry&) = delete;
      ActionRegistry& operator =(const ActionRegistry&) = delete;
      Entry* find_entry(std::string_view name) const;
      Entry& get_entry(std::string_view name) const;
      void update(Entry& entry);
      void rebuild();
      bool populate(QWidget& container, const std::string& name);
  };
}

#endif

#ifndef RELLU_UI_TOOLBAR_HPP
#define RELLU_UI_TOOLBAR_HPP
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <QIcon>
#include <QKeySequence>
#include <QString>
#include <QToolButton>
#include <QWidget>

namespace Rellu {

  /** Describes a button of the Toolbar. */
  struct ToolEntry {

    /** The unique name, such as <code>rectangle</code>. */
    std::string m_name;

    /** The label shown in the button's tooltip. */
    QString m_label;

    /** The icon, whose checked state is drawn while the button is active. */
    QIcon m_icon;

    /** The key that activates the button. */
    QKeySequence m_shortcut;
  };

  /** Returns Adobe XD's tools in the order of XD's toolbar. */
  std::vector<ToolEntry> make_xd_tools();

  /**
   * Returns the buttons at the bottom of Adobe XD's toolbar showing the
   * Document Assets, Layers and Plugins panels.
   */
  std::vector<ToolEntry> make_xd_panels();

  /**
   * The vertical toolbar at the left of the window: the tools at the top,
   * exactly one of which is active, and the panel buttons at the bottom, at
   * most one of which is active.
   */
  class Toolbar : public QWidget {
    public:

      /**
       * The type of function called when the user activates a button.
       * @param name The button's name, or empty when the user deactivates
       *        the active panel button.
       */
      using SelectionHandler = std::function<void (const std::string& name)>;

      /** The width of the toolbar in device independent pixels. */
      static constexpr auto WIDTH = 50;

      /**
       * Constructs a Toolbar with the first tool active and no panel
       * active.
       * @param tools The tools, from top to bottom.
       * @param panels The panel buttons, from top to bottom.
       * @param parent The parent widget.
       */
      Toolbar(std::vector<ToolEntry> tools, std::vector<ToolEntry> panels,
        QWidget* parent = nullptr);

      /** Returns the tools. */
      const std::vector<ToolEntry>& get_tools() const;

      /** Returns the panel buttons. */
      const std::vector<ToolEntry>& get_panels() const;

      /** Returns the name of the active tool. */
      const std::string& get_tool() const;

      /**
       * Makes a tool active, without calling the tool handler.
       * @param name The tool's name.
       */
      void set_tool(std::string_view name);

      /** Returns the name of the active panel button, or empty if none. */
      const std::string& get_panel() const;

      /**
       * Makes a panel button active, without calling the panel handler.
       * @param name The button's name, or empty for none.
       */
      void set_panel(std::string_view name);

      /**
       * Shows or hides a tool's button.
       * @param name The tool's name.
       * @param is_visible Whether the button is shown.
       */
      void set_tool_visible(std::string_view name, bool is_visible);

      /**
       * Returns a button.
       * @param name The name of the tool or panel button.
       * @return The button, or <code>nullptr</code> if there is none.
       */
      QToolButton* find_button(std::string_view name) const;

      /**
       * Sets the function called when the user activates a tool.
       * @param handler The function to call.
       */
      void set_tool_handler(SelectionHandler handler);

      /**
       * Sets the function called when the user activates or deactivates a
       * panel button.
       * @param handler The function to call.
       */
      void set_panel_handler(SelectionHandler handler);

    private:
      std::vector<ToolEntry> m_tools;
      std::vector<ToolEntry> m_panels;
      std::vector<QToolButton*> m_tool_buttons;
      std::vector<QToolButton*> m_panel_buttons;
      std::string m_tool;
      std::string m_panel;
      SelectionHandler m_tool_handler;
      SelectionHandler m_panel_handler;

      QToolButton* make_button(const ToolEntry& entry);
      void update_buttons();
  };
}

#endif

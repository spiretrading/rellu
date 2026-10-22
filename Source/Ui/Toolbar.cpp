#include <utility>
#include <QVBoxLayout>
#include "Rellu/Ui/Toolbar.hpp"
#include "Rellu/Ui/ToolTips.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto BUTTON_SIZE = 40;
  constexpr auto ICON_SIZE = 22;
  constexpr auto TOP_MARGIN = 4;
  constexpr auto SPACING = 0;
  constexpr auto BOTTOM_MARGIN = 4;
  constexpr auto BORDER_WIDTH = 2;

  ToolEntry make_entry(std::string name, QString label, Glyph glyph,
      const char* shortcut) {
    return ToolEntry(std::move(name), std::move(label), get_icon(glyph),
      QKeySequence(shortcut));
  }
}

std::vector<ToolEntry> Rellu::make_xd_tools() {
  auto tools = std::vector<ToolEntry>();
  tools.push_back(make_entry("select", "Select", Glyph::SELECT, "V"));
  tools.push_back(
    make_entry("rectangle", "Rectangle", Glyph::RECTANGLE, "R"));
  tools.push_back(make_entry("ellipse", "Ellipse", Glyph::ELLIPSE, "E"));
  tools.push_back(make_entry("polygon", "Polygon", Glyph::POLYGON, "Y"));
  tools.push_back(make_entry("line", "Line", Glyph::LINE, "L"));
  tools.push_back(make_entry("pen", "Pen", Glyph::PEN, "P"));
  tools.push_back(make_entry("text", "Text", Glyph::TEXT, "T"));
  tools.push_back(make_entry("artboard", "Artboard", Glyph::ARTBOARD, "A"));
  tools.push_back(make_entry("zoom", "Zoom", Glyph::ZOOM, "Z"));
  return tools;
}

std::vector<ToolEntry> Rellu::make_xd_panels() {
  auto panels = std::vector<ToolEntry>();
  panels.push_back(make_entry(
    "assets", "Document Assets", Glyph::ASSETS, "Shift+Ctrl+Y"));
  panels.push_back(make_entry("layers", "Layers", Glyph::LAYERS, "Ctrl+Y"));
  panels.push_back(
    make_entry("plugins", "Plugins", Glyph::PLUGINS, "Shift+Ctrl+P"));
  return panels;
}

Toolbar::Toolbar(std::vector<ToolEntry> tools, std::vector<ToolEntry> panels,
    QWidget* parent)
    : QWidget(parent),
      m_tools(std::move(tools)),
      m_panels(std::move(panels)) {
  setObjectName("toolbar");
  setAttribute(Qt::WA_StyledBackground);
  setFixedWidth(WIDTH);
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, TOP_MARGIN, BORDER_WIDTH, BOTTOM_MARGIN);
  layout->setSpacing(SPACING);
  for(auto& tool : m_tools) {
    auto button = make_button(tool);
    m_tool_buttons.push_back(button);
    layout->addWidget(button, 0, Qt::AlignHCenter);
    connect(button, &QToolButton::clicked, this, [this, name = tool.m_name] {
      set_tool(name);
      if(m_tool_handler) {
        m_tool_handler(name);
      }
    });
  }
  layout->addStretch(1);
  for(auto& panel : m_panels) {
    auto button = make_button(panel);
    m_panel_buttons.push_back(button);
    layout->addWidget(button, 0, Qt::AlignHCenter);
    connect(button, &QToolButton::clicked, this, [this, name = panel.m_name] {
      if(m_panel == name) {
        set_panel("");
      } else {
        set_panel(name);
      }
      if(m_panel_handler) {
        m_panel_handler(m_panel);
      }
    });
  }
  if(!m_tools.empty()) {
    m_tool = m_tools.front().m_name;
  }
  update_buttons();
}

const std::vector<ToolEntry>& Toolbar::get_tools() const {
  return m_tools;
}

const std::vector<ToolEntry>& Toolbar::get_panels() const {
  return m_panels;
}

const std::string& Toolbar::get_tool() const {
  return m_tool;
}

void Toolbar::set_tool(std::string_view name) {
  for(auto& tool : m_tools) {
    if(tool.m_name == name) {
      m_tool = tool.m_name;
      break;
    }
  }
  update_buttons();
}

const std::string& Toolbar::get_panel() const {
  return m_panel;
}

void Toolbar::set_panel(std::string_view name) {
  auto selected = std::string();
  for(auto& panel : m_panels) {
    if(panel.m_name == name) {
      selected = panel.m_name;
      break;
    }
  }
  m_panel = std::move(selected);
  update_buttons();
}

void Toolbar::set_tool_visible(std::string_view name, bool is_visible) {
  if(auto button = find_button(name)) {
    button->setVisible(is_visible);
  }
}

QToolButton* Toolbar::find_button(std::string_view name) const {
  for(auto i = std::size_t(0); i != m_tools.size(); ++i) {
    if(m_tools[i].m_name == name) {
      return m_tool_buttons[i];
    }
  }
  for(auto i = std::size_t(0); i != m_panels.size(); ++i) {
    if(m_panels[i].m_name == name) {
      return m_panel_buttons[i];
    }
  }
  return nullptr;
}

void Toolbar::set_tool_handler(SelectionHandler handler) {
  m_tool_handler = std::move(handler);
}

void Toolbar::set_panel_handler(SelectionHandler handler) {
  m_panel_handler = std::move(handler);
}

QToolButton* Toolbar::make_button(const ToolEntry& entry) {
  auto button = new QToolButton();
  button->setObjectName("toolbar_button");
  button->setCheckable(true);
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
  button->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
  button->setIcon(entry.m_icon);
  set_tool_tip_placement(*button, ToolTipPlacement::RIGHT);
  auto label = translate("toolbar", entry.m_label);
  button->setAccessibleName(label);
  if(entry.m_shortcut.isEmpty()) {
    button->setToolTip(label);
  } else {
    auto tip = QString("%1 (%2)").arg(entry.m_label,
      to_xd_tool_tip_shortcut_text(entry.m_shortcut, Language::ENGLISH));
    auto translation = translate("toolbar", tip);
    if(translation == tip) {
      translation = QString("%1 (%2)").arg(
        label, to_xd_tool_tip_shortcut_text(entry.m_shortcut));
    }
    button->setToolTip(translation);
  }
  return button;
}

void Toolbar::update_buttons() {
  for(auto i = std::size_t(0); i != m_tools.size(); ++i) {
    m_tool_buttons[i]->setChecked(m_tools[i].m_name == m_tool);
  }
  for(auto i = std::size_t(0); i != m_panels.size(); ++i) {
    m_panel_buttons[i]->setChecked(m_panels[i].m_name == m_panel);
  }
}

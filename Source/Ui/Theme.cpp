#include <QColor>
#include <QStyleFactory>
#include "Rellu/Ui/Theme.hpp"

using namespace Rellu;

namespace {
  const auto WINDOW = QColor(0xF3, 0xF4, 0xF6);
  const auto BASE = QColor(0xFF, 0xFF, 0xFF);
  const auto ALTERNATE_BASE = QColor(0xF7, 0xF8, 0xFA);
  const auto TEXT = QColor(0x1F, 0x23, 0x28);
  const auto DISABLED_TEXT = QColor(0xA0, 0xA5, 0xAC);
  const auto BUTTON = QColor(0xFA, 0xFA, 0xFB);
  const auto BORDER = QColor(0xD9, 0xDC, 0xE1);
  const auto HIGHLIGHT = QColor(0x2F, 0x6F, 0xEB);
  const auto SELECTION = QColor(0xDC, 0xE8, 0xFD);

  const auto STYLE_SHEET = QString(R"(
QMainWindow::separator {
  background: #D9DCE1;
  width: 1px;
  height: 1px;
}
QDockWidget {
  font-weight: 600;
}
QDockWidget::title {
  background: #F3F4F6;
  padding: 6px 8px;
  border-bottom: 1px solid #D9DCE1;
}
QDockWidget > QWidget {
  font-weight: normal;
  background: #FFFFFF;
}
QTreeView {
  background: #FFFFFF;
  border: none;
  outline: 0;
}
QTreeView::item {
  padding: 2px 0px;
}
QTreeView::item:selected {
  background: #DCE8FD;
  color: #1F2328;
}
QTreeView::item:hover:!selected {
  background: #F1F4F9;
}
QLabel#placeholder {
  color: #8A9099;
  padding: 16px;
}
QStatusBar {
  background: #F3F4F6;
  border-top: 1px solid #D9DCE1;
  color: #4A4F57;
}
QStatusBar::item {
  border: none;
}
QStatusBar QLabel {
  padding: 0px 8px;
}
QMenuBar {
  background: #FFFFFF;
  border-bottom: 1px solid #D9DCE1;
}
QMenuBar::item {
  padding: 4px 10px;
  background: transparent;
}
QMenuBar::item:selected {
  background: #EDEFF2;
  border-radius: 4px;
}
QMenu {
  background: #FFFFFF;
  border: 1px solid #D9DCE1;
  padding: 4px 0px;
}
QMenu::item {
  padding: 5px 28px 5px 24px;
}
QMenu::item:selected {
  background: #DCE8FD;
  color: #1F2328;
}
QMenu::item:disabled {
  color: #A0A5AC;
}
QMenu::separator {
  height: 1px;
  background: #E6E8EC;
  margin: 4px 8px;
}
)");
}

QPalette Rellu::make_light_palette() {
  auto palette = QPalette();
  palette.setColor(QPalette::Window, WINDOW);
  palette.setColor(QPalette::WindowText, TEXT);
  palette.setColor(QPalette::Base, BASE);
  palette.setColor(QPalette::AlternateBase, ALTERNATE_BASE);
  palette.setColor(QPalette::Text, TEXT);
  palette.setColor(QPalette::PlaceholderText, DISABLED_TEXT);
  palette.setColor(QPalette::Button, BUTTON);
  palette.setColor(QPalette::ButtonText, TEXT);
  palette.setColor(QPalette::BrightText, BASE);
  palette.setColor(QPalette::ToolTipBase, BASE);
  palette.setColor(QPalette::ToolTipText, TEXT);
  palette.setColor(QPalette::Highlight, HIGHLIGHT);
  palette.setColor(QPalette::HighlightedText, BASE);
  palette.setColor(QPalette::Link, HIGHLIGHT);
  palette.setColor(QPalette::Light, BASE);
  palette.setColor(QPalette::Midlight, ALTERNATE_BASE);
  palette.setColor(QPalette::Mid, BORDER);
  palette.setColor(QPalette::Dark, BORDER.darker(120));
  palette.setColor(QPalette::Shadow, BORDER.darker(160));
  for(auto role :
      {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    palette.setColor(QPalette::Disabled, role, DISABLED_TEXT);
  }
  palette.setColor(QPalette::Inactive, QPalette::Highlight, SELECTION);
  palette.setColor(QPalette::Inactive, QPalette::HighlightedText, TEXT);
  return palette;
}

void Rellu::apply_theme(QApplication& application) {
  if(auto style = QStyleFactory::create("Fusion")) {
    application.setStyle(style);
  }
  application.setPalette(make_light_palette());
  application.setStyleSheet(STYLE_SHEET);
}

#include <algorithm>
#include <QColor>
#include <QFont>
#include <QKeySequence>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include "Rellu/Ui/Theme.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "Colors.hpp"

using namespace Rellu;

namespace {
  constexpr auto MENU_ITEM_HEIGHT = 32;
  constexpr auto MENU_TEXT_INDENT = 27;
  constexpr auto MENU_SHORTCUT_GAP = 32;
  constexpr auto MENU_RIGHT_MARGIN = 14;
  constexpr auto MENU_ARROW_WIDTH = 22;
  constexpr auto MENU_BAR_HEIGHT = 32;
  constexpr auto MENU_BAR_PADDING = 12;
  constexpr auto CHECK_BOX_SIZE = 16;
  const auto MENU_BORDER = QColor(0xD0, 0xD0, 0xD0);
  const auto WHITE = QColor(0xFF, 0xFF, 0xFF);

  const auto STYLE_SHEET = QString(R"(
QMainWindow, QWidget#central {
  background: #F5F5F5;
}
QWidget#menu_row {
  background: #E4E4E4;
}
QWidget#app_bar {
  background: #F5F5F5;
  border-bottom: 1px solid #E4E4E4;
}
QWidget#toolbar {
  background: #F5F5F5;
  border-right: 1px solid #E4E4E4;
}
QStackedWidget#left_panel {
  background: #F5F5F5;
  border-right: 1px solid #E4E4E4;
}
QWidget#assets_panel, QWidget#plugins_panel, QWidget#layers_panel,
QWidget#assets_content, QScrollArea#assets_scroll {
  background: #F5F5F5;
}
QWidget#inspector_frame {
  background: #F5F5F5;
  border-left: 1px solid #E4E4E4;
}
QScrollArea#inspector_scroll, QWidget#inspector_content {
  background: #F5F5F5;
}
QToolTip {
  background: #FFFFFF;
  color: #3F3F3F;
  border: 1px solid #D0D0D0;
  padding: 3px 6px;
}
QToolButton#toolbar_button, QToolButton#app_bar_button,
QToolButton#panel_button, QToolButton#zoom_presets {
  border: none;
  border-radius: 4px;
  background: transparent;
}
QToolButton#toolbar_button:hover, QToolButton#app_bar_button:hover,
QToolButton#panel_button:hover, QToolButton#zoom_presets:hover {
  background: #EAEAEA;
}
QToolButton#zoom_presets::menu-indicator {
  image: none;
  width: 0px;
}
QToolButton#workspace_button {
  border: none;
  border-top: 2px solid transparent;
  border-bottom: 2px solid transparent;
  background: transparent;
  color: #8E8E8E;
  font-size: 14px;
  padding: 0px 2px;
}
QToolButton#workspace_button:checked {
  color: #3F3F3F;
  border-bottom: 2px solid #3F3F3F;
}
QToolButton#workspace_button:hover:!checked {
  color: #6E6E6E;
}
QLabel#document_title {
  color: #3F3F3F;
  font-size: 14px;
}
QWidget#zoom_control {
  border-bottom: 1px solid #CACACA;
}
QLineEdit#zoom_field {
  background: transparent;
  border: none;
  color: #3F3F3F;
  font-size: 13px;
}
QLabel#panel_heading {
  color: #6E6E6E;
  font-size: 11px;
  letter-spacing: 1px;
}
QLineEdit#search_field {
  background: transparent;
  border: none;
  border-bottom: 1px solid #CACACA;
  color: #3F3F3F;
  font-size: 12px;
  padding: 4px 0px;
}
QToolButton#section_toggle {
  border: none;
  background: transparent;
  color: #3F3F3F;
  font-weight: 600;
  font-size: 12px;
}
QLabel#asset_label {
  color: #3F3F3F;
  font-size: 12px;
}
QLabel#character_style_preview {
  color: #2C2C2C;
  font-size: 20px;
}
QLabel#empty_message, QLabel#empty_link {
  color: #6E6E6E;
  font-size: 12px;
}
QTreeView#layers {
  background: #F5F5F5;
  border: none;
  outline: 0;
  color: #3F3F3F;
  font-size: 12px;
  show-decoration-selected: 1;
}
QTreeView#layers::item {
  height: 32px;
  border: none;
}
QTreeView#layers::item:selected, QTreeView#layers::branch:selected {
  background: #E0F0FA;
  color: #3F3F3F;
}
QTreeView#layers::item:hover:!selected,
QTreeView#layers::branch:hover:!selected {
  background: #EBEBEB;
}
QLabel#layers_heading {
  color: #8E8E8E;
  font-size: 11px;
  letter-spacing: 1px;
}
QLabel#section_heading {
  color: #8E8E8E;
  font-size: 11px;
  letter-spacing: 1px;
}
QFrame#section_rule {
  background: #E4E4E4;
  border: none;
}
QLabel#field_label {
  color: #8E8E8E;
  font-size: 11px;
}
QLabel#row_label {
  color: #3F3F3F;
  font-size: 12px;
}
QLabel#row_label:disabled, QLabel#field_label:disabled,
QLabel#section_heading:disabled {
  color: #C8C8C8;
}
QLineEdit#number_field, QLineEdit#text_field {
  background: transparent;
  border: none;
  border-bottom: 1px solid #CACACA;
  color: #3F3F3F;
  font-size: 12px;
  padding: 2px 0px;
  selection-background-color: #2680EB;
}
QLineEdit#number_field:focus, QLineEdit#text_field:focus {
  border-bottom: 1px solid #2680EB;
}
QLineEdit#number_field:disabled, QLineEdit#text_field:disabled {
  color: #B8B8B8;
}
QLineEdit#number_field[mixed="true"] {
  color: #3F3F3F;
}
QComboBox#choice_field {
  background: #FAFAFA;
  border: 1px solid #DDDDDD;
  border-radius: 4px;
  color: #3F3F3F;
  font-size: 13px;
  padding: 4px 10px;
  min-height: 20px;
}
QComboBox#choice_field:disabled {
  color: #B8B8B8;
}
QComboBox#choice_field::drop-down, QComboBox#underlined_choice::drop-down {
  border: none;
  width: 26px;
}
QComboBox#choice_field::down-arrow,
QComboBox#underlined_choice::down-arrow {
  image: none;
}
QComboBox#underlined_choice {
  background: transparent;
  border: none;
  border-bottom: 1px solid #CACACA;
  color: #3F3F3F;
  font-size: 13px;
  padding: 2px 0px;
}
QComboBox#underlined_choice:disabled {
  color: #B8B8B8;
}
QComboBox QAbstractItemView {
  background: #FFFFFF;
  border: 1px solid #D0D0D0;
  selection-background-color: #2680EB;
  selection-color: #FFFFFF;
  outline: 0;
}
QToolButton#icon_toggle {
  background: transparent;
  border: none;
  border-radius: 3px;
}
QToolButton#icon_toggle:hover {
  background: #EAEAEA;
}
QToolButton#segment {
  background: #FAFAFA;
  border: 1px solid #DDDDDD;
  border-radius: 0px;
}
QToolButton#segment:hover {
  background: #F0F0F0;
}
QToolButton#text_button {
  background: #FAFAFA;
  border: 1px solid #DDDDDD;
  border-radius: 4px;
  color: #3F3F3F;
  font-size: 13px;
  padding: 4px 10px;
}
QToolButton#text_button:disabled {
  color: #B8B8B8;
}
QToolButton#text_button:checked {
  background: #FFFFFF;
  border: 1px solid #2680EB;
  color: #2680EB;
}
QToolButton#swatch {
  border: none;
  background: transparent;
}
QCheckBox {
  spacing: 10px;
  color: #3F3F3F;
  font-size: 12px;
}
QCheckBox:disabled {
  color: #B8B8B8;
}
QSlider#opacity_slider::groove:horizontal {
  height: 2px;
  background: #6E6E6E;
}
QSlider#opacity_slider::groove:horizontal:disabled {
  background: #D0D0D0;
}
QSlider#opacity_slider::handle:horizontal {
  width: 14px;
  height: 14px;
  margin: -7px 0px;
  border: 2px solid #505050;
  border-radius: 8px;
  background: #F5F5F5;
}
QSlider#opacity_slider::handle:horizontal:disabled {
  width: 14px;
  height: 14px;
  margin: -7px 0px;
  border: 2px solid #D0D0D0;
  border-radius: 8px;
  background: #F5F5F5;
}
QSlider#opacity_slider {
  min-height: 20px;
}
QLabel#placeholder {
  color: #8E8E8E;
  padding: 16px;
}
QScrollBar:vertical {
  width: 8px;
  background: transparent;
  margin: 0px;
}
QScrollBar::handle:vertical {
  background: #C8C8C8;
  border-radius: 3px;
  min-height: 30px;
  margin: 2px;
}
QScrollBar:horizontal {
  height: 8px;
  background: transparent;
  margin: 0px;
}
QScrollBar::handle:horizontal {
  background: #C8C8C8;
  border-radius: 3px;
  min-width: 30px;
  margin: 2px;
}
QScrollBar::add-line, QScrollBar::sub-line {
  width: 0px;
  height: 0px;
}
QScrollBar::add-page, QScrollBar::sub-page {
  background: transparent;
}
)");

  QString to_menu_shortcut(const QString& text) {
    auto sequence = QKeySequence::fromString(text, QKeySequence::NativeText);
    if(sequence.count() != 1) {
      return text;
    }
    return to_xd_shortcut_text(sequence);
  }

  void paint_check(QPainter& painter, const QRectF& box, const QColor& color) {
    auto path = QPainterPath(QPointF(box.left() + box.width() * 0.22,
      box.top() + box.height() * 0.52));
    path.lineTo(box.left() + box.width() * 0.42,
      box.top() + box.height() * 0.72);
    path.lineTo(box.left() + box.width() * 0.78,
      box.top() + box.height() * 0.30);
    auto pen = QPen(color, std::max(1.5, box.width() / 8));
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
  }

  void paint_chevron(
      QPainter& painter, const QRectF& rect, const QColor& color) {
    auto size = std::min(rect.width(), rect.height());
    auto center = rect.center();
    auto half = size * 0.25;
    auto pen = QPen(color, 1.3);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(QList<QPointF>({
      QPointF(center.x() - half, center.y() - half / 2),
      QPointF(center.x(), center.y() + half / 2),
      QPointF(center.x() + half, center.y() - half / 2)}));
  }

  class RelluStyle : public QProxyStyle {
    public:
      RelluStyle()
        : QProxyStyle(QStyleFactory::create("Fusion")) {}

      int pixelMetric(PixelMetric metric, const QStyleOption* option,
          const QWidget* widget) const override {
        switch(metric) {
          case PM_MenuPanelWidth:
            return 1;
          case PM_MenuHMargin:
          case PM_MenuVMargin:
          case PM_MenuBarPanelWidth:
          case PM_MenuBarVMargin:
          case PM_MenuBarHMargin:
          case PM_MenuBarItemSpacing:
            return 0;
          case PM_IndicatorWidth:
          case PM_IndicatorHeight:
            return CHECK_BOX_SIZE;
          case PM_SubMenuOverlap:
            return 0;
          default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
      }

      QSize sizeFromContents(ContentsType type, const QStyleOption* option,
          const QSize& size, const QWidget* widget) const override {
        if(type == CT_MenuItem) {
          auto item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
          if(!item) {
            return size;
          }
          if(item->menuItemType == QStyleOptionMenuItem::Separator) {
            return QSize(10, 1);
          }
          auto text = item->text;
          auto shortcut = QString();
          if(auto tab = text.indexOf('\t'); tab != -1) {
            shortcut = to_menu_shortcut(text.mid(tab + 1));
            text = text.left(tab);
          }
          auto metrics = item->fontMetrics;
          auto width = MENU_TEXT_INDENT + metrics.horizontalAdvance(
            QString(text).remove('&')) + MENU_RIGHT_MARGIN;
          if(!shortcut.isEmpty()) {
            width += MENU_SHORTCUT_GAP + metrics.horizontalAdvance(shortcut);
          }
          if(item->menuItemType == QStyleOptionMenuItem::SubMenu) {
            width += MENU_SHORTCUT_GAP + MENU_ARROW_WIDTH;
          }
          return QSize(width, MENU_ITEM_HEIGHT);
        } else if(type == CT_MenuBarItem) {
          return QSize(size.width() + 2 * MENU_BAR_PADDING, MENU_BAR_HEIGHT);
        }
        return QProxyStyle::sizeFromContents(type, option, size, widget);
      }

      void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
          QPainter* painter, const QWidget* widget) const override {
        if(element == PE_PanelMenu || element == PE_FrameMenu) {
          painter->save();
          painter->setPen(MENU_BORDER);
          painter->setBrush(element == PE_PanelMenu ? Colors::PANEL :
            QColor(Qt::transparent));
          painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
          painter->restore();
          return;
        } else if(element == PE_PanelMenuBar) {
          painter->fillRect(option->rect, Colors::MENU_ROW);
          return;
        } else if(element == PE_IndicatorCheckBox) {
          draw_check_box(*option, *painter);
          return;
        } else if(element == PE_IndicatorArrowDown) {
          painter->save();
          painter->setRenderHint(QPainter::Antialiasing);
          paint_chevron(*painter, QRectF(option->rect),
            option->state & State_Enabled ? Colors::ICON :
            Colors::DISABLED_TEXT);
          painter->restore();
          return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
      }

      void drawControl(ControlElement element, const QStyleOption* option,
          QPainter* painter, const QWidget* widget) const override {
        if(element == CE_MenuItem) {
          if(auto item =
              qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
            draw_menu_item(*item, *painter);
            return;
          }
        } else if(element == CE_MenuBarItem) {
          if(auto item =
              qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
            draw_menu_bar_item(*item, *painter);
            return;
          }
        } else if(element == CE_MenuBarEmptyArea ||
            element == CE_MenuEmptyArea) {
          painter->fillRect(option->rect, element == CE_MenuBarEmptyArea ?
            Colors::MENU_ROW : Colors::PANEL);
          return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
      }

      int styleHint(StyleHint hint, const QStyleOption* option,
          const QWidget* widget, QStyleHintReturn* data) const override {
        if(hint == SH_Menu_SubMenuPopupDelay) {
          return 150;
        } else if(hint == SH_ComboBox_Popup) {
          return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, data);
      }

    private:
      void draw_check_box(
          const QStyleOption& option, QPainter& painter) const {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        auto box = QRectF(option.rect).adjusted(1.5, 1.5, -1.5, -1.5);
        auto is_enabled = static_cast<bool>(option.state & State_Enabled);
        auto color = is_enabled ? Colors::CHECK : Colors::DISABLED_TEXT;
        if(option.state & State_On) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawRoundedRect(box.adjusted(-0.75, -0.75, 0.75, 0.75), 2,
            2);
          paint_check(painter, box, WHITE);
        } else if(option.state & State_NoChange) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawRoundedRect(box.adjusted(-0.75, -0.75, 0.75, 0.75), 2,
            2);
          painter.setPen(QPen(WHITE, 2));
          painter.drawLine(QPointF(box.left() + 3, box.center().y()),
            QPointF(box.right() - 3, box.center().y()));
        } else {
          painter.setPen(QPen(color, 1.5));
          painter.setBrush(Qt::NoBrush);
          painter.drawRoundedRect(box, 2, 2);
        }
        painter.restore();
      }

      void draw_menu_item(
          const QStyleOptionMenuItem& item, QPainter& painter) const {
        painter.save();
        auto& rect = item.rect;
        painter.fillRect(rect, Colors::PANEL);
        if(item.menuItemType == QStyleOptionMenuItem::Separator) {
          painter.fillRect(QRect(rect.left(), rect.center().y(),
            rect.width(), 1), Colors::MENU_SEPARATOR);
          painter.restore();
          return;
        }
        auto is_enabled = static_cast<bool>(item.state & State_Enabled);
        auto is_selected =
          is_enabled && static_cast<bool>(item.state & State_Selected);
        if(is_selected) {
          painter.fillRect(rect, Colors::SELECTION);
        }
        auto color = is_selected ? WHITE :
          is_enabled ? Colors::TEXT : Colors::DISABLED_TEXT;
        painter.setRenderHint(QPainter::Antialiasing);
        if(item.checkType != QStyleOptionMenuItem::NotCheckable &&
            item.checked) {
          auto box = QRectF(rect.left() + 7, rect.center().y() - 6, 12, 12);
          paint_check(painter, box, color);
        }
        auto text = item.text;
        auto shortcut = QString();
        if(auto tab = text.indexOf('\t'); tab != -1) {
          shortcut = to_menu_shortcut(text.mid(tab + 1));
          text = text.left(tab);
        }
        painter.setFont(item.font);
        painter.setPen(color);
        auto text_rect = rect.adjusted(MENU_TEXT_INDENT, 0, 0, 0);
        painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft |
          Qt::TextShowMnemonic | Qt::TextHideMnemonic, text);
        if(!shortcut.isEmpty()) {
          painter.drawText(rect.adjusted(0, 0, -MENU_RIGHT_MARGIN, 0),
            Qt::AlignVCenter | Qt::AlignRight, shortcut);
        }
        if(item.menuItemType == QStyleOptionMenuItem::SubMenu) {
          auto center = QPointF(rect.right() - MENU_RIGHT_MARGIN - 4,
            rect.center().y() + 0.5);
          painter.setPen(Qt::NoPen);
          painter.setBrush(is_selected ? WHITE :
            is_enabled ? QColor(0x1F, 0x1F, 0x1F) : Colors::DISABLED_TEXT);
          painter.drawPolygon(QList<QPointF>({
            QPointF(center.x() - 3.5, center.y() - 6),
            QPointF(center.x() + 3.5, center.y()),
            QPointF(center.x() - 3.5, center.y() + 6)}));
        }
        painter.restore();
      }

      void draw_menu_bar_item(
          const QStyleOptionMenuItem& item, QPainter& painter) const {
        painter.save();
        auto is_open = static_cast<bool>(item.state & State_Sunken);
        auto is_hovered = static_cast<bool>(item.state & State_Selected);
        if(is_open) {
          painter.fillRect(item.rect, Colors::MENU_PRESSED);
        } else if(is_hovered) {
          painter.fillRect(item.rect, QColor(0xD6, 0xD6, 0xD6));
        } else {
          painter.fillRect(item.rect, Colors::MENU_ROW);
        }
        painter.setFont(item.font);
        painter.setPen(item.state & State_Enabled ? Colors::TEXT :
          Colors::DISABLED_TEXT);
        painter.drawText(item.rect, Qt::AlignCenter | Qt::TextShowMnemonic |
          Qt::TextHideMnemonic, item.text);
        painter.restore();
      }
  };
}

QPalette Rellu::make_light_palette() {
  auto palette = QPalette();
  palette.setColor(QPalette::Window, Colors::PANEL);
  palette.setColor(QPalette::WindowText, Colors::TEXT);
  palette.setColor(QPalette::Base, WHITE);
  palette.setColor(QPalette::AlternateBase, Colors::PANEL);
  palette.setColor(QPalette::Text, Colors::TEXT);
  palette.setColor(QPalette::PlaceholderText, Colors::SECONDARY_TEXT);
  palette.setColor(QPalette::Button, Colors::BUTTON);
  palette.setColor(QPalette::ButtonText, Colors::TEXT);
  palette.setColor(QPalette::BrightText, WHITE);
  palette.setColor(QPalette::ToolTipBase, WHITE);
  palette.setColor(QPalette::ToolTipText, Colors::TEXT);
  palette.setColor(QPalette::Highlight, Colors::SELECTION);
  palette.setColor(QPalette::HighlightedText, WHITE);
  palette.setColor(QPalette::Link, Colors::SELECTION);
  palette.setColor(QPalette::Light, WHITE);
  palette.setColor(QPalette::Midlight, Colors::BUTTON);
  palette.setColor(QPalette::Mid, Colors::BORDER);
  palette.setColor(QPalette::Dark, Colors::BORDER.darker(120));
  palette.setColor(QPalette::Shadow, Colors::BORDER.darker(160));
  for(auto role :
      {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    palette.setColor(QPalette::Disabled, role, Colors::DISABLED_TEXT);
  }
  palette.setColor(QPalette::Inactive, QPalette::Highlight, Colors::SELECTION);
  palette.setColor(QPalette::Inactive, QPalette::HighlightedText, WHITE);
  return palette;
}

QColor Rellu::get_canvas_color(const Document& document) {
  for(auto& id : document.get_canvas()) {
    if(document.get(id).get_artboard()) {
      return Colors::PASTEBOARD;
    }
  }
  return WHITE;
}

void Rellu::apply_theme(QApplication& application) {
  application.setStyle(new RelluStyle());
  application.setPalette(make_light_palette());
  auto font = QFont("Segoe UI");
  font.setPixelSize(12);
  QApplication::setFont(font);
  auto menu_font = QFont("Segoe UI");
  menu_font.setPixelSize(14);
  QApplication::setFont(menu_font, "QMenu");
  QApplication::setFont(menu_font, "QMenuBar");
  application.setStyleSheet(STYLE_SHEET);
}

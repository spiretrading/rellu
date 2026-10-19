#include <algorithm>
#include <cmath>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QToolButton>
#include "Rellu/Ui/AssetsPanel.hpp"
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto THUMBNAIL_SIZE = 28;
  constexpr auto ROW_HEIGHT = 38;
  const auto EM_DASH = QString(QChar(0x2014));

  QColor to_qcolor(const Color& color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  int to_byte(double component) {
    return std::clamp(static_cast<int>(std::lround(component * 255)), 0, 255);
  }

  QPixmap make_thumbnail(auto paint) {
    auto ratio = 3.0;
    auto pixmap = QPixmap(static_cast<int>(THUMBNAIL_SIZE * ratio),
      static_cast<int>(THUMBNAIL_SIZE * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    auto bounds = QRectF(0.5, 0.5, THUMBNAIL_SIZE - 1, THUMBNAIL_SIZE - 1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRect(bounds);
    paint(painter, bounds);
    painter.setPen(QPen(Colors::BORDER, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bounds);
    return pixmap;
  }

  QPixmap make_swatch_thumbnail(const ColorSwatch& swatch) {
    return make_thumbnail([&] (QPainter& painter, const QRectF& bounds) {
      if(swatch.m_gradient.empty()) {
        painter.setBrush(to_qcolor(swatch.m_color));
      } else {
        auto gradient = QLinearGradient(bounds.topLeft(), bounds.topRight());
        for(auto& stop : swatch.m_gradient) {
          gradient.setColorAt(std::clamp(stop.m_offset, 0.0, 1.0),
            to_qcolor(stop.m_color));
        }
        painter.setBrush(gradient);
      }
      painter.drawRect(bounds);
    });
  }

  QPixmap make_component_thumbnail() {
    return make_thumbnail([] (QPainter& painter, const QRectF& bounds) {
      painter.setBrush(QColor(0xE1, 0xE1, 0xE1));
      painter.drawRect(bounds);
      painter.setBrush(QColor(0xC4, 0xC4, 0xC4));
      painter.drawRect(bounds.adjusted(6, 11, -6, -11));
    });
  }

  QToolButton* make_tool_button(Glyph glyph, const QString& tip) {
    auto button = new QToolButton();
    button->setObjectName("panel_button");
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(get_icon(glyph));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(24, 24);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    return button;
  }

  QWidget* make_row(const QPixmap& thumbnail, const QString& thumbnail_text,
      const QString& label) {
    auto row = new QWidget();
    row->setObjectName("asset_row");
    row->setFixedHeight(ROW_HEIGHT);
    auto layout = new QHBoxLayout(row);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(10);
    auto image = new QLabel();
    image->setFixedSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    if(thumbnail_text.isEmpty()) {
      image->setPixmap(thumbnail);
    } else {
      image->setObjectName("character_style_preview");
      image->setText(thumbnail_text);
      image->setAlignment(Qt::AlignCenter);
    }
    layout->addWidget(image);
    auto text = new QLabel(label);
    text->setObjectName("asset_label");
    layout->addWidget(text, 1);
    return row;
  }
}

AssetsPanel::AssetsPanel(QWidget* parent)
    : QWidget(parent),
      m_document(nullptr),
      m_entries(4),
      m_content(new QWidget()),
      m_content_layout(new QVBoxLayout(m_content)),
      m_is_empty_shown(false) {
  setObjectName("assets_panel");
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto header = new QWidget();
  auto header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(10, 10, 16, 0);
  header_layout->setSpacing(2);
  auto back = make_tool_button(Glyph::CHEVRON_LEFT, "Back");
  back->setEnabled(false);
  header_layout->addWidget(back);
  auto title = new QLabel("DOCUMENT ASSETS");
  title->setObjectName("panel_heading");
  header_layout->addWidget(title, 1);
  auto share = make_tool_button(Glyph::SHARE, "Share");
  share->setEnabled(false);
  header_layout->addWidget(share);
  layout->addWidget(header);
  auto search = new QWidget();
  auto search_layout = new QHBoxLayout(search);
  search_layout->setContentsMargins(46, 6, 16, 0);
  search_layout->setSpacing(4);
  auto field = new QLineEdit();
  field->setObjectName("search_field");
  field->setPlaceholderText("Search document assets");
  field->addAction(get_icon(Glyph::SEARCH), QLineEdit::LeadingPosition);
  field->setEnabled(false);
  search_layout->addWidget(field, 1);
  auto filter_menu = make_tool_button(Glyph::CHEVRON_DOWN, "Search Filter");
  filter_menu->setEnabled(false);
  search_layout->addWidget(filter_menu);
  layout->addWidget(search);
  auto controls = new QWidget();
  auto controls_layout = new QHBoxLayout(controls);
  controls_layout->setContentsMargins(14, 10, 16, 6);
  controls_layout->setSpacing(10);
  for(auto [glyph, tip] : {std::pair(Glyph::FILTER, "Filter"),
      std::pair(Glyph::SORT, "Sort")}) {
    auto button = make_tool_button(glyph, tip);
    button->setEnabled(false);
    controls_layout->addWidget(button);
  }
  controls_layout->addStretch(1);
  for(auto [glyph, tip] : {std::pair(Glyph::LIST_VIEW, "List View"),
      std::pair(Glyph::GRID_VIEW, "Grid View")}) {
    auto button = make_tool_button(glyph, tip);
    button->setEnabled(false);
    controls_layout->addWidget(button);
  }
  layout->addWidget(controls);
  auto scroll = new QScrollArea();
  scroll->setObjectName("assets_scroll");
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_content->setObjectName("assets_content");
  m_content_layout->setContentsMargins(0, 0, 0, 16);
  m_content_layout->setSpacing(0);
  scroll->setWidget(m_content);
  layout->addWidget(scroll, 1);
  rebuild();
}

Document* AssetsPanel::get_document() const {
  return m_document;
}

void AssetsPanel::set_document(Document* document) {
  m_subscription.release();
  m_document = document;
  if(m_document) {
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      if(!changes.m_components.empty() || !changes.m_swatches.empty() ||
          !changes.m_character_styles.empty()) {
        rebuild();
      }
    });
  }
  rebuild();
}

std::vector<QString> AssetsPanel::get_entries(AssetSection section) const {
  return m_entries[static_cast<int>(section)];
}

bool AssetsPanel::is_empty_shown() const {
  return m_is_empty_shown;
}

void AssetsPanel::rebuild() {
  for(auto& entries : m_entries) {
    entries.clear();
  }
  while(auto item = m_content_layout->takeAt(0)) {
    if(auto widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  if(m_document) {
    auto& resources = m_document->get_resources();
    for(auto& swatch : resources.m_swatches) {
      m_entries[static_cast<int>(AssetSection::COLORS)].push_back(
        get_swatch_label(swatch));
    }
    for(auto& style : resources.m_character_styles) {
      m_entries[static_cast<int>(AssetSection::CHARACTER_STYLES)].push_back(
        get_character_style_label(style));
    }
    for(auto& component : resources.m_components) {
      m_entries[static_cast<int>(AssetSection::COMPONENTS)].push_back(
        QString::fromStdString(component.second.m_name));
    }
  }
  add_section(AssetSection::COLORS, "Colors");
  add_section(AssetSection::CHARACTER_STYLES, "Character Styles");
  add_section(AssetSection::COMPONENTS, "Components");
  add_section(AssetSection::VIDEOS, "Videos");
  m_is_empty_shown = std::all_of(m_entries.begin(), m_entries.end(),
    [] (const auto& entries) {
      return entries.empty();
    });
  if(m_is_empty_shown) {
    auto invitation = new QWidget();
    auto layout = new QVBoxLayout(invitation);
    layout->setContentsMargins(24, 40, 24, 0);
    layout->setSpacing(12);
    auto illustration = new QLabel();
    illustration->setAlignment(Qt::AlignCenter);
    illustration->setPixmap(
      make_icon(Glyph::ASSETS, Colors::SECONDARY_TEXT).pixmap(72, 72));
    layout->addWidget(illustration);
    auto message = new QLabel(
      "Add your styles and components, or use elements from existing "
      "libraries.");
    message->setObjectName("empty_message");
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto link = new QLabel("<u>View libraries</u>");
    link->setObjectName("empty_link");
    link->setAlignment(Qt::AlignCenter);
    link->setEnabled(false);
    layout->addWidget(link);
    m_content_layout->addWidget(invitation);
  }
  m_content_layout->addStretch(1);
}

void AssetsPanel::add_section(AssetSection section, const QString& title) {
  auto header = new QWidget();
  auto header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(10, 10, 16, 4);
  header_layout->setSpacing(4);
  auto toggle = new QToolButton();
  toggle->setObjectName("section_toggle");
  toggle->setAutoRaise(true);
  toggle->setCheckable(true);
  toggle->setChecked(true);
  toggle->setFocusPolicy(Qt::NoFocus);
  toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toggle->setIcon(get_icon(Glyph::CHEVRON_DOWN));
  toggle->setIconSize(QSize(14, 14));
  toggle->setText(title);
  header_layout->addWidget(toggle, 1, Qt::AlignLeft);
  auto add = make_tool_button(Glyph::PLUS, "Add");
  add->setEnabled(false);
  header_layout->addWidget(add);
  m_content_layout->addWidget(header);
  auto rows = new QWidget();
  auto rows_layout = new QVBoxLayout(rows);
  rows_layout->setContentsMargins(0, 0, 0, 0);
  rows_layout->setSpacing(0);
  if(m_document) {
    auto& resources = m_document->get_resources();
    if(section == AssetSection::COLORS) {
      for(auto& swatch : resources.m_swatches) {
        rows_layout->addWidget(make_row(make_swatch_thumbnail(swatch),
          QString(), get_swatch_label(swatch)));
      }
    } else if(section == AssetSection::CHARACTER_STYLES) {
      for(auto& style : resources.m_character_styles) {
        rows_layout->addWidget(make_row(
          QPixmap(), "Ag", get_character_style_label(style)));
      }
    } else if(section == AssetSection::COMPONENTS) {
      for(auto& component : resources.m_components) {
        rows_layout->addWidget(make_row(make_component_thumbnail(), QString(),
          QString::fromStdString(component.second.m_name)));
      }
    }
  }
  m_content_layout->addWidget(rows);
  QObject::connect(toggle, &QToolButton::toggled, rows, [=] (bool is_open) {
    rows->setVisible(is_open);
    toggle->setIcon(
      get_icon(is_open ? Glyph::CHEVRON_DOWN : Glyph::CHEVRON_RIGHT));
  });
}

QString Rellu::get_swatch_label(const ColorSwatch& swatch) {
  if(!swatch.m_name.empty()) {
    return QString::fromStdString(swatch.m_name);
  }
  return QString("#%1%2%3").arg(to_byte(swatch.m_color.m_red), 2, 16,
    QChar('0')).arg(to_byte(swatch.m_color.m_green), 2, 16, QChar('0')).arg(
    to_byte(swatch.m_color.m_blue), 2, 16, QChar('0')).toUpper();
}

QString Rellu::get_character_style_label(const CharacterStyle& style) {
  auto& font = style.m_style.m_font;
  auto name = QString::fromStdString(font.m_family);
  if(!font.m_style.empty() && font.m_style != "Regular") {
    name += " " + QString::fromStdString(font.m_style);
  }
  auto size = QString::number(std::round(font.m_size * 100) / 100);
  return QString("%1 %2 %3pt").arg(name, EM_DASH, size);
}

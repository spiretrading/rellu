#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <tuple>
#include <utility>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDrag>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QToolButton>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Ui/AssetMoveDialog.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto THUMBNAIL_SIZE = 28;
  constexpr auto THUMBNAIL_RATIO = 3.0;
  constexpr auto ROW_HEIGHT = 38;
  constexpr auto MAXIMUM_CHECKED_CHANGES = 4096;
  constexpr auto MAXIMUM_DEPTH = 256;
  constexpr auto GROUP_INDENT = 16;
  const auto EM_DASH = QString(QChar(0x2014));
  const auto HIGHLIGHT = QString("background: #E0F0FA;");

  std::string get_section_id(AssetSection section) {
    if(section == AssetSection::COLORS) {
      return COLOR_SECTION;
    }
    return CHARACTER_STYLE_SECTION;
  }

  class GroupRow : public QWidget {
    public:
      std::function<void ()> m_double_click;
      std::function<QMenu* (QWidget*)> m_menu;

    protected:
      void mouseDoubleClickEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton && m_double_click) {
          m_double_click();
        }
      }

      void contextMenuEvent(QContextMenuEvent* event) override {
        if(!m_menu) {
          return;
        }
        auto menu = m_menu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(event->globalPos());
      }
  };

  QColor to_qcolor(const Color& color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  int to_byte(double component) {
    return std::clamp(static_cast<int>(std::lround(component * 255)), 0, 255);
  }

  QPixmap make_thumbnail(auto paint) {
    auto ratio = THUMBNAIL_RATIO;
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

  QPixmap make_component_thumbnail(const QImage& image) {
    return make_thumbnail([&] (QPainter& painter, const QRectF& bounds) {
      auto inner = bounds.adjusted(2, 2, -2, -2);
      auto size =
        QSizeF(image.size()).scaled(inner.size(), Qt::KeepAspectRatio);
      auto target = QRectF(inner.center().x() - size.width() / 2,
        inner.center().y() - size.height() / 2, size.width(), size.height());
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(target, image);
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
    button->setToolTip(translate("assets", tip));
    button->setAccessibleName(translate("assets", tip));
    return button;
  }

  void fill_row(QWidget& row, const QPixmap& thumbnail,
      const QString& thumbnail_text, const QString& label) {
    row.setObjectName("asset_row");
    row.setFixedHeight(ROW_HEIGHT);
    auto layout = new QHBoxLayout(&row);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(10);
    auto image = new QLabel();
    image->setObjectName("asset_thumbnail");
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
  }

  class AssetRow : public QWidget {
    public:
      std::function<void (Qt::KeyboardModifiers)> m_click;
      std::function<void ()> m_double_click;
      std::function<QMenu* (QWidget*)> m_menu;

    protected:
      void mousePressEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton && m_click) {
          m_click(event->modifiers());
        }
        QWidget::mousePressEvent(event);
      }

      void mouseDoubleClickEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton && m_double_click) {
          m_double_click();
        }
      }

      void contextMenuEvent(QContextMenuEvent* event) override {
        if(!m_menu) {
          return;
        }
        auto menu = m_menu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(event->globalPos());
      }
  };

  std::optional<double> parse_number(const QString& text) {
    auto is_valid = false;
    auto number = text.trimmed().toDouble(&is_valid);
    if(!is_valid || !std::isfinite(number)) {
      return std::nullopt;
    }
    return number;
  }

  QString format_number(double value) {
    return QString::number(std::round(value * 100) / 100);
  }

  QString to_hex(const Color& color) {
    return QString("#%1%2%3").arg(to_byte(color.m_red), 2, 16, QChar('0')).
      arg(to_byte(color.m_green), 2, 16, QChar('0')).
      arg(to_byte(color.m_blue), 2, 16, QChar('0')).toUpper();
  }

  class ComponentRow : public QWidget {
    public:
      ComponentRow(AssetsPanel& panel, const ComponentId& component)
        : m_panel(&panel),
          m_component(component) {}

    protected:
      void mousePressEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton) {
          m_press = event->position().toPoint();
          m_is_pressed = true;
        }
        QWidget::mousePressEvent(event);
      }

      void mouseReleaseEvent(QMouseEvent* event) override {
        m_is_pressed = false;
        QWidget::mouseReleaseEvent(event);
      }

      void mouseMoveEvent(QMouseEvent* event) override {
        if(!m_is_pressed || !m_panel->get_editor() ||
            (event->position().toPoint() - m_press).manhattanLength() <
              QApplication::startDragDistance()) {
          QWidget::mouseMoveEvent(event);
          return;
        }
        m_is_pressed = false;
        auto drag = new QDrag(this);
        drag->setMimeData(make_component_mime_data(m_component));
        auto thumbnail = m_panel->get_thumbnail(m_component);
        if(!thumbnail.isNull()) {
          drag->setPixmap(thumbnail);
        }
        drag->exec(Qt::CopyAction);
      }

      void contextMenuEvent(QContextMenuEvent* event) override {
        auto menu = m_panel->make_component_menu(m_component, this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(event->globalPos());
      }

    private:
      AssetsPanel* m_panel;
      ComponentId m_component;
      QPoint m_press;
      bool m_is_pressed = false;
  };
}

class AssetsPanel::StyleEditor : public QFrame {
  public:
    using ChangeHandler = std::function<void (const CharacterStyle& style)>;
    using CloseHandler = std::function<void ()>;

    StyleEditor(CharacterStyle style, const QStringList& families,
        std::function<QStringList (const QString&)> styles,
        ChangeHandler change, CloseHandler close, QWidget* parent)
        : QFrame(parent, Qt::Popup),
          m_style(std::move(style)),
          m_styles(std::move(styles)),
          m_change(std::move(change)),
          m_close(std::move(close)),
          m_family(new QComboBox()),
          m_font_style(new QComboBox()),
          m_size(new QLineEdit()),
          m_letter_spacing(new QLineEdit()),
          m_line_height(new QLineEdit()),
          m_paragraph_spacing(new QLineEdit()),
          m_color(new QToolButton()),
          m_picker(new ColorPicker(this)) {
      setObjectName("character_style_editor");
      setAttribute(Qt::WA_DeleteOnClose);
      setAttribute(Qt::WA_StyledBackground);
      setStyleSheet("QFrame#character_style_editor { background: #FFFFFF; "
        "border: 1px solid #D0D0D0; }");
      setFixedWidth(260);
      auto layout = new QVBoxLayout(this);
      layout->setContentsMargins(16, 14, 16, 14);
      layout->setSpacing(10);
      m_family->setObjectName("underlined_choice");
      m_family->setEditable(true);
      m_family->addItems(families);
      m_family->setAccessibleName(translate("assets", "Font"));
      layout->addWidget(m_family);
      auto row = new QWidget();
      auto row_layout = new QHBoxLayout(row);
      row_layout->setContentsMargins(0, 0, 0, 0);
      row_layout->setSpacing(14);
      m_size->setObjectName("number_field");
      m_size->setAccessibleName(translate("assets", "Font Size"));
      m_size->setFixedWidth(48);
      row_layout->addWidget(m_size);
      m_font_style->setObjectName("underlined_choice");
      m_font_style->setEditable(true);
      m_font_style->setAccessibleName(translate("assets", "Font Style"));
      row_layout->addWidget(m_font_style, 1);
      layout->addWidget(row);
      auto spacing = new QWidget();
      auto spacing_layout = new QHBoxLayout(spacing);
      spacing_layout->setContentsMargins(0, 0, 0, 0);
      spacing_layout->setSpacing(10);
      for(auto [field, glyph, name] : {
          std::tuple(m_letter_spacing, Glyph::CHARACTER_SPACING,
            "Character Spacing"),
          std::tuple(m_line_height, Glyph::LINE_SPACING, "Line Spacing"),
          std::tuple(m_paragraph_spacing, Glyph::PARAGRAPH_SPACING,
            "Paragraph Spacing")}) {
        auto icon = new QLabel();
        icon->setPixmap(get_icon(glyph).pixmap(QSize(18, 18)));
        spacing_layout->addWidget(icon);
        field->setObjectName("number_field");
        field->setAccessibleName(translate("assets", name));
        spacing_layout->addWidget(field, 1);
      }
      layout->addWidget(spacing);
      for(auto field : {m_size, m_letter_spacing, m_line_height,
          m_paragraph_spacing}) {
        QObject::connect(field, &QLineEdit::editingFinished, this, [this] {
          commit();
        });
      }
      m_line_height->setPlaceholderText(translate("assets", "Auto"));
      auto toggles = new QWidget();
      auto toggles_layout = new QHBoxLayout(toggles);
      toggles_layout->setContentsMargins(0, 0, 0, 0);
      toggles_layout->setSpacing(2);
      for(auto [glyph, name, transform] : {
          std::tuple(Glyph::UPPERCASE, "Uppercase", TextTransform::UPPERCASE),
          std::tuple(Glyph::LOWERCASE, "Lowercase", TextTransform::LOWERCASE),
          std::tuple(
            Glyph::TITLE_CASE, "Title Case", TextTransform::TITLE_CASE)}) {
        auto button = make_toggle(glyph, name);
        toggles_layout->addWidget(button);
        m_transforms.emplace_back(transform, button);
        QObject::connect(button, &QToolButton::clicked, this,
          [this, transform] {
            auto& current = m_style.m_style.m_transform;
            set_transform(current == transform ? TextTransform::NONE :
              transform);
          });
      }
      toggles_layout->addSpacing(8);
      for(auto [glyph, name, script] : {
          std::tuple(
            Glyph::SUPERSCRIPT, "Superscript", TextScript::SUPERSCRIPT),
          std::tuple(Glyph::SUBSCRIPT, "Subscript", TextScript::SUBSCRIPT)}) {
        auto button = make_toggle(glyph, name);
        toggles_layout->addWidget(button);
        m_scripts.emplace_back(script, button);
        QObject::connect(button, &QToolButton::clicked, this, [this, script] {
          auto& current = m_style.m_style.m_script;
          set_script(current == script ? TextScript::NORMAL : script);
        });
      }
      toggles_layout->addSpacing(8);
      m_underline = make_toggle(Glyph::UNDERLINE, "Underline");
      m_strikethrough = make_toggle(Glyph::STRIKETHROUGH, "Strikethrough");
      for(auto button : {m_underline, m_strikethrough}) {
        toggles_layout->addWidget(button);
        QObject::connect(button, &QToolButton::clicked, this, [this] {
          commit();
        });
      }
      toggles_layout->addStretch(1);
      layout->addWidget(toggles);
      m_color->setObjectName("swatch");
      m_color->setAccessibleName(translate("assets", "Color"));
      m_color->setFocusPolicy(Qt::NoFocus);
      m_color->setAutoRaise(true);
      m_color->setIconSize(QSize(30, 18));
      m_color->setFixedSize(34, 22);
      layout->addWidget(m_color);
      m_picker->set_gradient_enabled(false);
      m_picker->set_change_handler([this] (const Fill& fill) {
        if(auto solid = std::get_if<SolidFill>(&fill)) {
          auto style = m_style;
          style.m_style.m_color = solid->m_color;
          apply(std::move(style));
        }
      });
      QObject::connect(m_color, &QToolButton::clicked, this, [this] {
        m_picker->set_fill(SolidFill(m_style.m_style.m_color));
        m_picker->popup(QRect(m_color->mapToGlobal(QPoint(0, 0)),
          m_color->size()));
      });
      QObject::connect(m_family, &QComboBox::activated, this, [this] {
        commit();
      });
      if(auto line = m_family->lineEdit()) {
        QObject::connect(line, &QLineEdit::editingFinished, this, [this] {
          commit();
        });
      }
      QObject::connect(m_font_style, &QComboBox::activated, this, [this] {
        commit();
      });
      if(auto line = m_font_style->lineEdit()) {
        QObject::connect(line, &QLineEdit::editingFinished, this, [this] {
          commit();
        });
      }
      load();
    }

    ColorPicker& get_color_picker() {
      return *m_picker;
    }

  protected:
    void hideEvent(QHideEvent* event) override {
      QFrame::hideEvent(event);
      if(m_close) {
        std::exchange(m_close, CloseHandler())();
      }
    }

    void keyPressEvent(QKeyEvent* event) override {
      if(event->key() == Qt::Key_Escape) {
        close();
        return;
      }
      QFrame::keyPressEvent(event);
    }

  private:
    CharacterStyle m_style;
    std::function<QStringList (const QString&)> m_styles;
    ChangeHandler m_change;
    CloseHandler m_close;
    QComboBox* m_family;
    QComboBox* m_font_style;
    QLineEdit* m_size;
    QLineEdit* m_letter_spacing;
    QLineEdit* m_line_height;
    QLineEdit* m_paragraph_spacing;
    std::vector<std::pair<TextTransform, QToolButton*>> m_transforms;
    std::vector<std::pair<TextScript, QToolButton*>> m_scripts;
    QToolButton* m_underline;
    QToolButton* m_strikethrough;
    QToolButton* m_color;
    ColorPicker* m_picker;

    QToolButton* make_toggle(Glyph glyph, const QString& name) {
      auto button = new QToolButton();
      button->setObjectName("icon_toggle");
      button->setAutoRaise(true);
      button->setFocusPolicy(Qt::NoFocus);
      button->setCheckable(true);
      button->setIcon(get_icon(glyph));
      button->setIconSize(QSize(18, 18));
      button->setFixedSize(26, 26);
      button->setToolTip(translate("assets", name));
      button->setAccessibleName(translate("assets", name));
      return button;
    }

    void load() {
      auto& text = m_style.m_style;
      auto family = QString::fromStdString(text.m_font.m_family);
      m_family->setEditText(family);
      m_font_style->clear();
      if(m_styles) {
        m_font_style->addItems(m_styles(family));
      }
      m_font_style->setEditText(QString::fromStdString(text.m_font.m_style));
      m_size->setText(format_number(text.m_font.m_size));
      m_letter_spacing->setText(format_number(text.m_letter_spacing));
      m_line_height->setText(m_style.m_line_height ?
        format_number(*m_style.m_line_height) : QString());
      m_paragraph_spacing->setText(format_number(m_style.m_paragraph_spacing));
      for(auto& [transform, button] : m_transforms) {
        button->setChecked(text.m_transform == transform);
      }
      for(auto& [script, button] : m_scripts) {
        button->setChecked(text.m_script == script);
      }
      m_underline->setChecked(text.m_is_underlined);
      m_strikethrough->setChecked(text.m_is_struck_through);
      m_color->setIcon(QIcon(make_color_icon(text.m_color)));
    }

    QPixmap make_color_icon(const Color& color) const {
      auto ratio = devicePixelRatioF();
      auto pixmap = QPixmap(static_cast<int>(30 * ratio),
        static_cast<int>(18 * ratio));
      pixmap.setDevicePixelRatio(ratio);
      pixmap.fill(Qt::transparent);
      auto painter = QPainter(&pixmap);
      auto bounds = QRectF(0.5, 0.5, 29, 17);
      painter.fillRect(bounds, to_qcolor(color));
      painter.setPen(QPen(Colors::BORDER, 1));
      painter.drawRect(bounds);
      return pixmap;
    }

    void set_transform(TextTransform transform) {
      auto style = m_style;
      style.m_style.m_transform = transform;
      apply(std::move(style));
    }

    void set_script(TextScript script) {
      auto style = m_style;
      style.m_style.m_script = script;
      apply(std::move(style));
    }

    void apply(CharacterStyle style) {
      if(!(style == m_style)) {
        m_style = std::move(style);
        if(m_change) {
          m_change(m_style);
        }
      }
      load();
    }

    void commit() {
      auto style = m_style;
      auto& text = style.m_style;
      auto family = m_family->currentText().trimmed().toStdString();
      if(!family.empty() && family != text.m_font.m_family) {
        text.m_font.m_family = family;
        text.m_font.m_postscript_name.clear();
      }
      auto font_style = m_font_style->currentText().trimmed().toStdString();
      if(!font_style.empty() && font_style != text.m_font.m_style) {
        text.m_font.m_style = font_style;
        text.m_font.m_postscript_name.clear();
      }
      if(auto size = parse_number(m_size->text()); size && *size > 0) {
        text.m_font.m_size = *size;
      }
      if(auto spacing = parse_number(m_letter_spacing->text())) {
        text.m_letter_spacing = *spacing;
      }
      if(m_line_height->text().trimmed().isEmpty()) {
        style.m_line_height = std::nullopt;
      } else if(auto height = parse_number(m_line_height->text());
          height && *height > 0) {
        style.m_line_height = *height;
      }
      if(auto spacing = parse_number(m_paragraph_spacing->text());
          spacing && *spacing >= 0) {
        style.m_paragraph_spacing = *spacing;
      }
      text.m_is_underlined = m_underline->isChecked();
      text.m_is_struck_through = m_strikethrough->isChecked();
      apply(std::move(style));
    }
};

AssetsPanel::AssetsPanel(QWidget* parent)
    : QWidget(parent),
      m_document(nullptr),
      m_editor(nullptr),
      m_entries(4),
      m_group_names(4),
      m_picker(new ColorPicker(this)),
      m_content(new QWidget()),
      m_content_layout(new QVBoxLayout(m_content)),
      m_is_empty_shown(false) {
  setObjectName("assets_panel");
  m_picker->set_close_handler([this] {
    if(!m_edited_swatch.is_nil() && m_editor) {
      m_editor->get_history().seal();
    }
    m_edited_swatch = SwatchId();
  });
  m_picker->set_change_handler([this] (const Fill& fill) {
    if(m_edited_swatch.is_nil() || !m_document) {
      return;
    }
    auto& swatches = m_document->get_resources().m_swatches;
    auto current = std::find_if(swatches.begin(), swatches.end(),
      [&] (const auto& swatch) {
        return swatch.m_id == m_edited_swatch;
      });
    if(current == swatches.end()) {
      return;
    }
    auto swatch = *current;
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      swatch.m_color = solid->m_color;
      swatch.m_gradient.clear();
    } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
      swatch.m_gradient = gradient->m_stops;
      if(!gradient->m_stops.empty()) {
        swatch.m_color = gradient->m_stops.front().m_color;
      }
    }
    replace_swatch(swatch);
  });
  m_thumbnail_timer.setSingleShot(true);
  m_thumbnail_timer.setInterval(0);
  QObject::connect(&m_thumbnail_timer, &QTimer::timeout, this, [this] {
    render_thumbnails();
  });
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
  auto title = new QLabel(translate("assets", "DOCUMENT ASSETS"));
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
  field->setPlaceholderText(translate("assets", "Search document assets"));
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
  m_editor = nullptr;
  attach(document);
  rebuild();
}

Editor* AssetsPanel::get_editor() const {
  return m_editor;
}

void AssetsPanel::set_editor(Editor* editor) {
  m_editor = editor;
  attach(editor ? &editor->get_document() : nullptr);
  rebuild();
}

void AssetsPanel::set_thumbnailer(ComponentThumbnailer thumbnailer) {
  m_thumbnailer = std::move(thumbnailer);
  m_thumbnails.clear();
  rebuild();
}

void AssetsPanel::set_main_component_handler(
    std::function<void (const ComponentId& component)> handler) {
  m_main_component_handler = std::move(handler);
}

std::vector<QString> AssetsPanel::get_entries(AssetSection section) const {
  return m_entries[static_cast<int>(section)];
}

std::vector<ComponentId> AssetsPanel::get_components() const {
  return m_components;
}

QPixmap AssetsPanel::get_thumbnail(const ComponentId& component) const {
  if(auto entry = m_thumbnails.find(component); entry != m_thumbnails.end()) {
    return entry->second;
  }
  return QPixmap();
}

void AssetsPanel::render_thumbnails() {
  auto pending =
    std::exchange(m_pending, std::unordered_set<ComponentId>());
  if(!m_document || !m_thumbnailer) {
    return;
  }
  auto& components = m_document->get_resources().m_components;
  for(auto& id : pending) {
    if(!components.contains(id)) {
      continue;
    }
    auto image = QImage();
    try {
      image = m_thumbnailer(*m_document, id,
        static_cast<int>(THUMBNAIL_SIZE * THUMBNAIL_RATIO));
    } catch(const std::exception&) {}
    auto thumbnail = image.isNull() ? make_component_thumbnail() :
      make_component_thumbnail(image);
    m_thumbnails.insert_or_assign(id, thumbnail);
    if(auto label = m_thumbnail_labels.find(id);
        label != m_thumbnail_labels.end() && label->second) {
      label->second->setPixmap(thumbnail);
    }
  }
}

QMenu* AssetsPanel::make_component_menu(
    const ComponentId& component, QWidget* parent) {
  auto menu = new QMenu(parent);
  auto linked = m_document && is_linked(*m_document, component);
  auto edit = menu->addAction(translate("assets", "Edit Main Component"));
  edit->setEnabled(static_cast<bool>(m_main_component_handler) && !linked);
  QObject::connect(edit, &QAction::triggered, this, [this, component] {
    if(m_main_component_handler) {
      m_main_component_handler(component);
    }
  });
  auto rename = menu->addAction(translate("assets", "Rename"));
  rename->setEnabled(m_editor && !linked);
  QObject::connect(rename, &QAction::triggered, this, [this, component] {
    auto row = m_component_rows.find(component);
    if(row == m_component_rows.end() || !row->second) {
      return;
    }
    auto label = row->second->findChild<QLabel*>("asset_label");
    if(!label) {
      return;
    }
    auto field = new QLineEdit(label->text(), row->second);
    field->setObjectName("asset_rename");
    field->setGeometry(label->geometry());
    field->selectAll();
    field->show();
    field->setFocus();
    QObject::connect(field, &QLineEdit::editingFinished, this,
      [this, component, field = QPointer<QLineEdit>(field)] {
        if(!field) {
          return;
        }
        auto text = field->text();
        field->deleteLater();
        rename_component(component, text);
      });
  });
  return menu;
}

bool AssetsPanel::rename_component(
    const ComponentId& component, const QString& name) {
  if(!m_editor || name.trimmed().isEmpty() ||
      is_linked(m_editor->get_document(), component)) {
    return false;
  }
  try {
    return m_editor->execute(std::make_unique<RenameComponentCommand>(
      component, name.trimmed().toStdString())) != nullptr;
  } catch(const std::exception&) {
    return false;
  }
}

void AssetsPanel::reveal(const ComponentId& component) {
  for(auto& row : m_component_rows) {
    if(row.second) {
      row.second->setStyleSheet(row.first == component ? HIGHLIGHT : "");
    }
  }
  auto row = m_component_rows.find(component);
  if(row == m_component_rows.end() || !row->second) {
    return;
  }
  if(auto scroll = findChild<QScrollArea*>("assets_scroll")) {
    scroll->ensureWidgetVisible(row->second);
  }
}

bool AssetsPanel::is_empty_shown() const {
  return m_is_empty_shown;
}

void AssetsPanel::attach(Document* document) {
  m_subscription.release();
  m_document = document;
  m_thumbnails.clear();
  m_pending.clear();
  if(m_document) {
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      invalidate_thumbnails(changes);
      if(!changes.m_components.empty() || !changes.m_swatches.empty() ||
          !changes.m_character_styles.empty() ||
          changes.m_is_asset_group_changed) {
        rebuild();
      }
    });
  }
}

void AssetsPanel::rebuild() {
  for(auto& entries : m_entries) {
    entries.clear();
  }
  for(auto& names : m_group_names) {
    names.clear();
  }
  m_components.clear();
  m_swatches.clear();
  m_character_styles.clear();
  m_asset_rows.clear();
  m_group_rows.clear();
  m_thumbnail_labels.clear();
  m_component_rows.clear();
  while(auto item = m_content_layout->takeAt(0)) {
    if(auto widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  if(m_document) {
    auto& resources = m_document->get_resources();
    std::erase_if(m_selected, [&] (const auto& id) {
      return find_section(resources, id).empty();
    });
    for(auto& component : resources.m_components) {
      m_entries[static_cast<int>(AssetSection::COMPONENTS)].push_back(
        QString::fromStdString(component.second.m_name));
      m_components.push_back(component.first);
    }
  }
  add_section(AssetSection::COLORS, "Colors");
  add_section(AssetSection::CHARACTER_STYLES, "Character Styles");
  add_section(AssetSection::COMPONENTS, "Components");
  add_section(AssetSection::VIDEOS, "Videos");
  update_selection();
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
    auto message = new QLabel(translate("assets",
      "Add your styles and components, or use elements from existing "
      "libraries."));
    message->setObjectName("empty_message");
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto link = new QLabel(
      QString("<u>%1</u>").arg(translate("assets", "View libraries")));
    link->setObjectName("empty_link");
    link->setAlignment(Qt::AlignCenter);
    link->setEnabled(false);
    layout->addWidget(link);
    m_content_layout->addWidget(invitation);
  }
  m_content_layout->addStretch(1);
  if(!m_pending.empty()) {
    m_thumbnail_timer.start();
  }
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
  toggle->setText(translate("assets", title));
  header_layout->addWidget(toggle, 1, Qt::AlignLeft);
  auto add = make_tool_button(Glyph::PLUS, "Add");
  add->setEnabled(false);
  if(m_editor && section == AssetSection::COLORS) {
    add->setToolTip(translate("assets", "Add Color"));
    add->setAccessibleName(add->toolTip());
    add->setEnabled(true);
    QObject::connect(add, &QToolButton::clicked, this, [this] {
      add_colors();
    });
  } else if(m_editor && section == AssetSection::CHARACTER_STYLES) {
    add->setToolTip(translate("assets", "Add Character Style"));
    add->setAccessibleName(add->toolTip());
    add->setEnabled(true);
    QObject::connect(add, &QToolButton::clicked, this, [this] {
      add_character_styles();
    });
  }
  header_layout->addWidget(add);
  m_content_layout->addWidget(header);
  auto rows = new QWidget();
  auto rows_layout = new QVBoxLayout(rows);
  rows_layout->setContentsMargins(0, 0, 0, 0);
  rows_layout->setSpacing(0);
  if(m_document) {
    auto& resources = m_document->get_resources();
    if(section == AssetSection::COLORS ||
        section == AssetSection::CHARACTER_STYLES) {
      add_entries(section, get_section_id(section), *rows_layout, 0);
    } else if(section == AssetSection::COMPONENTS) {
      for(auto& component : resources.m_components) {
        rows_layout->addWidget(make_component_row(component.second));
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

QWidget* AssetsPanel::make_component_row(const Component& component) {
  auto row = new ComponentRow(*this, component.m_id);
  auto thumbnail = get_thumbnail(component.m_id);
  if(thumbnail.isNull()) {
    thumbnail = make_component_thumbnail();
    if(m_thumbnailer) {
      m_pending.insert(component.m_id);
    }
  }
  fill_row(*row, thumbnail, QString(),
    QString::fromStdString(component.m_name));
  row->setToolTip(QString::fromStdString(component.m_name));
  m_thumbnail_labels.emplace(component.m_id,
    row->findChild<QLabel*>("asset_thumbnail"));
  m_component_rows.emplace(component.m_id, row);
  return row;
}

void AssetsPanel::invalidate_thumbnails(const ChangeSet& changes) {
  if(!m_thumbnailer || m_thumbnails.empty()) {
    return;
  }
  auto masters = std::unordered_map<NodeId, ComponentId>();
  for(auto& component : m_document->get_resources().m_components) {
    masters.emplace(component.second.m_master, component.first);
  }
  auto ids = std::vector<NodeId>();
  for(auto& change : changes.m_modified) {
    ids.push_back(change.m_id);
  }
  ids.insert(ids.end(), changes.m_inserted.begin(), changes.m_inserted.end());
  ids.insert(ids.end(), changes.m_moved.begin(), changes.m_moved.end());
  for(auto& id : changes.m_components) {
    if(masters.contains(id) || m_document->get_resources().m_components.
        contains(id)) {
      m_thumbnails.erase(id);
      m_pending.insert(id);
    }
  }
  if(ids.size() > MAXIMUM_CHECKED_CHANGES) {
    for(auto& entry : masters) {
      m_thumbnails.erase(entry.second);
      m_pending.insert(entry.second);
    }
  } else {
    for(auto& id : ids) {
      auto current = id;
      for(auto depth = 0; depth != MAXIMUM_DEPTH; ++depth) {
        auto node = m_document->find(current);
        if(!node) {
          break;
        }
        if(node->m_parent.is_nil()) {
          if(auto master = masters.find(current); master != masters.end()) {
            m_thumbnails.erase(master->second);
            m_pending.insert(master->second);
          }
          break;
        }
        current = node->m_parent;
      }
    }
  }
  if(!m_pending.empty()) {
    m_thumbnail_timer.start();
  }
}

std::vector<SwatchId> AssetsPanel::get_swatches() const {
  return m_swatches;
}

std::vector<CharacterStyleId> AssetsPanel::get_character_styles() const {
  return m_character_styles;
}

void AssetsPanel::set_font_families(
    std::function<QStringList ()> families) {
  m_font_families = std::move(families);
}

ColorPicker& AssetsPanel::get_color_picker() {
  return *m_picker;
}

bool AssetsPanel::add_colors() {
  if(!m_editor) {
    return false;
  }
  return run(make_add_colors_command(
    m_editor->get_document(), m_editor->get_selection().get_nodes()));
}

bool AssetsPanel::add_character_styles() {
  if(!m_editor) {
    return false;
  }
  return run(make_add_character_styles_command(
    m_editor->get_document(), m_editor->get_selection().get_nodes()));
}

bool AssetsPanel::apply_swatch(const SwatchId& swatch, bool is_border) {
  if(!m_editor) {
    return false;
  }
  auto& document = m_editor->get_document();
  auto& swatches = document.get_resources().m_swatches;
  auto entry = std::find_if(swatches.begin(), swatches.end(),
    [&] (const auto& candidate) {
      return candidate.m_id == swatch;
    });
  auto& nodes = m_editor->get_selection().get_nodes();
  if(entry == swatches.end() || nodes.empty()) {
    return false;
  }
  if(is_border) {
    auto edit = StrokeEdit();
    edit.m_color = entry->m_gradient.empty() ? entry->m_color :
      entry->m_gradient.front().m_color;
    edit.m_is_enabled = true;
    return run(std::make_unique<SetStrokeCommand>(nodes, std::move(edit)));
  }
  return run(std::make_unique<SetFillCommand>(nodes, make_swatch_fill(*entry)));
}

bool AssetsPanel::apply_character_style(const CharacterStyleId& style) {
  if(!m_editor) {
    return false;
  }
  auto& nodes = m_editor->get_selection().get_nodes();
  if(nodes.empty()) {
    return false;
  }
  return run(std::make_unique<ApplyCharacterStyleCommand>(nodes, style));
}

bool AssetsPanel::replace_swatch(const ColorSwatch& swatch) {
  if(!m_editor) {
    return false;
  }
  auto is_merged = swatch.m_id == m_edited_swatch;
  return run(std::make_unique<ReplaceSwatchCommand>(swatch), is_merged);
}

bool AssetsPanel::replace_character_style(const CharacterStyle& style) {
  if(!m_editor) {
    return false;
  }
  return run(std::make_unique<ReplaceCharacterStyleCommand>(style),
    static_cast<bool>(m_style_editor));
}

bool AssetsPanel::rename_swatch(const SwatchId& swatch, const QString& name) {
  return run(std::make_unique<RenameSwatchCommand>(
    swatch, name.trimmed().toStdString()));
}

bool AssetsPanel::rename_character_style(
    const CharacterStyleId& style, const QString& name) {
  if(name.trimmed().isEmpty()) {
    return false;
  }
  return run(std::make_unique<RenameCharacterStyleCommand>(
    style, name.trimmed().toStdString()));
}
bool AssetsPanel::delete_swatch(const SwatchId& swatch) {
  return run(std::make_unique<RemoveSwatchCommand>(swatch));
}

bool AssetsPanel::delete_character_style(const CharacterStyleId& style) {
  return run(std::make_unique<RemoveCharacterStyleCommand>(style));
}

void AssetsPanel::edit_swatch(const SwatchId& swatch) {
  if(!m_editor || !m_document) {
    return;
  }
  auto& swatches = m_document->get_resources().m_swatches;
  auto entry = std::find_if(swatches.begin(), swatches.end(),
    [&] (const auto& candidate) {
      return candidate.m_id == swatch;
    });
  if(entry == swatches.end()) {
    return;
  }
  if(m_picker->isVisible()) {
    m_picker->hide();
  }
  m_editor->get_history().seal();
  m_picker->set_gradient_enabled(!entry->m_gradient.empty());
  m_picker->set_fill(make_swatch_fill(*entry));
  m_picker->set_swatches({});
  m_edited_swatch = swatch;
  auto row = m_asset_rows.find(swatch);
  auto anchor = QRect(mapToGlobal(QPoint(width(), 0)), QSize(1, height()));
  if(row != m_asset_rows.end() && row->second) {
    auto origin = row->second->mapToGlobal(QPoint(0, 0));
    anchor = QRect(QPoint(mapToGlobal(QPoint(width(), 0)).x() +
      m_picker->width() + 16, origin.y()), QSize(1, row->second->height()));
  }
  m_picker->popup(anchor);
}

void AssetsPanel::edit_character_style(const CharacterStyleId& style) {
  if(!m_editor || !m_document) {
    return;
  }
  auto& styles = m_document->get_resources().m_character_styles;
  auto entry = std::find_if(styles.begin(), styles.end(),
    [&] (const auto& candidate) {
      return candidate.m_id == style;
    });
  if(entry == styles.end()) {
    return;
  }
  if(m_style_editor) {
    m_style_editor->close();
  }
  m_editor->get_history().seal();
  auto families = m_font_families ? m_font_families() : QStringList();
  auto editor = new StyleEditor(*entry, families, m_font_styles,
    [this] (const CharacterStyle& style) {
      replace_character_style(style);
    }, [this] {
      if(m_editor) {
        m_editor->get_history().seal();
      }
    }, this);
  m_style_editor = editor;
  editor->adjustSize();
  auto position = mapToGlobal(QPoint(width() + 8, 0));
  if(auto row = m_asset_rows.find(style);
      row != m_asset_rows.end() && row->second) {
    position.setY(row->second->mapToGlobal(QPoint(0, 0)).y());
  }
  if(auto screen = QGuiApplication::screenAt(position)) {
    auto available = screen->availableGeometry();
    position.setY(std::clamp(position.y(), available.top(),
      std::max(available.bottom() - editor->height(), available.top())));
  }
  editor->move(position);
  editor->show();
}

QMenu* AssetsPanel::make_swatch_menu(const SwatchId& swatch, QWidget* parent) {
  auto menu = new QMenu(parent);
  auto color = QString();
  if(m_document) {
    for(auto& entry : m_document->get_resources().m_swatches) {
      if(entry.m_id == swatch) {
        color = to_hex(entry.m_gradient.empty() ? entry.m_color :
          entry.m_gradient.front().m_color);
      }
    }
  }
  auto copy = menu->addAction(translate("assets", "Copy %1").arg(color));
  QObject::connect(copy, &QAction::triggered, this, [color] {
    if(auto clipboard = QGuiApplication::clipboard()) {
      clipboard->setText(color);
    }
  });
  auto edit = menu->addAction(translate("assets", "Edit"));
  edit->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(edit, &QAction::triggered, this, [this, swatch] {
    edit_swatch(swatch);
  });
  menu->addSeparator();
  auto highlight = menu->addAction(translate("assets", "Highlight on canvas"));
  highlight->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(highlight, &QAction::triggered, this, [this, swatch] {
    highlight_instances(swatch);
  });
  menu->addSeparator();
  auto group = menu->addAction(translate("assets", "New group from element"));
  group->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(group, &QAction::triggered, this, [this, swatch] {
    group_assets({swatch});
  });
  menu->addSeparator();
  add_move_menu(*menu, "Move to...", {swatch});
  menu->addSeparator();
  auto rename = menu->addAction(translate("assets", "Rename"));
  rename->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(rename, &QAction::triggered, this, [this, swatch] {
    start_rename(swatch, [this, swatch] (const QString& name) {
      rename_swatch(swatch, name);
    });
  });
  auto remove = menu->addAction(translate("assets", "Delete"));
  remove->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(remove, &QAction::triggered, this, [this, swatch] {
    delete_swatch(swatch);
  });
  return menu;
}

QMenu* AssetsPanel::make_character_style_menu(
    const CharacterStyleId& style, QWidget* parent) {
  auto menu = new QMenu(parent);
  auto remove_font = menu->addAction(translate("assets", "Remove font"));
  remove_font->setEnabled(false);
  menu->addSeparator();
  auto edit = menu->addAction(translate("assets", "Edit"));
  edit->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(edit, &QAction::triggered, this, [this, style] {
    edit_character_style(style);
  });
  menu->addSeparator();
  auto highlight = menu->addAction(translate("assets", "Highlight on canvas"));
  highlight->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(highlight, &QAction::triggered, this, [this, style] {
    highlight_instances(style);
  });
  menu->addSeparator();
  auto group = menu->addAction(translate("assets", "New group from element"));
  group->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(group, &QAction::triggered, this, [this, style] {
    group_assets({style});
  });
  menu->addSeparator();
  add_move_menu(*menu, "Move to...", {style});
  menu->addSeparator();
  auto rename = menu->addAction(translate("assets", "Rename"));
  rename->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(rename, &QAction::triggered, this, [this, style] {
    start_rename(style, [this, style] (const QString& name) {
      rename_character_style(style, name);
    });
  });
  auto remove = menu->addAction(translate("assets", "Delete"));
  remove->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(remove, &QAction::triggered, this, [this, style] {
    delete_character_style(style);
  });
  return menu;
}

void AssetsPanel::reveal_swatch(const SwatchId& swatch) {
  highlight(swatch);
}

void AssetsPanel::reveal_character_style(const CharacterStyleId& style) {
  highlight(style);
}

QWidget* AssetsPanel::make_swatch_row(const ColorSwatch& swatch) {
  auto row = new AssetRow();
  fill_row(*row, make_swatch_thumbnail(swatch), QString(),
    get_swatch_label(swatch));
  row->setToolTip(to_hex(swatch.m_color));
  auto id = swatch.m_id;
  row->m_click = [this, id] (Qt::KeyboardModifiers modifiers) {
    click_asset(id, modifiers);
  };
  row->m_double_click = [this, id] {
    start_rename(id, [this, id] (const QString& name) {
      rename_swatch(id, name);
    });
  };
  row->m_menu = [this, id] (QWidget* parent) {
    return make_asset_menu(id, parent);
  };
  m_asset_rows.emplace(id, row);
  return row;
}

QWidget* AssetsPanel::make_character_style_row(const CharacterStyle& style) {
  auto row = new AssetRow();
  fill_row(*row, QPixmap(), "Ag", get_character_style_label(style));
  if(auto preview = row->findChild<QLabel*>("character_style_preview")) {
    auto font = preview->font();
    font.setFamily(QString::fromStdString(style.m_style.m_font.m_family));
    preview->setFont(font);
  }
  if(!style.m_name.empty()) {
    row->setToolTip(QString::fromStdString(style.m_name));
  }
  auto id = style.m_id;
  row->m_click = [this, id] (Qt::KeyboardModifiers modifiers) {
    click_asset(id, modifiers);
  };
  row->m_double_click = [this, id] {
    start_rename(id, [this, id] (const QString& name) {
      rename_character_style(id, name);
    });
  };
  row->m_menu = [this, id] (QWidget* parent) {
    return make_asset_menu(id, parent);
  };
  m_asset_rows.emplace(id, row);
  return row;
}

void AssetsPanel::start_rename(
    const NodeId& id, std::function<void (QString)> done) {
  auto row = m_asset_rows.find(id);
  if(row == m_asset_rows.end() || !row->second) {
    return;
  }
  auto label = row->second->findChild<QLabel*>("asset_label");
  if(!label) {
    return;
  }
  auto field = new QLineEdit(label->text(), row->second);
  field->setObjectName("asset_rename");
  field->setGeometry(label->geometry());
  field->selectAll();
  field->show();
  field->setFocus();
  QObject::connect(field, &QLineEdit::editingFinished, this,
    [field = QPointer<QLineEdit>(field), done = std::move(done)] {
      if(!field) {
        return;
      }
      auto text = field->text();
      field->deleteLater();
      done(text);
    });
}

void AssetsPanel::highlight(const NodeId& id) {
  m_selected = {id};
  update_selection();
  for(auto& row : m_component_rows) {
    if(row.second) {
      row.second->setStyleSheet(row.first == id ? HIGHLIGHT : "");
    }
  }
  auto row = m_asset_rows.find(id);
  if(row == m_asset_rows.end() || !row->second) {
    return;
  }
  if(auto scroll = findChild<QScrollArea*>("assets_scroll")) {
    scroll->ensureWidgetVisible(row->second);
  }
}

void AssetsPanel::set_font_styles(
    std::function<QStringList (const QString& family)> styles) {
  m_font_styles = std::move(styles);
}

void AssetsPanel::set_highlight_handler(
    std::function<void (const std::vector<NodeId>& nodes)> handler) {
  m_highlight_handler = std::move(handler);
}

void AssetsPanel::set_message_handler(
    std::function<void (const QString& message)> handler) {
  m_message_handler = std::move(handler);
}

const std::vector<NodeId>& AssetsPanel::get_selected_assets() const {
  return m_selected;
}

void AssetsPanel::set_selected_assets(std::vector<NodeId> assets) {
  m_selected = std::move(assets);
  update_selection();
}

bool AssetsPanel::highlight_instances(const NodeId& asset) {
  if(!m_editor) {
    return false;
  }
  auto& document = m_editor->get_document();
  auto& resources = document.get_resources();
  auto instances = std::vector<NodeId>();
  auto message = QString();
  for(auto& swatch : resources.m_swatches) {
    if(swatch.m_id == asset) {
      instances = find_swatch_instances(document, swatch);
      message = translate("assets", "0 objects use this color");
    }
  }
  for(auto& style : resources.m_character_styles) {
    if(style.m_id == asset) {
      instances = find_character_style_instances(document, style);
      message = translate("assets", "0 objects use this character style");
    }
  }
  if(instances.empty()) {
    if(m_highlight_handler) {
      m_highlight_handler({});
    }
    if(m_message_handler && !message.isEmpty()) {
      m_message_handler(message);
    }
    return false;
  }
  m_editor->get_selection().clear();
  if(m_highlight_handler) {
    m_highlight_handler(instances);
  }
  return true;
}

std::string AssetsPanel::group_assets(const std::vector<NodeId>& assets) {
  if(!m_editor || assets.empty()) {
    return std::string();
  }
  auto ordered = std::vector<NodeId>();
  for(auto& id : m_swatches) {
    if(std::find(assets.begin(), assets.end(), id) != assets.end()) {
      ordered.push_back(id);
    }
  }
  for(auto& id : m_character_styles) {
    if(std::find(assets.begin(), assets.end(), id) != assets.end()) {
      ordered.push_back(id);
    }
  }
  if(ordered.size() != assets.size()) {
    ordered = assets;
  }
  auto command = std::make_unique<GroupAssetsCommand>(ordered);
  auto group = command->get_group();
  if(!run(std::move(command))) {
    return std::string();
  }
  start_group_rename(group);
  return group;
}

bool AssetsPanel::move_assets(
    const std::vector<NodeId>& assets, const std::string& group) {
  return run(std::make_unique<MoveAssetsCommand>(assets, group));
}

bool AssetsPanel::rename_group(const std::string& group, const QString& name) {
  if(name.trimmed().isEmpty()) {
    return false;
  }
  return run(std::make_unique<RenameAssetGroupCommand>(
    group, name.trimmed().toStdString()));
}

bool AssetsPanel::delete_assets(const std::vector<NodeId>& assets) {
  if(!m_document || assets.empty()) {
    return false;
  }
  auto& resources = m_document->get_resources();
  auto commands = std::vector<std::unique_ptr<Command>>();
  auto colors = 0;
  auto styles = 0;
  for(auto& asset : assets) {
    auto section = find_section(resources, asset);
    if(section == COLOR_SECTION) {
      commands.push_back(std::make_unique<RemoveSwatchCommand>(asset));
      ++colors;
    } else if(section == CHARACTER_STYLE_SECTION) {
      commands.push_back(
        std::make_unique<RemoveCharacterStyleCommand>(asset));
      ++styles;
    }
  }
  if(commands.empty()) {
    return false;
  } else if(commands.size() == 1) {
    return run(std::move(commands.front()));
  }
  auto label = std::string("Delete Mixed Assets");
  if(styles == 0) {
    label = "Delete Colors";
  } else if(colors == 0) {
    label = "Delete Character Styles";
  }
  return run(std::make_unique<CompositeCommand>(label, std::move(commands)));
}

std::vector<QString> AssetsPanel::get_group_names(AssetSection section) const {
  return m_group_names[static_cast<int>(section)];
}

QMenu* AssetsPanel::make_group_menu(
    const std::string& group, QWidget* parent) {
  auto menu = new QMenu(parent);
  auto subgroup = menu->addAction(translate("assets", "Create subgroup"));
  subgroup->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(subgroup, &QAction::triggered, this, [this, group] {
    auto command = std::make_unique<AddAssetGroupCommand>(group);
    auto id = command->get_group();
    if(run(std::move(command))) {
      start_group_rename(id);
    }
  });
  menu->addSeparator();
  auto rename = menu->addAction(translate("assets", "Rename group"));
  rename->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(rename, &QAction::triggered, this, [this, group] {
    start_group_rename(group);
  });
  auto ungroup = menu->addAction(translate("assets", "Ungroup"));
  ungroup->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(ungroup, &QAction::triggered, this, [this, group] {
    run(std::make_unique<RemoveAssetGroupCommand>(group, false));
  });
  auto remove = menu->addAction(translate("assets", "Delete"));
  remove->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(remove, &QAction::triggered, this, [this, group] {
    run(std::make_unique<RemoveAssetGroupCommand>(group, true));
  });
  return menu;
}

QMenu* AssetsPanel::make_selection_menu(QWidget* parent) {
  auto menu = new QMenu(parent);
  auto selected = m_selected;
  auto is_one_section = !selected.empty() && m_document &&
    std::all_of(selected.begin(), selected.end(), [&] (const auto& id) {
      return find_section(m_document->get_resources(), id) ==
        find_section(m_document->get_resources(), selected.front());
    });
  auto group = menu->addAction(translate("assets", "New group from selection"));
  group->setEnabled(m_editor && is_one_section);
  QObject::connect(group, &QAction::triggered, this, [this, selected] {
    group_assets(selected);
  });
  menu->addSeparator();
  if(is_one_section) {
    add_move_menu(*menu, "Move selected items to", selected);
  } else {
    menu->addAction(translate("assets", "Move selected items to"))->
      setEnabled(false);
  }
  menu->addSeparator();
  auto remove = menu->addAction(translate("assets", "Delete"));
  remove->setEnabled(static_cast<bool>(m_editor));
  QObject::connect(remove, &QAction::triggered, this, [this, selected] {
    delete_assets(selected);
  });
  return menu;
}

void AssetsPanel::add_entries(AssetSection section, const std::string& group,
    QVBoxLayout& layout, int depth) {
  auto& resources = m_document->get_resources();
  auto index = static_cast<int>(section);
  for(auto& entry : list_assets(resources, group)) {
    if(!entry.m_group.empty()) {
      auto found = std::find_if(resources.m_asset_groups.begin(),
        resources.m_asset_groups.end(), [&] (const auto& candidate) {
          return candidate.m_id == entry.m_group;
        });
      m_group_names[index].push_back(QString::fromStdString(found->m_name));
      auto members = new QWidget();
      auto members_layout = new QVBoxLayout(members);
      members_layout->setContentsMargins(0, 0, 0, 0);
      members_layout->setSpacing(0);
      layout.addWidget(make_group_row(*found, *members, depth));
      add_entries(section, entry.m_group, *members_layout, depth + 1);
      layout.addWidget(members);
      continue;
    }
    auto row = static_cast<QWidget*>(nullptr);
    if(section == AssetSection::COLORS) {
      for(auto& swatch : resources.m_swatches) {
        if(swatch.m_id == entry.m_asset) {
          m_entries[index].push_back(get_swatch_label(swatch));
          m_swatches.push_back(swatch.m_id);
          row = make_swatch_row(swatch);
        }
      }
    } else {
      for(auto& style : resources.m_character_styles) {
        if(style.m_id == entry.m_asset) {
          m_entries[index].push_back(get_character_style_label(style));
          m_character_styles.push_back(style.m_id);
          row = make_character_style_row(style);
        }
      }
    }
    if(row) {
      row->layout()->setContentsMargins(16 + GROUP_INDENT * depth, 0, 16, 0);
      layout.addWidget(row);
    }
  }
}

QWidget* AssetsPanel::make_group_row(
    const AssetGroup& group, QWidget& members, int depth) {
  auto row = new GroupRow();
  row->setObjectName("asset_group_row");
  row->setFixedHeight(ROW_HEIGHT);
  auto layout = new QHBoxLayout(row);
  layout->setContentsMargins(10 + GROUP_INDENT * depth, 0, 16, 0);
  layout->setSpacing(4);
  auto toggle = new QToolButton();
  toggle->setObjectName("section_toggle");
  toggle->setAutoRaise(true);
  toggle->setCheckable(true);
  toggle->setChecked(true);
  toggle->setFocusPolicy(Qt::NoFocus);
  toggle->setIcon(get_icon(Glyph::CHEVRON_DOWN));
  toggle->setIconSize(QSize(14, 14));
  toggle->setAccessibleName(QString::fromStdString(group.m_name));
  layout->addWidget(toggle);
  auto label = new QLabel(QString::fromStdString(group.m_name));
  label->setObjectName("asset_label");
  layout->addWidget(label, 1);
  QObject::connect(toggle, &QToolButton::toggled, &members,
    [toggle, members = &members] (bool is_open) {
      members->setVisible(is_open);
      toggle->setIcon(
        get_icon(is_open ? Glyph::CHEVRON_DOWN : Glyph::CHEVRON_RIGHT));
    });
  auto id = group.m_id;
  row->m_double_click = [this, id] {
    start_group_rename(id);
  };
  row->m_menu = [this, id] (QWidget* parent) {
    return make_group_menu(id, parent);
  };
  m_group_rows.emplace(id, row);
  return row;
}

void AssetsPanel::click_asset(
    const NodeId& id, Qt::KeyboardModifiers modifiers) {
  if(modifiers & Qt::ControlModifier) {
    if(auto i = std::find(m_selected.begin(), m_selected.end(), id);
        i != m_selected.end()) {
      m_selected.erase(i);
    } else {
      m_selected.push_back(id);
    }
    update_selection();
    return;
  } else if(modifiers & Qt::ShiftModifier && !m_selected.empty()) {
    auto& order = m_document && find_section(m_document->get_resources(),
      id) == COLOR_SECTION ? m_swatches : m_character_styles;
    auto anchor = std::find(order.begin(), order.end(), m_selected.front());
    auto target = std::find(order.begin(), order.end(), id);
    if(anchor != order.end() && target != order.end()) {
      auto first = std::min(anchor, target);
      auto last = std::max(anchor, target);
      auto range = std::vector<NodeId>(first, last + 1);
      if(target < anchor) {
        std::reverse(range.begin(), range.end());
      }
      m_selected = std::move(range);
      update_selection();
      return;
    }
  }
  m_selected = {id};
  update_selection();
  if(m_document && find_section(m_document->get_resources(), id) ==
      COLOR_SECTION) {
    apply_swatch(id, static_cast<bool>(modifiers & Qt::AltModifier));
  } else {
    apply_character_style(id);
  }
}

QMenu* AssetsPanel::make_asset_menu(const NodeId& id, QWidget* parent) {
  if(m_selected.size() > 1 &&
      std::find(m_selected.begin(), m_selected.end(), id) !=
        m_selected.end()) {
    return make_selection_menu(parent);
  }
  m_selected = {id};
  update_selection();
  if(m_document && find_section(m_document->get_resources(), id) ==
      COLOR_SECTION) {
    return make_swatch_menu(id, parent);
  }
  return make_character_style_menu(id, parent);
}

void AssetsPanel::add_move_menu(QMenu& menu, const QString& title,
    const std::vector<NodeId>& assets) {
  auto action = menu.addAction(translate("assets", title));
  if(!m_editor || !m_document || assets.empty()) {
    action->setEnabled(false);
    return;
  }
  QObject::connect(action, &QAction::triggered, this, [this, assets] {
    if(!m_editor) {
      return;
    }
    auto dialog = new AssetMoveDialog(*m_editor, assets, this);
    dialog->popup(QCursor::pos());
  });
}

void AssetsPanel::start_group_rename(const std::string& group) {
  auto row = m_group_rows.find(group);
  if(row == m_group_rows.end() || !row->second) {
    return;
  }
  auto label = row->second->findChild<QLabel*>("asset_label");
  if(!label) {
    return;
  }
  auto field = new QLineEdit(label->text(), row->second);
  field->setObjectName("asset_rename");
  field->setGeometry(label->geometry());
  field->selectAll();
  field->show();
  field->setFocus();
  QObject::connect(field, &QLineEdit::editingFinished, this,
    [this, group, field = QPointer<QLineEdit>(field)] {
      if(!field) {
        return;
      }
      auto text = field->text();
      field->deleteLater();
      rename_group(group, text);
    });
}

void AssetsPanel::update_selection() {
  for(auto& row : m_asset_rows) {
    if(row.second) {
      row.second->setStyleSheet(std::find(m_selected.begin(),
        m_selected.end(), row.first) != m_selected.end() ? HIGHLIGHT : "");
    }
  }
}

bool AssetsPanel::run(std::unique_ptr<Command> command, bool is_merged) {
  if(!m_editor || !command) {
    return false;
  }
  try {
    if(is_merged) {
      return static_cast<bool>(m_editor->merge(std::move(command)));
    }
    m_editor->get_history().seal();
    return static_cast<bool>(m_editor->execute(std::move(command)));
  } catch(const std::exception&) {
    return false;
  }
}

QMimeData* Rellu::make_component_mime_data(const ComponentId& component) {
  auto data = new QMimeData();
  data->setData(COMPONENT_MIME_TYPE,
    QByteArray::fromStdString(to_string(component)));
  return data;
}

std::optional<ComponentId> Rellu::read_component_mime_data(
    const QMimeData& data) {
  if(!data.hasFormat(COMPONENT_MIME_TYPE)) {
    return std::nullopt;
  }
  if(auto id = parse_node_id(data.data(COMPONENT_MIME_TYPE).toStdString())) {
    return *id;
  }
  return std::nullopt;
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

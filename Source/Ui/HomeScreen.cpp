#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QHash>
#include <QHelpEvent>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextLayout>
#include <QToolTip>
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Ui/HomeScreen.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto CONTEXT = "home";
  const auto BACKGROUND = QColor(0xF5, 0xF5, 0xF5);
  const auto SURFACE = QColor(0xFF, 0xFF, 0xFF);
  const auto CARD = QColor(0xEA, 0xEA, 0xEA);
  const auto CARD_HOVER = QColor(0xE1, 0xE1, 0xE1);
  const auto TILE_HOVER = QColor(0xEF, 0xEF, 0xEF);
  const auto CTA = QColor(0x18, 0x70, 0xE8);
  const auto CTA_ACTIVE = QColor(0x0E, 0x66, 0xCE);
  const auto QUIET_HOVER = QColor(0xEA, 0xEA, 0xEA);
  const auto QUIET_PRESSED = QColor(0xE1, 0xE1, 0xE1);
  const auto CTA_TEXT = QColor(0xFF, 0xFF, 0xFF);
  const auto TEXT = QColor(0x4B, 0x4B, 0x4B);
  const auto HEADING = QColor(0x50, 0x50, 0x50);
  const auto NAVIGATION_TEXT = QColor(0x2C, 0x2C, 0x2C);
  const auto SECONDARY_TEXT = QColor(0x76, 0x76, 0x76);
  const auto PATH_TEXT = QColor(0x6E, 0x6E, 0x6E);
  const auto RULE = QColor(0xE1, 0xE1, 0xE1);
  const auto SELECTED = QColor(0xEA, 0xEA, 0xEA);
  const auto ILLUSTRATION = QColor(0xBB, 0xBB, 0xBB);
  const auto FOCUS = QColor(0x26, 0x80, 0xEB);
  const auto MARK = QColor(0x2B, 0x2F, 0x4A);
  const auto MARK_TEXT = QColor(0x8F, 0xD3, 0xFF);
  constexpr auto MARK_X = 36;
  constexpr auto MARK_Y = 38;
  constexpr auto MARK_SIZE = 36;
  constexpr auto BUTTON_X = 25;
  constexpr auto NEW_FILE_Y = 118;
  constexpr auto OPEN_Y = 158;
  constexpr auto BUTTON_HEIGHT = 32;
  constexpr auto CTA_PADDING = 21;
  constexpr auto QUIET_PADDING = 19;
  constexpr auto RULE_X = 25;
  constexpr auto RULE_Y = 204;
  constexpr auto RULE_WIDTH = 174;
  constexpr auto NAVIGATION_X = 29;
  constexpr auto NAVIGATION_Y = 227;
  constexpr auto NAVIGATION_WIDTH = 174;
  constexpr auto NAVIGATION_HEIGHT = 32;
  constexpr auto NAVIGATION_ICON_X = 12;
  constexpr auto NAVIGATION_ICON_SIZE = 18;
  constexpr auto NAVIGATION_TEXT_X = 38;
  constexpr auto MAIN_X = 217;
  constexpr auto MARGIN = 16;
  constexpr auto TITLE_Y = 31;
  constexpr auto TITLE_HEIGHT = 22;
  constexpr auto PRESETS_Y = 67;
  constexpr auto PRESETS_HEIGHT = 260;
  constexpr auto PRESETS_PADDING = 20;
  constexpr auto ROW_OFFSET = 15;
  constexpr auto TILE_WIDTH = 134;
  constexpr auto TILE_HEIGHT = 115;
  constexpr auto IMAGE_Y = 6;
  constexpr auto IMAGE_HEIGHT = 65;
  constexpr auto NAME_Y = 75;
  constexpr auto NAME_HEIGHT = 18;
  constexpr auto CUSTOM_NAME_HEIGHT = 15;
  constexpr auto DESCRIPTION_Y = 93;
  constexpr auto DESCRIPTION_HEIGHT = 18;
  constexpr auto DROPDOWN_WIDTH = 128;
  constexpr auto DROPDOWN_PADDING = 13;
  constexpr auto DROPDOWN_LABEL_WIDTH = 97;
  constexpr auto CHEVRON_GAP = 8;
  constexpr auto CHEVRON_WIDTH = 10;
  constexpr auto FIELDS_Y = 92;
  constexpr auto FIELD_HEIGHT = 20;
  constexpr auto WIDTH_LABEL_X = 9;
  constexpr auto WIDTH_FIELD_X = 23;
  constexpr auto HEIGHT_LABEL_X = 73;
  constexpr auto HEIGHT_FIELD_X = 85;
  constexpr auto FIELD_WIDTH = 34;
  constexpr auto RECENT_Y = 359;
  constexpr auto RECENT_HEIGHT = 22;
  constexpr auto GRID_Y = 398;
  constexpr auto MAXIMUM_WIDTH = 1530;
  constexpr auto MINIMUM_COLUMN_PITCH = 152;
  constexpr auto CARD_GAP = 24;
  constexpr auto CARD_ROW_EXTENT = 104;
  constexpr auto DEFAULT_CARD_SIZE = 144;
  constexpr auto THUMBNAIL_INSET = 8.0;
  constexpr auto CARD_NAME_OFFSET = 12;
  constexpr auto CARD_LINE_HEIGHT = 21;
  constexpr auto CARD_FOLDER_HEIGHT = 18;
  constexpr auto RADIUS = 2.0;
  constexpr auto TILE_RADIUS = 4.0;
  constexpr auto FOCUS_WIDTH = 2.0;

  QFont make_font(int size, QFont::Weight weight = QFont::Normal) {
    auto font = QFont("Segoe UI");
    font.setPixelSize(size);
    font.setWeight(weight);
    return font;
  }

  QString times() {
    return QString(QChar(0x00D7));
  }

  QString format_size(double value) {
    return QString::number(value, 'g', 16);
  }

  void draw_rounded(QPainter& painter, const QRectF& bounds, double radius,
      const QColor& color) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(bounds, radius, radius);
  }

  void draw_illustration(
      QPainter& painter, HomePresetType type, const QRectF& bounds) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    auto scale = bounds.height() / 65.0;
    painter.translate(bounds.center().x() - 32.5 * scale, bounds.top());
    painter.scale(scale, scale);
    painter.setPen(QPen(ILLUSTRATION, 1));
    painter.setBrush(Qt::NoBrush);
    if(type == HomePresetType::MOBILE) {
      painter.drawRoundedRect(QRectF(22.5, 4.5, 34, 49), 3, 3);
      painter.drawEllipse(QPointF(39.5, 8), 0.6, 0.6);
      painter.setBrush(SURFACE);
      painter.drawRoundedRect(QRectF(9.5, 17.5, 19, 36), 3, 3);
      painter.drawLine(QPointF(16, 20.5), QPointF(22, 20.5));
    } else if(type == HomePresetType::DESKTOP) {
      painter.drawRect(QRectF(7.5, 13.5, 50, 30));
      painter.drawLine(QPointF(7.5, 18.5), QPointF(57.5, 18.5));
      painter.drawLine(QPointF(10, 16), QPointF(16, 16));
    } else if(type == HomePresetType::SOCIAL) {
      painter.drawRoundedRect(QRectF(14.5, 7.5, 36, 28), 2, 2);
      painter.drawLine(QPointF(12.5, 39.5), QPointF(52.5, 39.5));
      painter.drawLine(QPointF(12.5, 43.5), QPointF(52.5, 43.5));
      painter.drawLine(QPointF(12.5, 47.5), QPointF(40.5, 47.5));
    } else {
      auto page = QPainterPath(QPointF(21.5, 14.5));
      page.lineTo(36.5, 14.5);
      page.lineTo(46.5, 24.5);
      page.lineTo(46.5, 52.5);
      page.lineTo(21.5, 52.5);
      page.closeSubpath();
      painter.drawPath(page);
      painter.drawLine(QPointF(36.5, 14.5), QPointF(36.5, 24.5));
      painter.drawLine(QPointF(36.5, 24.5), QPointF(46.5, 24.5));
      painter.drawLine(QPointF(15.5, 9.5), QPointF(15.5, 17.5));
      painter.drawLine(QPointF(11.5, 13.5), QPointF(19.5, 13.5));
    }
    painter.restore();
  }

  void draw_default_thumbnail(QPainter& painter, const QRectF& bounds) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    auto size = bounds.width() * 0.4;
    auto page = QRectF(bounds.center().x() - size * 0.4,
      bounds.center().y() - size / 2, size * 0.8, size);
    auto fold = size * 0.25;
    auto path = QPainterPath(page.topLeft());
    path.lineTo(page.right() - fold, page.top());
    path.lineTo(page.right(), page.top() + fold);
    path.lineTo(page.bottomRight());
    path.lineTo(page.bottomLeft());
    path.closeSubpath();
    painter.setPen(QPen(QColor(0xDC, 0xDC, 0xDC), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.drawPolyline(QPolygonF({QPointF(page.right() - fold, page.top()),
      QPointF(page.right() - fold, page.top() + fold),
      QPointF(page.right(), page.top() + fold)}));
    painter.restore();
  }

  QString get_type_name(HomePresetType type) {
    if(type == HomePresetType::MOBILE) {
      return "mobile";
    } else if(type == HomePresetType::DESKTOP) {
      return "desktop";
    } else if(type == HomePresetType::SOCIAL) {
      return "social";
    }
    return "custom";
  }

  QString get_selection(const HomeSelections& selections,
      HomePresetType type) {
    if(type == HomePresetType::MOBILE) {
      return selections.m_mobile;
    } else if(type == HomePresetType::DESKTOP) {
      return selections.m_desktop;
    } else if(type == HomePresetType::SOCIAL) {
      return selections.m_social;
    }
    return QString();
  }

  void set_selection(
      HomeSelections& selections, HomePresetType type, const QString& name) {
    if(type == HomePresetType::MOBILE) {
      selections.m_mobile = name;
    } else if(type == HomePresetType::DESKTOP) {
      selections.m_desktop = name;
    } else if(type == HomePresetType::SOCIAL) {
      selections.m_social = name;
    }
  }

  QStringList wrap(const QString& text, const QFont& font, double width,
      int lines) {
    auto metrics = QFontMetricsF(font);
    auto layout = QTextLayout(text, font);
    auto option = QTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    auto result = QStringList();
    while(true) {
      auto line = layout.createLine();
      if(!line.isValid()) {
        break;
      }
      line.setLineWidth(width);
      if(result.size() == lines - 1) {
        result.append(metrics.elidedText(
          text.mid(line.textStart()), Qt::ElideRight, width));
        break;
      }
      result.append(text.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return result;
  }

  struct Thumbnail {
    QDateTime m_modified;
    QImage m_image;
  };

  QImage get_thumbnail(const QString& path) {
    static auto cache = QHash<QString, Thumbnail>();
    auto modified = QFileInfo(path).lastModified();
    auto entry = cache.find(path);
    if(entry != cache.end() && entry->m_modified == modified) {
      return entry->m_image;
    }
    auto image = load_xd_thumbnail(path);
    cache.insert(path, Thumbnail(modified, image));
    return image;
  }
}

class HomeScreen::Button : public QWidget {
  public:
    Button(bool is_cta, std::function<void ()> action, QWidget* parent)
        : QWidget(parent),
          m_is_cta(is_cta),
          m_action(std::move(action)),
          m_is_hovered(false),
          m_is_pressed(false) {
      setFocusPolicy(Qt::TabFocus);
      setCursor(Qt::PointingHandCursor);
      setFont(make_font(14, QFont::DemiBold));
    }

    void set_text(const QString& text) {
      m_text = text;
      auto padding = m_is_cta ? CTA_PADDING : QUIET_PADDING;
      setFixedSize(static_cast<int>(std::ceil(
        QFontMetricsF(font()).horizontalAdvance(text))) + 2 * padding,
        BUTTON_HEIGHT);
      update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      auto bounds = QRectF(rect());
      if(m_is_cta) {
        draw_rounded(painter, bounds, bounds.height() / 2,
          m_is_pressed || m_is_hovered ? CTA_ACTIVE : CTA);
      } else if(m_is_pressed) {
        draw_rounded(painter, bounds, bounds.height() / 2, QUIET_PRESSED);
      } else if(m_is_hovered) {
        draw_rounded(painter, bounds, bounds.height() / 2, QUIET_HOVER);
      }
      if(hasFocus()) {
        painter.setPen(QPen(FOCUS, FOCUS_WIDTH));
        painter.setBrush(Qt::NoBrush);
        auto ring = bounds.adjusted(1, 1, -1, -1);
        painter.drawRoundedRect(ring, ring.height() / 2, ring.height() / 2);
      }
      painter.setFont(font());
      painter.setPen(m_is_cta ? CTA_TEXT : TEXT);
      painter.drawText(bounds, Qt::AlignCenter, m_text);
    }

    void enterEvent(QEnterEvent*) override {
      m_is_hovered = true;
      update();
    }

    void leaveEvent(QEvent*) override {
      m_is_hovered = false;
      m_is_pressed = false;
      update();
    }

    void mousePressEvent(QMouseEvent* event) override {
      if(event->button() == Qt::LeftButton) {
        m_is_pressed = true;
        update();
      }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
      if(event->button() != Qt::LeftButton) {
        return;
      }
      m_is_pressed = false;
      update();
      if(rect().contains(event->position().toPoint()) && m_action) {
        m_action();
      }
    }

    void keyPressEvent(QKeyEvent* event) override {
      if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
          event->key() == Qt::Key_Space) {
        if(m_action) {
          m_action();
        }
        return;
      }
      QWidget::keyPressEvent(event);
    }

  private:
    bool m_is_cta;
    std::function<void ()> m_action;
    QString m_text;
    bool m_is_hovered;
    bool m_is_pressed;
};

class HomeScreen::Back : public QWidget {
  public:
    Back(std::function<void ()> action, QWidget* parent)
        : QWidget(parent),
          m_action(std::move(action)),
          m_is_enabled(false) {
      setToolTip("Rellu");
    }

    bool is_enabled() const {
      return m_is_enabled;
    }

    void set_enabled(bool is_enabled) {
      m_is_enabled = is_enabled;
      setCursor(is_enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
      setFocusPolicy(is_enabled ? Qt::TabFocus : Qt::NoFocus);
      update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      auto mark = QRectF(width() - MARK_SIZE, 0, MARK_SIZE, MARK_SIZE);
      if(m_is_enabled) {
        auto center = QPointF(mark.left() - 11, mark.center().y());
        painter.setPen(QPen(SECONDARY_TEXT, 2, Qt::SolidLine, Qt::RoundCap,
          Qt::RoundJoin));
        painter.drawPolyline(QPolygonF({center + QPointF(2.5, -4),
          center + QPointF(-1.5, 0), center + QPointF(2.5, 4)}));
      }
      draw_rounded(painter, mark, 6, MARK);
      painter.setFont(make_font(19, QFont::Bold));
      painter.setPen(MARK_TEXT);
      painter.drawText(mark, Qt::AlignCenter, "Rl");
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
      if(event->button() == Qt::LeftButton && m_is_enabled &&
          rect().contains(event->position().toPoint()) && m_action) {
        m_action();
      }
    }

    void keyPressEvent(QKeyEvent* event) override {
      if(m_is_enabled && (event->key() == Qt::Key_Return ||
          event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
        if(m_action) {
          m_action();
        }
        return;
      }
      QWidget::keyPressEvent(event);
    }

  private:
    std::function<void ()> m_action;
    bool m_is_enabled;
};

class HomeScreen::Tile : public QWidget {
  public:
    Tile(HomeScreen& screen, HomePresetType type, QWidget* parent)
        : QWidget(parent),
          m_screen(&screen),
          m_type(type),
          m_presets(get_home_presets(type)),
          m_width(nullptr),
          m_height(nullptr),
          m_is_hovered(false),
          m_is_keyboard_focus(false) {
      setObjectName("preset_" + get_type_name(type));
      setFixedSize(TILE_WIDTH, TILE_HEIGHT);
      setFocusPolicy(Qt::TabFocus);
      setCursor(Qt::PointingHandCursor);
      if(type == HomePresetType::CUSTOM) {
        m_width = make_field("custom_width", WIDTH_FIELD_X);
        m_height = make_field("custom_height", HEIGHT_FIELD_X);
      }
    }

    HomePresetType get_type() const {
      return m_type;
    }

    void update_fields(const QString& width, const QString& height) {
      if(!m_width) {
        return;
      }
      if(m_width->text() != width) {
        m_width->setText(width);
      }
      if(m_height->text() != height) {
        m_height->setText(height);
      }
    }

    QRectF get_dropdown_region() const {
      auto label = get_label();
      auto metrics = QFontMetricsF(make_font(12, QFont::Bold));
      auto text = std::min(metrics.horizontalAdvance(label),
        static_cast<double>(DROPDOWN_LABEL_WIDTH));
      auto content = text + CHEVRON_GAP + CHEVRON_WIDTH;
      auto left = (TILE_WIDTH - DROPDOWN_WIDTH) / 2.0 + DROPDOWN_PADDING;
      if(content < DROPDOWN_WIDTH - 2 * DROPDOWN_PADDING) {
        left = (TILE_WIDTH - content) / 2;
      }
      return QRectF(left, NAME_Y, content, NAME_HEIGHT);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      if(m_is_hovered) {
        draw_rounded(painter, QRectF(rect()), TILE_RADIUS, TILE_HOVER);
      }
      if(m_is_keyboard_focus && hasFocus()) {
        painter.setPen(QPen(FOCUS, FOCUS_WIDTH));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1),
          TILE_RADIUS, TILE_RADIUS);
      }
      draw_illustration(painter, m_type,
        QRectF(0, IMAGE_Y, TILE_WIDTH, IMAGE_HEIGHT));
      auto name = make_font(12, QFont::Bold);
      painter.setFont(name);
      painter.setPen(TEXT);
      if(m_type == HomePresetType::CUSTOM) {
        painter.drawText(QRectF(0, NAME_Y, TILE_WIDTH, CUSTOM_NAME_HEIGHT),
          Qt::AlignCenter, QFontMetricsF(name).elidedText(
            translate(CONTEXT, m_presets.front().m_name), Qt::ElideRight,
            TILE_WIDTH));
        painter.setFont(make_font(12));
        painter.setPen(SECONDARY_TEXT);
        painter.drawText(QRectF(WIDTH_LABEL_X, FIELDS_Y, WIDTH_FIELD_X -
          WIDTH_LABEL_X, FIELD_HEIGHT), Qt::AlignLeft | Qt::AlignVCenter,
          translate(CONTEXT, "W"));
        painter.drawText(QRectF(HEIGHT_LABEL_X, FIELDS_Y, HEIGHT_FIELD_X -
          HEIGHT_LABEL_X, FIELD_HEIGHT), Qt::AlignLeft | Qt::AlignVCenter,
          translate(CONTEXT, "H"));
        return;
      }
      auto region = get_dropdown_region();
      auto label = QRectF(region.left(), region.top(), region.width() -
        CHEVRON_GAP - CHEVRON_WIDTH, region.height());
      painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetricsF(name).elidedText(get_label(), Qt::ElideRight,
          label.width() + 0.5));
      auto chevron = QPointF(region.right() - CHEVRON_WIDTH / 2.0,
        region.center().y());
      painter.setPen(QPen(SECONDARY_TEXT, 1.5, Qt::SolidLine, Qt::RoundCap,
        Qt::RoundJoin));
      painter.drawPolyline(QPolygonF({chevron + QPointF(-4, -2),
        chevron + QPointF(0, 2), chevron + QPointF(4, -2)}));
      painter.setFont(make_font(12));
      painter.setPen(SECONDARY_TEXT);
      painter.drawText(QRectF(0, DESCRIPTION_Y, TILE_WIDTH,
        DESCRIPTION_HEIGHT), Qt::AlignCenter,
        get_home_preset_description(get_shown()));
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
      if(event->button() != Qt::LeftButton ||
          !rect().contains(event->position().toPoint())) {
        return;
      }
      if(m_type != HomePresetType::CUSTOM) {
        auto row = QRectF(0, NAME_Y, TILE_WIDTH, NAME_HEIGHT);
        if(row.contains(event->position()) &&
            get_dropdown_region().contains(event->position())) {
          show_menu();
          return;
        }
      } else if(QRectF(0, FIELDS_Y, TILE_WIDTH, FIELD_HEIGHT).contains(
          event->position())) {
        return;
      }
      m_screen->activate(m_type);
    }

    void keyPressEvent(QKeyEvent* event) override {
      if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        m_screen->activate(m_type);
        return;
      }
      QWidget::keyPressEvent(event);
    }

    void enterEvent(QEnterEvent*) override {
      m_is_hovered = true;
      update();
    }

    void leaveEvent(QEvent*) override {
      m_is_hovered = false;
      update();
    }

    void focusInEvent(QFocusEvent* event) override {
      m_is_keyboard_focus = event->reason() == Qt::TabFocusReason ||
        event->reason() == Qt::BacktabFocusReason;
      update();
      QWidget::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override {
      m_is_keyboard_focus = false;
      update();
      QWidget::focusOutEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
      if(event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent*>(event)->key();
        if(key == Qt::Key_Return || key == Qt::Key_Enter) {
          m_screen->activate(m_type);
          return true;
        }
      }
      return QWidget::eventFilter(watched, event);
    }

  private:
    HomeScreen* m_screen;
    HomePresetType m_type;
    std::vector<HomePreset> m_presets;
    QLineEdit* m_width;
    QLineEdit* m_height;
    bool m_is_hovered;
    bool m_is_keyboard_focus;

    HomePreset get_shown() const {
      return m_screen->get_shown_preset(m_type);
    }

    QString get_label() const {
      return get_home_preset_label(get_shown());
    }

    QLineEdit* make_field(const QString& name, int x) {
      auto field = new QLineEdit(this);
      field->setObjectName(name);
      field->setFrame(false);
      field->setFont(make_font(11));
      field->setGeometry(x, FIELDS_Y, FIELD_WIDTH, FIELD_HEIGHT);
      field->setCursor(Qt::IBeamCursor);
      field->setStyleSheet(QString(
        "QLineEdit { background: #F5F5F5; color: #767676; border: none;"
        " border-bottom: 1px solid #E1E1E1; padding: 0px 2px; }"
        " QLineEdit:focus { border-bottom: 1px solid #2680EB; }"));
      field->installEventFilter(this);
      QObject::connect(field, &QLineEdit::textEdited, field,
        [=, this] (const QString& text) {
          auto is_width = field == m_width;
          if(!is_home_size_text(text)) {
            auto previous = is_width ? m_screen->m_width : m_screen->m_height;
            auto position = std::max(field->cursorPosition() - 1, 0);
            field->setText(previous);
            field->setCursorPosition(
              std::min(position, static_cast<int>(previous.size())));
            return;
          }
          if(is_width) {
            m_screen->set_custom_text(text, m_screen->m_height);
          } else {
            m_screen->set_custom_text(m_screen->m_width, text);
          }
        });
      return field;
    }

    void show_menu() {
      auto menu = QMenu(this);
      auto shown = get_shown();
      for(auto& preset : m_presets) {
        auto action = menu.addAction(get_home_preset_label(preset));
        action->setCheckable(true);
        action->setChecked(preset == shown);
        QObject::connect(action, &QAction::triggered, this, [=, this] {
          m_screen->choose(preset);
        });
      }
      auto region = get_dropdown_region();
      menu.exec(mapToGlobal(QPoint(static_cast<int>(region.left()),
        static_cast<int>(region.bottom()))));
    }
};

class HomeScreen::Card : public QWidget {
  public:
    Card(HomeScreen& screen, const QString& path, QWidget* parent)
        : QWidget(parent),
          m_screen(&screen),
          m_path(path),
          m_title(get_recent_title(path)),
          m_folder(get_recent_folder(path)),
          m_thumbnail(get_thumbnail(path)),
          m_size(0),
          m_is_hovered(false),
          m_is_keyboard_focus(false) {
      setObjectName("recent");
      setFocusPolicy(Qt::TabFocus);
      setCursor(Qt::PointingHandCursor);
      set_size(DEFAULT_CARD_SIZE);
    }

    void set_size(int size) {
      if(size == m_size) {
        return;
      }
      m_size = size;
      m_lines = wrap(m_title, make_font(14), size, 2);
      setFixedSize(size, size + CARD_NAME_OFFSET + CARD_LINE_HEIGHT *
        static_cast<int>(std::max<qsizetype>(m_lines.size(), 1)) +
        CARD_FOLDER_HEIGHT);
      update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      auto frame = QRectF(0, 0, m_size, m_size);
      draw_rounded(painter, frame, RADIUS, m_is_hovered ? CARD_HOVER : CARD);
      auto area = frame.adjusted(THUMBNAIL_INSET, THUMBNAIL_INSET,
        -THUMBNAIL_INSET, -THUMBNAIL_INSET);
      if(m_thumbnail.isNull()) {
        draw_default_thumbnail(painter, area);
      } else {
        auto size = QSizeF(m_thumbnail.size()).scaled(area.size(),
          Qt::KeepAspectRatio);
        auto target = QRectF(area.center().x() - size.width() / 2,
          area.center().y() - size.height() / 2, size.width(), size.height());
        painter.drawImage(target, m_thumbnail);
      }
      if(m_is_keyboard_focus && hasFocus()) {
        painter.setPen(QPen(FOCUS, FOCUS_WIDTH));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame.adjusted(1, 1, -1, -1), RADIUS, RADIUS);
      }
      painter.setFont(make_font(14));
      painter.setPen(TEXT);
      auto y = static_cast<double>(m_size + CARD_NAME_OFFSET);
      for(auto& line : m_lines) {
        painter.drawText(QRectF(0, y, m_size, CARD_LINE_HEIGHT),
          Qt::AlignLeft | Qt::AlignVCenter, line);
        y += CARD_LINE_HEIGHT;
      }
      auto folder = make_font(12);
      painter.setFont(folder);
      painter.setPen(PATH_TEXT);
      painter.drawText(QRectF(0, get_folder_y(), m_size, CARD_FOLDER_HEIGHT),
        Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(folder).elidedText(
          m_folder, Qt::ElideRight, m_size));
    }

    bool event(QEvent* event) override {
      if(event->type() == QEvent::ToolTip) {
        auto help = static_cast<QHelpEvent*>(event);
        auto text = help->pos().y() >= get_folder_y() ? m_path : m_title;
        QToolTip::showText(help->globalPos(), text, this);
        return true;
      }
      return QWidget::event(event);
    }

    void enterEvent(QEnterEvent*) override {
      m_is_hovered = true;
      update();
    }

    void leaveEvent(QEvent*) override {
      m_is_hovered = false;
      update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
      if(event->button() == Qt::LeftButton &&
          rect().contains(event->position().toPoint())) {
        m_screen->open(m_path);
      }
    }

    void keyPressEvent(QKeyEvent* event) override {
      if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        m_screen->open(m_path);
        return;
      }
      QWidget::keyPressEvent(event);
    }

    void focusInEvent(QFocusEvent* event) override {
      m_is_keyboard_focus = event->reason() == Qt::TabFocusReason ||
        event->reason() == Qt::BacktabFocusReason;
      update();
      QWidget::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override {
      m_is_keyboard_focus = false;
      update();
      QWidget::focusOutEvent(event);
    }

  private:
    HomeScreen* m_screen;
    QString m_path;
    QString m_title;
    QString m_folder;
    QImage m_thumbnail;
    QStringList m_lines;
    int m_size;
    bool m_is_hovered;
    bool m_is_keyboard_focus;

    int get_folder_y() const {
      return height() - CARD_FOLDER_HEIGHT;
    }
};

class HomeScreen::Content : public QWidget {
  public:
    explicit Content(HomeScreen& screen)
        : m_screen(&screen) {
      for(auto type : {HomePresetType::MOBILE, HomePresetType::DESKTOP,
          HomePresetType::SOCIAL, HomePresetType::CUSTOM}) {
        m_tiles.push_back(new Tile(screen, type, this));
      }
    }

    const std::vector<Tile*>& get_tiles() const {
      return m_tiles;
    }

    void set_paths(const QStringList& paths) {
      for(auto card : m_cards) {
        delete card;
      }
      m_cards.clear();
      for(auto& path : paths) {
        auto card = new Card(*m_screen, path, this);
        card->show();
        m_cards.push_back(card);
      }
      arrange(width());
    }

    void arrange(int width) {
      if(this->width() != width) {
        resize(width, height());
      }
      auto inner = get_inner();
      auto row = static_cast<int>(m_tiles.size()) * TILE_WIDTH;
      auto left = inner.left() + std::max(PRESETS_PADDING,
        (inner.width() - row) / 2);
      auto top = PRESETS_Y + (PRESETS_HEIGHT - TILE_HEIGHT + ROW_OFFSET) / 2;
      for(auto tile : m_tiles) {
        tile->move(left, top);
        left += TILE_WIDTH;
      }
      auto columns = std::max(inner.width() / MINIMUM_COLUMN_PITCH, 1);
      auto pitch = static_cast<double>(inner.width()) / columns;
      auto size = std::max(static_cast<int>(pitch) - CARD_GAP, 1);
      auto row_pitch = size + CARD_ROW_EXTENT;
      auto bottom = PRESETS_Y + PRESETS_HEIGHT;
      for(auto i = 0; i != static_cast<int>(m_cards.size()); ++i) {
        auto x = inner.left() +
          static_cast<int>(std::round((i % columns) * pitch));
        auto y = GRID_Y + (i / columns) * row_pitch;
        m_cards[i]->set_size(size);
        m_cards[i]->move(x, y);
        bottom = std::max(bottom, y + row_pitch);
      }
      auto height = bottom + MARGIN;
      if(this->height() != height) {
        resize(width, height);
      }
      update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.fillRect(rect(), BACKGROUND);
      auto inner = get_inner();
      painter.setFont(make_font(18, QFont::DemiBold));
      painter.setPen(TEXT);
      painter.drawText(QRectF(inner.left(), TITLE_Y, inner.width(),
        TITLE_HEIGHT), Qt::AlignLeft | Qt::AlignVCenter,
        translate(CONTEXT, "Design. Prototype. Share."));
      draw_rounded(painter, QRectF(inner.left(), PRESETS_Y, inner.width(),
        PRESETS_HEIGHT), RADIUS, SURFACE);
      if(!m_cards.empty()) {
        painter.setPen(HEADING);
        painter.drawText(QRectF(inner.left(), RECENT_Y, inner.width(),
          RECENT_HEIGHT), Qt::AlignLeft | Qt::AlignVCenter,
          translate(CONTEXT, "Recent"));
      }
    }

  private:
    HomeScreen* m_screen;
    std::vector<Tile*> m_tiles;
    std::vector<Card*> m_cards;

    QRect get_inner() const {
      auto available = std::max(width() - 2 * MARGIN, 0);
      auto inner = std::min(available, MAXIMUM_WIDTH);
      return QRect(MARGIN + (available - inner) / 2, 0, inner, height());
    }
};

const std::vector<HomePreset>& Rellu::get_home_presets() {
  static const auto presets = std::vector<HomePreset>{
    HomePreset(HomePresetType::MOBILE, "iPhone 14 Pro Max", 430, 932),
    HomePreset(HomePresetType::MOBILE, "iPhone 14 Pro", 393, 852),
    HomePreset(HomePresetType::MOBILE,
      "iPhone 14 Plus, 13 Pro Max, 12 Pro Max", 428, 926),
    HomePreset(HomePresetType::MOBILE, "iPhone 14, 13 Pro, 13, 12 Pro, 12",
      390, 844),
    HomePreset(HomePresetType::MOBILE, "iPhone 13 mini", 375, 812),
    HomePreset(HomePresetType::MOBILE, "iPhone X, XS, 11 Pro", 375, 812),
    HomePreset(HomePresetType::MOBILE, "iPhone XR, XS Max, 11", 414, 896),
    HomePreset(HomePresetType::MOBILE, "Google Pixel 7 Pro, 6 Pro", 412,
      892),
    HomePreset(HomePresetType::MOBILE, "iPad", 768, 1024),
    HomePreset(HomePresetType::DESKTOP, "Web 1920", 1920, 1080),
    HomePreset(HomePresetType::DESKTOP, "Web 1366", 1366, 768),
    HomePreset(HomePresetType::DESKTOP, "Web 1280", 1280, 800),
    HomePreset(HomePresetType::SOCIAL, "Instagram Story", 1080, 1920),
    HomePreset(HomePresetType::SOCIAL, "Instagram Post", 1080, 1080),
    HomePreset(HomePresetType::SOCIAL, "Twitter Post", 1024, 512),
    HomePreset(HomePresetType::SOCIAL, "Twitter Header", 1500, 500),
    HomePreset(HomePresetType::SOCIAL, "Facebook Post", 1200, 630),
    HomePreset(HomePresetType::SOCIAL, "Facebook Cover", 820, 312),
    HomePreset(HomePresetType::SOCIAL, "YouTube Video", 1280, 720),
    HomePreset(HomePresetType::CUSTOM, "Custom Size", 0, 0)};
  return presets;
}

std::vector<HomePreset> Rellu::get_home_presets(HomePresetType type) {
  auto presets = std::vector<HomePreset>();
  for(auto& preset : get_home_presets()) {
    if(preset.m_type == type) {
      presets.push_back(preset);
    }
  }
  return presets;
}

QString Rellu::get_home_preset_label(const HomePreset& preset) {
  auto name = translate(CONTEXT, preset.m_name);
  if(preset.m_width == 0 || preset.m_height == 0) {
    return name;
  }
  return QString("%1 (%2 %3 %4)").arg(name, format_size(preset.m_width),
    times(), format_size(preset.m_height));
}

QString Rellu::get_home_preset_description(const HomePreset& preset) {
  if(preset.m_width == 0 || preset.m_height == 0) {
    return QString();
  }
  return QString("%1 x %2 px").arg(format_size(preset.m_width),
    format_size(preset.m_height));
}

bool Rellu::is_home_size_text(const QString& text) {
  static const auto patterns = std::vector<QRegularExpression>{
    QRegularExpression(
      R"(^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$)"),
    QRegularExpression(R"(^[+-]?Infinity$)"),
    QRegularExpression(R"(^0[xX][0-9a-fA-F]+$)"),
    QRegularExpression(R"(^0[oO][0-7]+$)"),
    QRegularExpression(R"(^0[bB][01]+$)")};
  auto trimmed = text.trimmed();
  if(trimmed.isEmpty()) {
    return true;
  }
  return std::any_of(patterns.begin(), patterns.end(),
    [&] (const auto& pattern) {
      return pattern.match(trimmed).hasMatch();
    });
}

double Rellu::to_home_size(const QString& text) {
  auto trimmed = text.trimmed();
  if(trimmed.isEmpty()) {
    return 0;
  }
  if(trimmed.endsWith("Infinity")) {
    return trimmed.startsWith('-') ? -INFINITY : INFINITY;
  }
  if(trimmed.size() > 2 && trimmed[0] == '0' && trimmed[1].isLetter()) {
    auto base = trimmed[1].toLower() == 'x' ? 16 :
      trimmed[1].toLower() == 'o' ? 8 : 2;
    auto value = 0.0;
    for(auto i = 2; i < trimmed.size(); ++i) {
      value = value * base + QString(trimmed[i]).toInt(nullptr, 16);
    }
    return value;
  }
  if(trimmed.endsWith('.')) {
    trimmed.chop(1);
  }
  return trimmed.toDouble();
}

QString Rellu::get_recent_title(const QString& path) {
  auto name = QFileInfo(path).fileName();
  if(name.endsWith(".xd", Qt::CaseInsensitive)) {
    name.chop(3);
  }
  return name;
}

QString Rellu::get_recent_folder(const QString& path) {
  return QDir::toNativeSeparators(QFileInfo(path).absolutePath());
}

QImage Rellu::load_xd_thumbnail(const QString& path) {
  try {
    auto package = XdPackage(std::filesystem::path(path.toStdU16String()));
    for(auto name : {"thumbnail.png", "preview.png"}) {
      if(package.contains(name)) {
        auto image = QImage::fromData(package.read(name));
        if(!image.isNull()) {
          return image;
        }
      }
    }
  } catch(const std::exception&) {
  }
  return QImage();
}

HomeSelections Rellu::load_home_selections(QSettings& settings) {
  auto selections = HomeSelections();
  selections.m_mobile = settings.value("home/mobile").toString();
  selections.m_desktop = settings.value("home/desktop").toString();
  selections.m_social = settings.value("home/social").toString();
  selections.m_width = settings.value("home/custom_width").toString();
  selections.m_height = settings.value("home/custom_height").toString();
  return selections;
}

void Rellu::save_home_selections(
    QSettings& settings, const HomeSelections& selections) {
  settings.setValue("home/mobile", selections.m_mobile);
  settings.setValue("home/desktop", selections.m_desktop);
  settings.setValue("home/social", selections.m_social);
  settings.setValue("home/custom_width", selections.m_width);
  settings.setValue("home/custom_height", selections.m_height);
}

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent),
      m_back(new Back([this] {
        if(m_back_handler) {
          m_back_handler();
        }
      }, this)),
      m_new_file(new Button(true, [this] {
        if(m_new_handler) {
          m_new_handler();
        }
      }, this)),
      m_open(new Button(false, [this] {
        if(m_open_dialog_handler) {
          m_open_dialog_handler();
        }
      }, this)),
      m_scroll(new QScrollArea(this)),
      m_content(new Content(*this)) {
  setMinimumSize(800, 500);
  m_back->setObjectName("back");
  m_new_file->setObjectName("new_file");
  m_open->setObjectName("open");
  m_back->setGeometry(0, MARK_Y, MARK_X + MARK_SIZE, MARK_SIZE);
  m_new_file->move(BUTTON_X, NEW_FILE_Y);
  m_open->move(BUTTON_X, OPEN_Y);
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setWidgetResizable(false);
  m_scroll->setWidget(m_content);
  m_scroll->viewport()->setAutoFillBackground(false);
  m_scroll->viewport()->installEventFilter(this);
  retranslate();
}

void HomeScreen::set_create_handler(CreateHandler handler) {
  m_create_handler = std::move(handler);
}

void HomeScreen::set_new_handler(ActionHandler handler) {
  m_new_handler = std::move(handler);
}

void HomeScreen::set_open_handler(OpenHandler handler) {
  m_open_handler = std::move(handler);
}

void HomeScreen::set_open_dialog_handler(ActionHandler handler) {
  m_open_dialog_handler = std::move(handler);
}

void HomeScreen::set_back_handler(ActionHandler handler) {
  m_back_handler = std::move(handler);
}

void HomeScreen::set_selections_handler(SelectionsHandler handler) {
  m_selections_handler = std::move(handler);
}

bool HomeScreen::is_back_enabled() const {
  return m_back->is_enabled();
}

void HomeScreen::set_back_enabled(bool is_enabled) {
  m_back->set_enabled(is_enabled);
}

const QStringList& HomeScreen::get_recent_paths() const {
  return m_paths;
}

void HomeScreen::set_recent_paths(const QStringList& paths) {
  if(paths == m_paths) {
    return;
  }
  m_paths = paths;
  m_content->set_paths(paths);
  m_content->update();
}

const HomeSelections& HomeScreen::get_selections() const {
  return m_selections;
}

void HomeScreen::set_selections(const HomeSelections& selections) {
  m_selections = selections;
  set_custom_text(selections.m_width, selections.m_height);
  for(auto tile : m_content->get_tiles()) {
    tile->update();
  }
}

HomePreset HomeScreen::get_shown_preset(HomePresetType type) const {
  auto presets = get_home_presets(type);
  if(type == HomePresetType::CUSTOM) {
    auto preset = presets.front();
    if(is_home_size_text(m_width) && is_home_size_text(m_height)) {
      preset.m_width = to_home_size(m_width);
      preset.m_height = to_home_size(m_height);
    }
    return preset;
  }
  auto name = get_selection(m_selections, type);
  auto preset = std::find_if(presets.begin(), presets.end(),
    [&] (const auto& preset) {
      return preset.m_name == name;
    });
  if(preset == presets.end()) {
    return presets.front();
  }
  return *preset;
}

void HomeScreen::activate(HomePresetType type) {
  if(type == HomePresetType::CUSTOM) {
    auto selections = m_selections;
    selections.m_width = m_width;
    selections.m_height = m_height;
    update_selections(selections);
  }
  create(get_shown_preset(type));
}

void HomeScreen::choose(const HomePreset& preset) {
  if(preset.m_type != HomePresetType::CUSTOM) {
    auto selections = m_selections;
    set_selection(selections, preset.m_type, preset.m_name);
    update_selections(selections);
    for(auto tile : m_content->get_tiles()) {
      tile->update();
    }
  }
  create(preset);
}

void HomeScreen::paintEvent(QPaintEvent*) {
  auto painter = QPainter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), BACKGROUND);
  painter.fillRect(QRect(0, 0, width(), HEADER_HEIGHT), SURFACE);
  painter.fillRect(QRect(RULE_X, RULE_Y, RULE_WIDTH, 1), RULE);
  auto item = QRectF(NAVIGATION_X, NAVIGATION_Y, NAVIGATION_WIDTH,
    NAVIGATION_HEIGHT);
  draw_rounded(painter, item, 4, SELECTED);
  auto icon = make_icon(Glyph::HOME, NAVIGATION_TEXT);
  icon.paint(&painter, QRect(NAVIGATION_X + NAVIGATION_ICON_X,
    NAVIGATION_Y + (NAVIGATION_HEIGHT - NAVIGATION_ICON_SIZE) / 2,
    NAVIGATION_ICON_SIZE, NAVIGATION_ICON_SIZE));
  painter.setFont(make_font(14));
  painter.setPen(NAVIGATION_TEXT);
  painter.drawText(item.adjusted(NAVIGATION_TEXT_X, 0, 0, 0),
    Qt::AlignLeft | Qt::AlignVCenter, translate(CONTEXT, "Home"));
}

void HomeScreen::resizeEvent(QResizeEvent* event) {
  m_scroll->setGeometry(MAIN_X, HEADER_HEIGHT, std::max(width() - MAIN_X, 0),
    std::max(height() - HEADER_HEIGHT, 0));
  m_content->arrange(m_scroll->viewport()->width());
  QWidget::resizeEvent(event);
}

void HomeScreen::keyPressEvent(QKeyEvent* event) {
  if(event->matches(QKeySequence::Open)) {
    if(m_open_dialog_handler) {
      m_open_dialog_handler();
    }
    return;
  }
  QWidget::keyPressEvent(event);
}

void HomeScreen::changeEvent(QEvent* event) {
  if(event->type() == QEvent::LanguageChange) {
    retranslate();
  }
  QWidget::changeEvent(event);
}

bool HomeScreen::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_scroll->viewport() && event->type() == QEvent::Resize) {
    m_content->arrange(m_scroll->viewport()->width());
  }
  return QWidget::eventFilter(watched, event);
}

void HomeScreen::create(const HomePreset& preset) {
  if(m_create_handler) {
    m_create_handler(preset);
  }
}

void HomeScreen::open(const QString& path) {
  if(m_open_handler) {
    m_open_handler(path);
  }
}

void HomeScreen::update_selections(const HomeSelections& selections) {
  if(selections == m_selections) {
    return;
  }
  m_selections = selections;
  if(m_selections_handler) {
    m_selections_handler(m_selections);
  }
}

void HomeScreen::set_custom_text(
    const QString& width, const QString& height) {
  m_width = width;
  m_height = height;
  for(auto tile : m_content->get_tiles()) {
    tile->update_fields(width, height);
  }
}

void HomeScreen::retranslate() {
  m_new_file->set_text(translate(CONTEXT, "New file"));
  m_open->set_text(translate(CONTEXT, "Open"));
  update();
  m_content->update();
}

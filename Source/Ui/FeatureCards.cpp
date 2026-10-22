#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTextLayout>
#include "Rellu/Ui/FeatureCards.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "BoxShadow.hpp"

using namespace Rellu;

namespace {
  const auto FEATURE_PROPERTY = "rellu_feature";
  constexpr auto CARD_WIDTH = 280.0;
  constexpr auto BORDER_WIDTH = 1.0;
  constexpr auto CORNER_RADIUS = 4.0;
  constexpr auto IMAGE_HEIGHT = 200.0;
  constexpr auto BODY_TOP = 22.0;
  constexpr auto BODY_SIDE = 24.0;
  constexpr auto DESCRIPTION_TOP = 16.0;
  constexpr auto BODY_HEIGHT = 140.0;
  constexpr auto LINK_TOP = 22.0;
  constexpr auto BOTTOM_MARGIN = 24.0;
  constexpr auto TITLE_SIZE = 16;
  constexpr auto TEXT_SIZE = 14;
  constexpr auto LINK_ICON_SIZE = 12.0;
  constexpr auto LINK_ICON_GAP = 4.0;
  constexpr auto LINE_HEIGHT = 1.25;
  constexpr auto INFO_ANCHOR_SIZE = 20.0;
  const auto BACKGROUND = QColor(0xFF, 0xFF, 0xFF);
  const auto IMAGE_BACKGROUND = QColor(0xF5, 0xF5, 0xF5);
  const auto BORDER = QColor(0xCA, 0xCA, 0xCA);
  const auto TITLE = QColor(0x2C, 0x2C, 0x2C);
  const auto DESCRIPTION = QColor(0x4B, 0x4B, 0x4B);
  const auto LINK = QColor(0x18, 0x70, 0xE8);
  const auto LINE = QColor(0xCC, 0xCC, 0xCC);
  const auto PINK = QColor(0xFF, 0xC4, 0xD0);
  const auto BLUE = QColor(0x5B, 0x9B, 0xE6);
  const auto GREEN = QColor(0x3C, 0xC0, 0x50);
  const auto NAVY = QColor(0x3A, 0x3F, 0x5C);
  const auto PAPER = QColor(0xFF, 0xFF, 0xFF);
  const auto LAVENDER = QColor(0xE4, 0xE2, 0xF0);

  QString get_title(Feature feature) {
    auto title = [] (const char* text) {
      return translate("feature_card", text);
    };
    auto shortcut = [&] (const char* text, const char* keys) {
      return title(text) + ' ' + translate("feature_card", keys);
    };
    switch(feature) {
      case Feature::DEVICE_PREVIEW:
        return title("Device preview");
      case Feature::DESKTOP_PREVIEW:
        return title("Desktop preview");
      case Feature::ASSETS_PANEL:
        return shortcut("Libraries", "(Shift + Ctrl + Y)");
      case Feature::LAYERS_PANEL:
        return shortcut("Layers", "(Ctrl + Y)");
      case Feature::PLUGINS_PANEL:
        return shortcut("Plugins", "(Shift + Ctrl + P)");
      case Feature::REPEAT_GRID:
        return shortcut("Repeat Grid", "(Ctrl + R)");
      case Feature::COMPONENTS:
        return shortcut("Components", "(Ctrl + K)");
      case Feature::TRANSFORM:
        return title("Transform");
      case Feature::LAYOUT:
        return title("Layout");
      case Feature::INTERACTION:
        return title("Interactions");
      case Feature::INTERACTION_ACTION_TYPE:
        return title("Actions");
    }
    return QString();
  }

  QString get_description(Feature feature) {
    auto text = [] (const char* text) {
      return translate("feature_card", text);
    };
    switch(feature) {
      case Feature::DEVICE_PREVIEW:
        return text("Preview your designs on a USB-connected mobile device.");
      case Feature::DESKTOP_PREVIEW:
        return text("Preview and interact with your design.");
      case Feature::ASSETS_PANEL:
        return text("Leverage libraries to reuse and share assets with teams.");
      case Feature::LAYERS_PANEL:
        return text(
          "View all artboards, objects, and groups in your document.");
      case Feature::PLUGINS_PANEL:
        return text(
          "View your installed plugins and use them while designing.");
      case Feature::REPEAT_GRID:
        return text(
          "Create repeated objects and customize with text and images.");
      case Feature::COMPONENTS:
        return text("Save common design objects and customize instances.");
      case Feature::TRANSFORM:
        return text(
          "Resize, rotate, or apply 3D Transforms to selected objects.");
      case Feature::LAYOUT:
        return text("Define how groups of objects resize on the canvas.");
      case Feature::INTERACTION:
        return text("Define triggers and actions to make designs interactive.");
      case Feature::INTERACTION_ACTION_TYPE:
        return text("Select an interaction, such as transition or animation.");
    }
    return QString();
  }

  struct Placement {
    CardPlacement m_placement;
    double m_offset;
    bool m_is_click_ignored;
  };

  Placement get_placement(Feature feature) {
    switch(feature) {
      case Feature::DEVICE_PREVIEW:
        return Placement(CardPlacement::BOTTOM_RIGHT, 1, false);
      case Feature::DESKTOP_PREVIEW:
        return Placement(CardPlacement::BOTTOM_RIGHT, 0, false);
      case Feature::ASSETS_PANEL:
        return Placement(CardPlacement::RIGHT_BOTTOM, 8, false);
      case Feature::LAYERS_PANEL:
        return Placement(CardPlacement::RIGHT_BOTTOM, 8, false);
      case Feature::PLUGINS_PANEL:
        return Placement(CardPlacement::RIGHT_BOTTOM, 8, false);
      case Feature::REPEAT_GRID:
        return Placement(CardPlacement::LEFT_TOP, 15, false);
      case Feature::COMPONENTS:
        return Placement(CardPlacement::BOTTOM_RIGHT, 2, true);
      case Feature::TRANSFORM:
        return Placement(CardPlacement::BOTTOM_RIGHT, 2, true);
      case Feature::LAYOUT:
        return Placement(CardPlacement::TOP_RIGHT, 4, true);
      case Feature::INTERACTION:
        return Placement(CardPlacement::BOTTOM_RIGHT, 2, true);
      case Feature::INTERACTION_ACTION_TYPE:
        return Placement(CardPlacement::BOTTOM_RIGHT, 2, true);
    }
    return Placement(CardPlacement::BOTTOM_RIGHT, 0, false);
  }

  void fill(QPainter& painter, const QColor& color, const QRectF& rect,
      double radius = 2) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(rect, radius, radius);
  }

  void stroke(QPainter& painter, const QColor& color, const QRectF& rect,
      double width = 1.5, double radius = 2) {
    painter.setPen(QPen(color, width));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, radius, radius);
  }

  void draw_lines(QPainter& painter, QPointF origin, double width, int count) {
    for(auto i = 0; i != count; ++i) {
      fill(painter, LINE, QRectF(origin.x(), origin.y() + 8 * i,
        i + 1 == count ? width * 0.6 : width, 4));
    }
  }

  void draw_pointer(QPainter& painter, QPointF tip) {
    auto path = QPainterPath(tip);
    path.lineTo(tip + QPointF(0, 22));
    path.lineTo(tip + QPointF(6, 16));
    path.lineTo(tip + QPointF(15, 16));
    path.closeSubpath();
    painter.setPen(QPen(PAPER, 1.5));
    painter.setBrush(BLUE);
    painter.drawPath(path);
  }

  void draw_screen(QPainter& painter, const QRectF& rect) {
    fill(painter, PAPER, rect, 3);
    stroke(painter, LINE, rect, 2, 3);
    fill(painter, LAVENDER, QRectF(rect.left(), rect.top(), rect.width(), 14),
      3);
  }

  void draw_illustration(QPainter& painter, const QRectF& area,
      Feature feature) {
    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(area.center() - QPointF(140, 100));
    switch(feature) {
      case Feature::DEVICE_PREVIEW:
        draw_screen(painter, QRectF(36, 46, 140, 100));
        fill(painter, PINK, QRectF(52, 72, 50, 34));
        draw_lines(painter, QPointF(112, 74), 50, 3);
        fill(painter, PAPER, QRectF(198, 40, 56, 110), 8);
        stroke(painter, NAVY, QRectF(198, 40, 56, 110), 2.5, 8);
        fill(painter, PINK, QRectF(206, 56, 40, 26));
        draw_lines(painter, QPointF(206, 92), 40, 3);
        painter.setPen(QPen(NAVY, 2.5, Qt::DashLine));
        painter.drawLine(QPointF(176, 120), QPointF(198, 120));
        break;
      case Feature::DESKTOP_PREVIEW:
        draw_screen(painter, QRectF(40, 36, 200, 130));
        fill(painter, PINK, QRectF(58, 64, 50, 40));
        fill(painter, BLUE, QRectF(116, 64, 50, 40));
        draw_lines(painter, QPointF(58, 118), 108, 3);
        painter.setPen(Qt::NoPen);
        painter.setBrush(NAVY);
        painter.drawPolygon(QPolygonF({QPointF(198, 78), QPointF(198, 112),
          QPointF(226, 95)}));
        break;
      case Feature::ASSETS_PANEL:
        fill(painter, PAPER, QRectF(70, 30, 140, 140), 3);
        stroke(painter, LINE, QRectF(70, 30, 140, 140), 1.5, 3);
        for(auto i = 0; i != 4; ++i) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(std::array{PINK, BLUE, GREEN, NAVY}[i]);
          painter.drawEllipse(QPointF(96 + 30 * i, 58), 10, 10);
        }
        draw_lines(painter, QPointF(86, 86), 108, 2);
        for(auto i = 0; i != 2; ++i) {
          fill(painter, LAVENDER, QRectF(86 + 56 * i, 112, 50, 44));
        }
        break;
      case Feature::LAYERS_PANEL:
        fill(painter, PAPER, QRectF(80, 30, 120, 140), 3);
        stroke(painter, LINE, QRectF(80, 30, 120, 140), 1.5, 3);
        for(auto i = 0; i != 5; ++i) {
          auto row = QRectF(92, 44 + 24 * i, 96, 16);
          if(i == 2) {
            fill(painter, QColor(0xDE, 0xEC, 0xFB), row.adjusted(-6, -4, 6, 4),
              0);
          }
          stroke(painter, i == 2 ? BLUE : NAVY,
            QRectF(row.left() + 12 * (i % 2), row.top() + 2, 12, 12), 1.5, 1);
          fill(painter, LINE, QRectF(row.left() + 20 + 12 * (i % 2),
            row.top() + 6, 50, 4));
        }
        break;
      case Feature::PLUGINS_PANEL:
        for(auto i = 0; i != 3; ++i) {
          auto tile = QRectF(56 + 60 * i, 64, 48, 48);
          fill(painter, std::array{PINK, BLUE, GREEN}[i], tile, 8);
          fill(painter, PAPER, QRectF(tile.left() + 16, tile.top() + 16, 16,
            16), 3);
        }
        draw_lines(painter, QPointF(56, 128), 168, 2);
        break;
      case Feature::REPEAT_GRID:
        for(auto i = 0; i != 3; ++i) {
          auto card = QRectF(30 + 84 * i, 56, 70, 90);
          fill(painter, PAPER, card, 3);
          fill(painter, PINK, QRectF(card.left() + 8, card.top() + 8, 54, 34));
          draw_lines(painter, QPointF(card.left() + 8, card.top() + 50), 54, 2);
          fill(painter, NAVY, QRectF(card.left() + 14, card.top() + 72, 42, 8),
            4);
        }
        painter.setPen(QPen(GREEN, 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(24, 50, 236, 102));
        draw_pointer(painter, QPointF(118, 118));
        break;
      case Feature::COMPONENTS:
        for(auto [center, color] : {std::pair(QPointF(100, 100), GREEN),
            std::pair(QPointF(180, 100), BLUE)}) {
          painter.setPen(QPen(color, 3));
          painter.setBrush(PAPER);
          painter.drawPolygon(QPolygonF({center + QPointF(0, -34),
            center + QPointF(34, 0), center + QPointF(0, 34),
            center + QPointF(-34, 0)}));
          fill(painter, color, QRectF(center.x() - 10, center.y() - 10, 20,
            20), 2);
        }
        break;
      case Feature::TRANSFORM:
        painter.setPen(QPen(BLUE, 1.5));
        painter.setBrush(PINK);
        painter.save();
        painter.translate(140, 100);
        painter.rotate(-12);
        painter.drawRect(QRectF(-50, -36, 100, 72));
        for(auto corner : {QPointF(-50, -36), QPointF(50, -36),
            QPointF(50, 36), QPointF(-50, 36)}) {
          painter.setBrush(PAPER);
          painter.drawEllipse(corner, 4, 4);
        }
        painter.restore();
        painter.setPen(QPen(NAVY, 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(172, 40, 44, 44), 10 * 16, 110 * 16);
        break;
      case Feature::LAYOUT:
        stroke(painter, BLUE, QRectF(50, 50, 180, 100), 1.5, 2);
        fill(painter, PINK, QRectF(64, 64, 70, 72));
        draw_lines(painter, QPointF(146, 72), 70, 4);
        painter.setPen(QPen(NAVY, 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(236, 100), QPointF(260, 100));
        painter.drawLine(QPointF(252, 92), QPointF(260, 100));
        painter.drawLine(QPointF(252, 108), QPointF(260, 100));
        break;
      case Feature::INTERACTION:
        draw_screen(painter, QRectF(30, 50, 90, 110));
        draw_screen(painter, QRectF(170, 50, 90, 110));
        fill(painter, PINK, QRectF(42, 76, 66, 30));
        fill(painter, BLUE, QRectF(182, 76, 66, 30));
        painter.setPen(QPen(BLUE, 2.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(108, 91), QPointF(170, 91));
        painter.setBrush(BLUE);
        painter.drawEllipse(QPointF(108, 91), 4, 4);
        painter.drawPolygon(QPolygonF({QPointF(170, 91), QPointF(160, 85),
          QPointF(160, 97)}));
        break;
      case Feature::INTERACTION_ACTION_TYPE:
        fill(painter, PAPER, QRectF(80, 36, 120, 130), 3);
        stroke(painter, LINE, QRectF(80, 36, 120, 130), 1.5, 3);
        for(auto i = 0; i != 5; ++i) {
          if(i == 1) {
            fill(painter, BLUE, QRectF(80, 46 + 22 * i, 120, 20), 0);
          }
          fill(painter, i == 1 ? PAPER : LINE,
            QRectF(94, 54 + 22 * i, i % 2 ? 60 : 80, 4));
        }
        break;
    }
    painter.restore();
  }

  void draw_link_icon(QPainter& painter, const QRectF& box) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(LINK, 1.2));
    painter.setBrush(Qt::NoBrush);
    auto left = box.left();
    auto top = box.top();
    auto size = box.width();
    auto path = QPainterPath(QPointF(left + size * 0.45, top + size * 0.1));
    path.lineTo(left + size * 0.1, top + size * 0.1);
    path.lineTo(left + size * 0.1, top + size * 0.9);
    path.lineTo(left + size * 0.9, top + size * 0.9);
    path.lineTo(left + size * 0.9, top + size * 0.55);
    painter.drawPath(path);
    painter.drawLine(QPointF(left + size * 0.45, top + size * 0.55),
      QPointF(left + size * 0.9, top + size * 0.1));
    painter.drawLine(QPointF(left + size * 0.6, top + size * 0.1),
      QPointF(left + size * 0.9, top + size * 0.1));
    painter.drawLine(QPointF(left + size * 0.9, top + size * 0.1),
      QPointF(left + size * 0.9, top + size * 0.4));
    painter.restore();
  }

  QFont make_font(int size, QFont::Weight weight) {
    auto font = QApplication::font();
    font.setPixelSize(size);
    font.setWeight(weight);
    return font;
  }

  double lay_out(QTextLayout& layout, double width) {
    auto metrics = QFontMetricsF(layout.font());
    auto line_height = LINE_HEIGHT * layout.font().pixelSize();
    auto leading = (line_height - metrics.ascent() - metrics.descent()) / 2;
    layout.beginLayout();
    auto height = 0.0;
    while(true) {
      auto line = layout.createLine();
      if(!line.isValid()) {
        break;
      }
      line.setLineWidth(width);
      line.setPosition(QPointF(0, height + leading));
      height += line_height;
    }
    layout.endLayout();
    return height;
  }

  class CardWindow : public QWidget {
    public:
      CardWindow()
          : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint |
              Qt::NoDropShadowWindowHint),
            m_feature(Feature::REPEAT_GRID) {
        setObjectName("rellu_feature_card");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
      }

      void place(Feature feature, const QWidget& anchor) {
        m_feature = feature;
        m_card = get_feature_card(feature);
        auto screen = anchor.screen();
        auto ratio = screen->devicePixelRatio();
        auto origin = screen->geometry().topLeft();
        auto inner = CARD_WIDTH - 2 * BODY_SIDE;
        m_title = std::make_unique<QTextLayout>(
          m_card.m_title, make_font(TITLE_SIZE, QFont::DemiBold));
        auto title_height = lay_out(*m_title, inner);
        m_title_height = title_height;
        m_description = std::make_unique<QTextLayout>(
          m_card.m_description, make_font(TEXT_SIZE, QFont::Normal));
        auto description_height = lay_out(*m_description, inner);
        auto body = std::min(BODY_HEIGHT,
          title_height + DESCRIPTION_TOP + description_height);
        auto link_height = LINE_HEIGHT * TEXT_SIZE;
        auto height = 2 * BORDER_WIDTH + IMAGE_HEIGHT + BORDER_WIDTH +
          BODY_TOP + body + LINK_TOP + link_height + BOTTOM_MARGIN;
        m_body_height = body;
        m_size = QSizeF(CARD_WIDTH, height);
        auto position = get_card_position(m_card, get_card_anchor(feature,
          QRectF(QRect(anchor.mapToGlobal(QPoint(0, 0)) - origin,
            anchor.size()))), m_size);
        auto available = QRectF(
          screen->availableGeometry().translated(-origin));
        position.setX(std::clamp(position.x(), available.left(),
          std::max(available.left(), available.right() - m_size.width())));
        position.setY(std::clamp(position.y(), available.top(),
          std::max(available.top(), available.bottom() - m_size.height())));
        auto box = QRect(
          static_cast<int>(std::lround(position.x() * ratio)),
          static_cast<int>(std::lround(position.y() * ratio)),
          static_cast<int>(std::lround(m_size.width() * ratio)),
          static_cast<int>(std::lround(m_size.height() * ratio)));
        auto shadow = make_box_shadow(box, ratio, ShadowKind::CARD);
        m_shadow = std::move(shadow.m_image);
        m_shadow.setDevicePixelRatio(ratio);
        m_box = QRectF(QPointF(shadow.m_box.topLeft()) / ratio,
          QSizeF(shadow.m_box.size()) / ratio);
        setGeometry(QRect(shadow.m_position + origin, shadow.m_size));
        update();
      }

      QRectF get_link_rect() const {
        auto font = make_font(TEXT_SIZE, QFont::Medium);
        auto metrics = QFontMetricsF(font);
        auto text = translate("feature_card", "Learn how");
        auto top = m_box.top() + BORDER_WIDTH + IMAGE_HEIGHT + BORDER_WIDTH +
          BODY_TOP + m_body_height + LINK_TOP;
        return QRectF(m_box.left() + BODY_SIDE, top,
          metrics.horizontalAdvance(text) + LINK_ICON_GAP + LINK_ICON_SIZE,
          LINE_HEIGHT * TEXT_SIZE);
      }

      const FeatureCard& get_card() const {
        return m_card;
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.drawImage(QPointF(0, 0), m_shadow);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::Antialiasing);
        auto shape = QPainterPath();
        shape.addRoundedRect(m_box, CORNER_RADIUS, CORNER_RADIUS);
        painter.fillPath(shape, BACKGROUND);
        auto image = QRectF(m_box.left() + BORDER_WIDTH,
          m_box.top() + BORDER_WIDTH, m_box.width() - 2 * BORDER_WIDTH,
          IMAGE_HEIGHT);
        painter.save();
        painter.setClipPath(shape);
        painter.fillRect(image, IMAGE_BACKGROUND);
        draw_illustration(painter, image, m_feature);
        painter.fillRect(QRectF(image.left(), image.bottom(), image.width(),
          BORDER_WIDTH), BORDER);
        painter.restore();
        painter.setPen(QPen(BORDER, BORDER_WIDTH));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
        auto left = m_box.left() + BODY_SIDE;
        auto top = image.bottom() + BORDER_WIDTH + BODY_TOP;
        painter.save();
        painter.setClipRect(QRectF(left, top, image.width(), m_body_height));
        painter.setPen(TITLE);
        m_title->draw(&painter, QPointF(left, top));
        painter.setPen(DESCRIPTION);
        m_description->draw(&painter, QPointF(left,
          top + m_title_height + DESCRIPTION_TOP));
        painter.restore();
        auto link = get_link_rect();
        auto font = make_font(TEXT_SIZE, QFont::Medium);
        auto metrics = QFontMetricsF(font);
        auto text = translate("feature_card", "Learn how");
        painter.setFont(font);
        painter.setPen(LINK);
        auto baseline = link.top() + (link.height() + metrics.ascent() -
          metrics.descent()) / 2;
        painter.drawText(QPointF(link.left(), baseline), text);
        draw_link_icon(painter, QRectF(
          link.left() + metrics.horizontalAdvance(text) + LINK_ICON_GAP,
          baseline - LINK_ICON_SIZE + 1, LINK_ICON_SIZE, LINK_ICON_SIZE));
      }

      void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton &&
            get_link_rect().contains(event->position())) {
          hide();
        }
      }

    private:
      Feature m_feature;
      FeatureCard m_card;
      std::unique_ptr<QTextLayout> m_title;
      std::unique_ptr<QTextLayout> m_description;
      double m_title_height = 0;
      double m_body_height = 0;
      QSizeF m_size;
      QImage m_shadow;
      QRectF m_box;
  };
}

FeatureCard Rellu::get_feature_card(Feature feature) {
  auto placement = get_placement(feature);
  return FeatureCard(get_title(feature), get_description(feature),
    placement.m_placement, placement.m_offset, placement.m_is_click_ignored);
}

void Rellu::set_feature(QWidget& widget, Feature feature) {
  widget.setProperty(FEATURE_PROPERTY, static_cast<int>(feature));
}

std::optional<Feature> Rellu::get_feature(const QWidget& widget) {
  auto value = widget.property(FEATURE_PROPERTY);
  if(!value.isValid()) {
    return std::nullopt;
  }
  return static_cast<Feature>(value.toInt());
}

QRectF Rellu::get_card_anchor(Feature feature, const QRectF& widget) {
  switch(feature) {
    case Feature::COMPONENTS:
    case Feature::TRANSFORM:
    case Feature::LAYOUT:
    case Feature::INTERACTION:
    case Feature::INTERACTION_ACTION_TYPE:
      return QRectF(widget.center().x() - INFO_ANCHOR_SIZE / 2,
        widget.center().y() - INFO_ANCHOR_SIZE / 2, INFO_ANCHOR_SIZE,
        INFO_ANCHOR_SIZE);
    case Feature::REPEAT_GRID:
      return widget.adjusted(-1, -2.0 / 3, 0, 0);
    default:
      return widget;
  }
}

QPointF Rellu::get_card_position(
    const FeatureCard& card, const QRectF& anchor, const QSizeF& size) {
  auto offset = card.m_offset;
  switch(card.m_placement) {
    case CardPlacement::BOTTOM_RIGHT:
      return QPointF(anchor.right() - size.width(), anchor.bottom() + offset);
    case CardPlacement::BOTTOM_LEFT:
      return QPointF(anchor.left(), anchor.bottom() + offset);
    case CardPlacement::RIGHT_BOTTOM:
      return QPointF(anchor.right() + offset, anchor.bottom() - size.height());
    case CardPlacement::LEFT_TOP:
      return QPointF(anchor.left() - offset - size.width(), anchor.top());
    case CardPlacement::TOP_RIGHT:
      return QPointF(
        anchor.right() - size.width(), anchor.top() - offset - size.height());
  }
  return anchor.bottomLeft();
}

FeatureCardFilter::FeatureCardFilter(QObject* parent)
    : QObject(parent),
      m_window(nullptr) {
  for(auto [timer, delay] : {std::pair(&m_show_timer, SHOW_DELAY),
      std::pair(&m_close_timer, CLOSE_DELAY),
      std::pair(&m_lifetime_timer, LIFETIME)}) {
    timer->setSingleShot(true);
    timer->setInterval(delay);
  }
  connect(&m_show_timer, &QTimer::timeout, this, [this] {
    show_card();
  });
  connect(&m_close_timer, &QTimer::timeout, this, [this] {
    close();
  });
  connect(&m_lifetime_timer, &QTimer::timeout, this, [this] {
    close();
  });
}

FeatureCardFilter::~FeatureCardFilter() {
  delete m_window;
}

QWidget* FeatureCardFilter::get_card() const {
  if(m_window && m_window->isVisible()) {
    return m_window;
  }
  return nullptr;
}

bool FeatureCardFilter::eventFilter(QObject* watched, QEvent* event) {
  auto type = event->type();
  if(type == QEvent::Enter) {
    if(is_card(watched)) {
      m_close_timer.stop();
    } else if(watched->isWidgetType() &&
        get_feature(static_cast<QWidget&>(*watched))) {
      auto widget = static_cast<QWidget*>(watched);
      if(get_card() && m_anchor == widget) {
        m_close_timer.stop();
      } else {
        m_pending = widget;
        m_show_timer.start();
      }
    }
  } else if(type == QEvent::Leave) {
    if(is_card(watched)) {
      m_close_timer.start();
    } else if(watched == m_pending && m_show_timer.isActive()) {
      m_show_timer.stop();
      m_pending = nullptr;
    } else if(watched == m_anchor && get_card()) {
      m_close_timer.start();
    }
  } else if(type == QEvent::KeyPress) {
    m_show_timer.stop();
    m_pending = nullptr;
    close();
  } else if(type == QEvent::MouseButtonPress && watched->isWidgetType()) {
    if(!is_card(watched)) {
      if(watched == m_pending && m_show_timer.isActive()) {
        auto feature = get_feature(*m_pending);
        if(!feature || !get_feature_card(*feature).m_is_click_ignored) {
          m_show_timer.stop();
          m_pending = nullptr;
        }
      }
      close();
    }
  }
  return QObject::eventFilter(watched, event);
}

void FeatureCardFilter::show_card() {
  if(!m_pending) {
    return;
  }
  auto feature = get_feature(*m_pending);
  if(!feature || !m_pending->isVisible()) {
    m_pending = nullptr;
    return;
  }
  if(!m_window) {
    m_window = new CardWindow();
  }
  m_close_timer.stop();
  m_anchor = m_pending;
  m_pending = nullptr;
  static_cast<CardWindow*>(m_window)->place(*feature, *m_anchor);
  m_window->show();
  m_window->raise();
  m_lifetime_timer.start();
}

void FeatureCardFilter::close() {
  m_close_timer.stop();
  m_lifetime_timer.stop();
  m_anchor = nullptr;
  if(m_window) {
    m_window->hide();
  }
}

bool FeatureCardFilter::is_card(QObject* object) const {
  return m_window && object == m_window;
}

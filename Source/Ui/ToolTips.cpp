#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>
#include <QAbstractItemView>
#include <QColor>
#include <QCursor>
#include <QEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QToolTip>
#include "Rellu/Ui/ToolTips.hpp"

using namespace Rellu;

namespace {
  const auto PLACEMENT_PROPERTY = "rellu_tool_tip_placement";
  const auto BACKGROUND = QColor(0xF0, 0xF0, 0xF0);
  const auto BORDER = QColor(0xCF, 0xCF, 0xCF);
  const auto TEXT = QColor(0x3F, 0x3F, 0x3F);
  constexpr auto BORDER_WIDTH = 4.0 / 3;
  constexpr auto PADDING_LEFT = 8.0;
  constexpr auto PADDING_TOP = 5.0;
  constexpr auto PADDING_RIGHT = 8.0;
  constexpr auto PADDING_BOTTOM = 7.0;
  constexpr auto POINTER_OFFSET = 20.0;
  constexpr auto SHADOW_REACH = 2.5;

  struct ShadowLayer {
    double m_opacity;
    double m_radius;
    double m_offset;
  };

  constexpr auto SHADOW = std::array{ShadowLayer(0.1375, 2.25, 0.4583),
    ShadowLayer(0.19, 8.9167, 7.6667)};

  double get_coverage(double position, double start, double end,
      double radius) {
    auto cumulative = [&] (double value) {
      return 0.5 * (1 + std::erf(value / (radius * std::sqrt(2.0))));
    };
    return cumulative(position - start) - cumulative(position - end);
  }

  QPoint get_screen_origin(const QScreen& screen) {
    return screen.geometry().topLeft();
  }

  class ToolTipWindow : public QWidget {
    public:
      ToolTipWindow()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint |
            Qt::NoDropShadowWindowHint | Qt::WindowTransparentForInput) {
        setObjectName("rellu_tool_tip");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
      }

      void place(const QString& text, const QScreen& screen,
          std::optional<QRectF> anchor, QPointF pointer) {
        auto ratio = screen.devicePixelRatio();
        auto font = QToolTip::font();
        auto metrics = QFontMetricsF(font);
        auto border = std::max(1L, std::lround(BORDER_WIDTH * ratio));
        auto width = 2 * border +
          std::lround((PADDING_LEFT + PADDING_RIGHT) * ratio) +
          static_cast<long>(
            std::ceil(metrics.horizontalAdvance(text) * ratio - 1e-6));
        auto height = std::lround(2 * border + (PADDING_TOP + PADDING_BOTTOM +
          metrics.ascent() + metrics.descent()) * ratio);
        auto left = 0L;
        auto top = 0L;
        if(anchor) {
          left = std::lround(anchor->right() * ratio);
          top = static_cast<long>(std::floor(
            anchor->center().y() * ratio - height / 2.0)) + 1;
        } else {
          auto x = std::floor(pointer.x() + 1e-6);
          auto y = std::floor(pointer.y() + 1e-6);
          left = static_cast<long>(std::floor(x * ratio - width / 2.0));
          top = static_cast<long>(
            std::floor((y + POINTER_OFFSET) * ratio + 0.5));
        }
        auto available = screen.availableGeometry().translated(
          -get_screen_origin(screen));
        left = std::clamp(left, std::lround(available.left() * ratio),
          std::max(std::lround(available.left() * ratio),
            std::lround((available.right() + 1) * ratio) - width));
        auto reach = std::ceil(SHADOW[1].m_radius * SHADOW_REACH * ratio);
        auto margin_left = static_cast<long>(reach);
        auto margin_top = static_cast<long>(
          std::max(0.0, reach - SHADOW[1].m_offset * ratio));
        auto margin_right = static_cast<long>(reach);
        auto margin_bottom = static_cast<long>(
          reach + SHADOW[1].m_offset * ratio);
        auto origin = QPoint(
          static_cast<int>(std::floor((left - margin_left) / ratio)),
          static_cast<int>(std::floor((top - margin_top) / ratio)));
        auto native = QPoint(static_cast<int>(std::lround(origin.x() * ratio)),
          static_cast<int>(std::lround(origin.y() * ratio)));
        auto box_left = left - native.x();
        auto box_top = top - native.y();
        auto image_width = box_left + width + margin_right;
        auto image_height = box_top + height + margin_bottom;
        m_image = QImage(static_cast<int>(image_width),
          static_cast<int>(image_height), QImage::Format_ARGB32_Premultiplied);
        m_image.fill(Qt::transparent);
        auto columns = std::vector<std::array<double, SHADOW.size()>>(
          m_image.width());
        auto rows = std::vector<std::array<double, SHADOW.size()>>(
          m_image.height());
        for(auto i = std::size_t(0); i != SHADOW.size(); ++i) {
          auto radius = SHADOW[i].m_radius * ratio;
          auto offset = SHADOW[i].m_offset * ratio;
          for(auto x = 0; x != m_image.width(); ++x) {
            columns[x][i] = get_coverage(
              x + 0.5, box_left, box_left + width, radius);
          }
          for(auto y = 0; y != m_image.height(); ++y) {
            rows[y][i] = get_coverage(y + 0.5, box_top + offset,
              box_top + height + offset, radius);
          }
        }
        for(auto y = 0; y != m_image.height(); ++y) {
          auto line = reinterpret_cast<QRgb*>(m_image.scanLine(y));
          for(auto x = 0; x != m_image.width(); ++x) {
            auto alpha = 0.0;
            for(auto i = std::size_t(0); i != SHADOW.size(); ++i) {
              alpha += SHADOW[i].m_opacity * columns[x][i] * rows[y][i];
            }
            line[x] = qRgba(0, 0, 0,
              static_cast<int>(std::lround(std::min(alpha, 1.0) * 255)));
          }
        }
        auto box = QRect(static_cast<int>(box_left),
          static_cast<int>(box_top), static_cast<int>(width),
          static_cast<int>(height));
        auto painter = QPainter(&m_image);
        painter.fillRect(box, BORDER);
        painter.fillRect(box.adjusted(static_cast<int>(border),
          static_cast<int>(border), -static_cast<int>(border),
          -static_cast<int>(border)), BACKGROUND);
        m_image.setDevicePixelRatio(ratio);
        painter.end();
        auto text_painter = QPainter(&m_image);
        text_painter.setFont(font);
        text_painter.setPen(TEXT);
        text_painter.drawText(QPointF(
          (box_left + border + PADDING_LEFT * ratio) / ratio,
          (box_top + border + (PADDING_TOP + metrics.ascent()) * ratio) /
            ratio), text);
        text_painter.end();
        auto screen_origin = get_screen_origin(screen);
        setGeometry(QRect(origin + screen_origin,
          QSize(static_cast<int>(std::ceil(image_width / ratio)) + 1,
            static_cast<int>(std::ceil(image_height / ratio)) + 1)));
        update();
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.drawImage(QPointF(0, 0), m_image);
      }

    private:
      QImage m_image;
  };
}

void Rellu::set_tool_tip_placement(
    QWidget& widget, ToolTipPlacement placement) {
  widget.setProperty(PLACEMENT_PROPERTY, static_cast<int>(placement));
}

ToolTipPlacement Rellu::get_tool_tip_placement(const QWidget& widget) {
  auto value = widget.property(PLACEMENT_PROPERTY);
  if(!value.isValid()) {
    return ToolTipPlacement::POINTER;
  }
  return static_cast<ToolTipPlacement>(value.toInt());
}

ToolTipFilter::ToolTipFilter(QObject* parent)
  : QObject(parent),
    m_window(nullptr) {}

ToolTipFilter::~ToolTipFilter() {
  delete m_window;
}

bool ToolTipFilter::eventFilter(QObject* watched, QEvent* event) {
  auto type = event->type();
  if(type == QEvent::MouseMove && watched->isWindowType()) {
    m_pointer = static_cast<QMouseEvent*>(event)->globalPosition();
  }
  if(type == QEvent::ToolTip && watched->isWidgetType()) {
    auto& widget = static_cast<QWidget&>(*watched);
    auto& help = static_cast<QHelpEvent&>(*event);
    auto text = widget.toolTip();
    auto area = std::optional<QRect>();
    if(auto view = qobject_cast<QAbstractItemView*>(widget.parentWidget());
        view && view->viewport() == &widget) {
      auto index = view->indexAt(help.pos());
      text = index.data(Qt::ToolTipRole).toString();
      area = view->visualRect(index);
    }
    if(text.isEmpty()) {
      hide();
      return QObject::eventFilter(watched, event);
    }
    auto pointer = m_pointer;
    if(!pointer ||
        (QPointF(help.globalPos()) - *pointer).manhattanLength() > 1) {
      pointer = QPointF(help.globalPos());
    }
    show(widget, text, *pointer, area);
    return true;
  }
  if(m_window && m_window->isVisible()) {
    if(type == QEvent::MouseButtonPress ||
        type == QEvent::MouseButtonDblClick || type == QEvent::KeyPress ||
        type == QEvent::Wheel || type == QEvent::WindowDeactivate) {
      hide();
    } else if(watched == m_target && type == QEvent::Hide) {
      hide();
    } else if(watched == m_target && type == QEvent::Leave &&
        !is_over_target()) {
      hide();
    } else if(watched == m_target && type == QEvent::MouseMove && m_area &&
        !m_area->contains(static_cast<QMouseEvent*>(event)->position().
          toPoint())) {
      hide();
    }
  }
  return QObject::eventFilter(watched, event);
}

bool ToolTipFilter::is_over_target() const {
  return m_target &&
    m_target->rect().contains(m_target->mapFromGlobal(QCursor::pos()));
}

void ToolTipFilter::show(QWidget& target, const QString& text,
    QPointF pointer, std::optional<QRect> area) {
  auto screen = target.screen();
  if(!screen) {
    return;
  }
  if(!m_window) {
    m_window = new ToolTipWindow();
  }
  QToolTip::hideText();
  m_target = &target;
  m_area = area;
  auto origin = get_screen_origin(*screen);
  auto anchor = std::optional<QRectF>();
  if(get_tool_tip_placement(target) == ToolTipPlacement::RIGHT) {
    anchor = QRectF(QRect(target.mapToGlobal(QPoint(0, 0)) - origin,
      target.size()));
  }
  static_cast<ToolTipWindow*>(m_window)->place(
    text, *screen, anchor, pointer - QPointF(origin));
  m_window->show();
  m_window->raise();
}

void ToolTipFilter::hide() {
  m_target = nullptr;
  m_area = std::nullopt;
  if(m_window) {
    m_window->hide();
  }
}

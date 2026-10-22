#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
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
#include <QTextLayout>
#include <QTextOption>
#include <QToolTip>
#include "Rellu/Ui/FeatureCards.hpp"
#include "Rellu/Ui/ToolTips.hpp"
#include "BoxShadow.hpp"

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
  constexpr auto MAXIMUM_WIDTH = 320.0;
  auto active_filter = static_cast<ToolTipFilter*>(nullptr);

  std::vector<QString> wrap(const QString& text, const QFont& font,
      double width) {
    auto layout = QTextLayout(text, font);
    auto option = QTextOption();
    option.setWrapMode(QTextOption::WordWrap);
    layout.setTextOption(option);
    layout.beginLayout();
    auto lines = std::vector<QString>();
    while(true) {
      auto line = layout.createLine();
      if(!line.isValid()) {
        break;
      }
      line.setLineWidth(width);
      lines.push_back(
        text.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return lines;
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
        auto lines = wrap(text, font, MAXIMUM_WIDTH - 2 * BORDER_WIDTH -
          PADDING_LEFT - PADDING_RIGHT);
        auto advance = 0.0;
        for(auto& line : lines) {
          advance = std::max(advance, metrics.horizontalAdvance(line));
        }
        auto line_height = metrics.ascent() + metrics.descent();
        auto width = 2 * border +
          std::lround((PADDING_LEFT + PADDING_RIGHT) * ratio) +
          static_cast<long>(std::ceil(advance * ratio - 1e-6));
        auto height = std::lround(2 * border + (PADDING_TOP + PADDING_BOTTOM +
          static_cast<double>(lines.size()) * line_height) * ratio);
        auto left = 0L;
        auto top = 0L;
        if(anchor) {
          left = std::lround(anchor->right() * ratio);
          top = static_cast<long>(std::floor(
            anchor->center().y() * ratio - height / 2.0)) + 1;
        } else {
          auto position = get_pointer_tip_position(pointer,
            QSize(static_cast<int>(width), static_cast<int>(height)), ratio);
          left = position.x();
          top = position.y();
        }
        auto available = screen.availableGeometry().translated(
          -get_screen_origin(screen));
        left = std::clamp(left, std::lround(available.left() * ratio),
          std::max(std::lround(available.left() * ratio),
            std::lround((available.right() + 1) * ratio) - width));
        auto shadow = make_box_shadow(QRect(static_cast<int>(left),
          static_cast<int>(top), static_cast<int>(width),
          static_cast<int>(height)), ratio);
        m_image = std::move(shadow.m_image);
        auto box = shadow.m_box;
        auto box_left = box.left();
        auto box_top = box.top();
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
        for(auto i = std::size_t(0); i != lines.size(); ++i) {
          text_painter.drawText(QPointF(
            (box_left + border + PADDING_LEFT * ratio) / ratio,
            (box_top + border + (PADDING_TOP + metrics.ascent() +
              static_cast<double>(i) * line_height) * ratio) / ratio),
            lines[i]);
        }
        text_painter.end();
        setGeometry(QRect(
          shadow.m_position + get_screen_origin(screen), shadow.m_size));
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

QPoint Rellu::get_pointer_tip_position(
    QPointF pointer, QSize size, double ratio) {
  auto x = std::floor(pointer.x() + 1e-6);
  auto y = std::floor(pointer.y() + 1e-6);
  auto left = std::round(x - size.width() / (2 * ratio));
  return QPoint(static_cast<int>(std::floor(left * ratio + 0.5)),
    static_cast<int>(std::floor((y + POINTER_OFFSET) * ratio + 0.5)));
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

bool Rellu::show_tool_tip(QWidget& target, const QString& text,
    QPoint position, std::optional<QRect> area) {
  if(!active_filter) {
    return false;
  }
  active_filter->show_text(target, text, position, area);
  return true;
}

ToolTipFilter::ToolTipFilter(QObject* parent)
    : QObject(parent),
      m_window(nullptr) {
  active_filter = this;
}

ToolTipFilter::~ToolTipFilter() {
  if(active_filter == this) {
    active_filter = nullptr;
  }
  delete m_window;
}

void ToolTipFilter::show_text(QWidget& target, const QString& text,
    QPoint position, std::optional<QRect> area) {
  auto pointer = m_pointer;
  if(!pointer || (QPointF(position) - *pointer).manhattanLength() > 1) {
    pointer = QPointF(position);
  }
  show(target, text, *pointer, area);
}

bool ToolTipFilter::eventFilter(QObject* watched, QEvent* event) {
  auto type = event->type();
  if(type == QEvent::MouseMove && watched->isWindowType()) {
    m_pointer = static_cast<QMouseEvent*>(event)->globalPosition();
  }
  if(type == QEvent::ToolTip && watched->isWidgetType()) {
    auto& widget = static_cast<QWidget&>(*watched);
    auto& help = static_cast<QHelpEvent&>(*event);
    if(get_feature(widget)) {
      hide();
      return true;
    }
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
    show_text(widget, text, help.globalPos(), area);
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

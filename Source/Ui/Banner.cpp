#include <algorithm>
#include <utility>
#include <QEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include "Rellu/Ui/Banner.hpp"

using namespace Rellu;

namespace {
  const auto BACKGROUND = QColor(0x18, 0x70, 0xE8);
  const auto FOREGROUND = QColor(0xFF, 0xFF, 0xFF);
  const auto LINK_HOVER = QColor(0xBE, 0xD2, 0xF6);
  const auto UNDERLINE = QColor(0xA5, 0xC5, 0xF5);
  const auto UNDERLINE_HOVER = QColor(0x8B, 0xB7, 0xF3);
  const auto CLOSE_HOVER = QColor(0x38, 0x88, 0xE8);
  constexpr auto RADIUS = 3.0;
  constexpr auto FONT_SIZE = 14;
  constexpr auto TEXT_OFFSET = 23.0;
  constexpr auto CLOSE_SIZE = 12.0;
  constexpr auto CLOSE_INSET = 25.0;
  constexpr auto CLOSE_HOVER_SIZE = 28.0;
  constexpr auto CLOSE_WIDTH = 1.0;
  constexpr auto UNDERLINE_OFFSET = 1.0;
  constexpr auto UNDERLINE_WIDTH = 2.0;
  constexpr auto CLOSE = -2;
  constexpr auto NONE = -1;

  QFont make_font() {
    auto font = QFont("Segoe UI");
    font.setPixelSize(FONT_SIZE);
    return font;
  }

  class ResizeFilter : public QObject {
    public:
      explicit ResizeFilter(BannerHost& host)
        : m_host(&host) {}

      bool eventFilter(QObject* watched, QEvent* event) override {
        if(event->type() == QEvent::Resize) {
          m_host->place();
        }
        return QObject::eventFilter(watched, event);
      }

    private:
      BannerHost* m_host;
  };
}

std::vector<BannerSpan> Rellu::parse_banner_message(const QString& message) {
  auto spans = std::vector<BannerSpan>();
  auto append = [&] (const QString& text, bool is_link) {
    if(text.isEmpty()) {
      return;
    }
    if(!is_link && !spans.empty() && !spans.back().m_is_link) {
      spans.back().m_text += text;
    } else {
      spans.push_back(BannerSpan(text, is_link));
    }
  };
  auto position = qsizetype(0);
  while(position < message.size()) {
    auto open = message.indexOf('[', position);
    auto close = open < 0 ? qsizetype(-1) : message.indexOf(']', open);
    auto bar = close < 0 ? qsizetype(-1) : message.indexOf('|', open);
    if(open < 0 || close < 0 || bar < 0 || bar > close) {
      append(message.mid(position), false);
      break;
    }
    append(message.mid(position, open - position), false);
    append(message.mid(open + 1, bar - open - 1), true);
    position = close + 1;
  }
  return spans;
}

Banner::Banner(const QString& message, QWidget* parent)
    : QWidget(parent),
      m_hover(NONE),
      m_is_dismissed(false) {
  setMouseTracking(true);
  setFont(make_font());
  set_message(message);
}

const QString& Banner::get_message() const {
  return m_message;
}

void Banner::set_message(const QString& message) {
  m_message = message;
  m_spans = parse_banner_message(message);
  update();
}

QRectF Banner::get_close_region() const {
  auto center = QPointF(width() - CLOSE_INSET - CLOSE_SIZE / 2,
    height() / 2.0);
  auto half = CLOSE_HOVER_SIZE / 2;
  return QRectF(center.x() - half, center.y() - half, CLOSE_HOVER_SIZE,
    CLOSE_HOVER_SIZE);
}

std::vector<QRectF> Banner::get_link_regions() const {
  auto metrics = QFontMetricsF(font());
  auto origin = get_text_origin();
  auto regions = std::vector<QRectF>();
  auto x = origin.x();
  for(auto& span : m_spans) {
    auto advance = metrics.horizontalAdvance(span.m_text);
    if(span.m_is_link) {
      regions.push_back(QRectF(x, origin.y() - metrics.ascent(), advance,
        metrics.ascent() + metrics.descent()));
    }
    x += advance;
  }
  return regions;
}

QPointF Banner::get_text_origin() const {
  auto metrics = QFontMetricsF(font());
  auto width = 0.0;
  for(auto& span : m_spans) {
    width += metrics.horizontalAdvance(span.m_text);
  }
  return QPointF(this->width() / 2.0 - TEXT_OFFSET - width / 2,
    height() / 2.0 + metrics.capHeight() / 2);
}

bool Banner::is_dismissed() const {
  return m_is_dismissed;
}

void Banner::dismiss() {
  if(m_is_dismissed) {
    return;
  }
  m_is_dismissed = true;
  hide();
  deleteLater();
}

void Banner::paintEvent(QPaintEvent*) {
  auto painter = QPainter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  auto bounds = QRectF(rect());
  auto background = QPainterPath();
  background.moveTo(bounds.bottomLeft());
  background.lineTo(bounds.left(), bounds.top() + RADIUS);
  background.quadTo(bounds.topLeft(), QPointF(bounds.left() + RADIUS,
    bounds.top()));
  background.lineTo(bounds.right() - RADIUS, bounds.top());
  background.quadTo(bounds.topRight(), QPointF(bounds.right(),
    bounds.top() + RADIUS));
  background.lineTo(bounds.bottomRight());
  background.closeSubpath();
  painter.fillPath(background, BACKGROUND);
  auto metrics = QFontMetricsF(font());
  auto origin = get_text_origin();
  auto ratio = devicePixelRatioF();
  auto x = origin.x();
  auto link = 0;
  painter.setFont(font());
  for(auto& span : m_spans) {
    auto advance = metrics.horizontalAdvance(span.m_text);
    auto is_hovered = span.m_is_link && m_hover == link;
    painter.setPen(is_hovered ? LINK_HOVER : FOREGROUND);
    painter.drawText(QPointF(x, origin.y()), span.m_text);
    if(span.m_is_link) {
      painter.fillRect(QRectF(x, origin.y() + UNDERLINE_OFFSET / ratio,
        advance, UNDERLINE_WIDTH / ratio),
        is_hovered ? UNDERLINE_HOVER : UNDERLINE);
      ++link;
    }
    x += advance;
  }
  auto close = get_close_region();
  if(m_hover == CLOSE) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(CLOSE_HOVER);
    painter.drawEllipse(close);
  }
  auto center = close.center();
  auto half = CLOSE_SIZE / 2;
  painter.setPen(QPen(FOREGROUND, CLOSE_WIDTH));
  painter.drawLine(center + QPointF(-half, -half),
    center + QPointF(half, half));
  painter.drawLine(center + QPointF(half, -half),
    center + QPointF(-half, half));
}

void Banner::mouseMoveEvent(QMouseEvent* event) {
  auto hover = find_target(event->position());
  if(hover != m_hover) {
    m_hover = hover;
    setCursor(hover == NONE ? Qt::ArrowCursor : Qt::PointingHandCursor);
    update();
  }
}

void Banner::mouseReleaseEvent(QMouseEvent* event) {
  if(event->button() == Qt::LeftButton &&
      find_target(event->position()) != NONE) {
    dismiss();
  }
}

void Banner::leaveEvent(QEvent*) {
  if(m_hover != NONE) {
    m_hover = NONE;
    update();
  }
}

int Banner::find_target(QPointF position) const {
  auto close = get_close_region();
  auto offset = position - close.center();
  auto radius = CLOSE_HOVER_SIZE / 2;
  if(offset.x() * offset.x() + offset.y() * offset.y() <= radius * radius) {
    return CLOSE;
  }
  auto regions = get_link_regions();
  for(auto i = std::size_t(0); i != regions.size(); ++i) {
    if(regions[i].contains(position)) {
      return static_cast<int>(i);
    }
  }
  return NONE;
}

BannerHost::BannerHost(QWidget& host)
  : BannerHost(host, DELAY, DURATION) {}

BannerHost::BannerHost(QWidget& host, std::chrono::milliseconds delay,
    std::chrono::milliseconds duration)
    : m_host(&host),
      m_duration(duration),
      m_filter(std::make_unique<ResizeFilter>(*this)) {
  m_delay.setSingleShot(true);
  m_delay.setInterval(delay);
  QObject::connect(&m_delay, &QTimer::timeout, [this] {
    reveal();
  });
  m_timer.setSingleShot(true);
  m_timer.setInterval(m_duration);
  QObject::connect(&m_timer, &QTimer::timeout, [this] {
    if(m_banner) {
      m_banner->dismiss();
    }
  });
  m_host->installEventFilter(m_filter.get());
}

BannerHost::~BannerHost() {
  m_host->removeEventFilter(m_filter.get());
  if(m_banner) {
    delete m_banner.data();
  }
}

void BannerHost::show(const QString& message) {
  m_pending = message;
  m_delay.start();
}

Banner* BannerHost::get_banner() const {
  if(!m_banner || m_banner->is_dismissed()) {
    return nullptr;
  }
  return m_banner.data();
}

void BannerHost::place() {
  if(!m_banner) {
    return;
  }
  auto width = std::min(Banner::MAXIMUM_WIDTH,
    std::max(0, m_host->width() - Banner::MARGIN));
  m_banner->setGeometry((m_host->width() - width) / 2,
    m_host->height() - Banner::HEIGHT, width, Banner::HEIGHT);
}

void BannerHost::reveal() {
  if(auto banner = get_banner()) {
    banner->set_message(m_pending);
  } else {
    m_banner = new Banner(m_pending, m_host);
  }
  place();
  m_banner->show();
  m_banner->raise();
  m_timer.start();
}

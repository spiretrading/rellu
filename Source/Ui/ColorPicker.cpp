#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <QApplication>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>
#include "Rellu/Ui/ColorPicker.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto WIDTH = 290;
  constexpr auto MARGIN = 16;
  constexpr auto SQUARE_SIZE = 176;
  constexpr auto SLIDER_WIDTH = 16;
  constexpr auto BAR_HEIGHT = 40;
  constexpr auto SWATCH_SIZE = 22;
  constexpr auto REMOVE_DISTANCE = 30;
  constexpr auto CHECKER = 4;
  const auto WHITE = QColor(0xFF, 0xFF, 0xFF);
  const auto MODELS = QStringList({"Hex", "RGB", "HSB"});
  const auto TYPES = QStringList({"Solid Color", "Linear Gradient",
    "Radial Gradient", "Angular Gradient"});

  QColor to_qcolor(const Color& color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  Color to_color(const QColor& color) {
    return Color(color.redF(), color.greenF(), color.blueF(), color.alphaF());
  }

  int to_byte(double component) {
    return std::clamp(static_cast<int>(std::lround(component * 255)), 0, 255);
  }

  QString to_hex(const Color& color) {
    return QString("#%1%2%3").arg(to_byte(color.m_red), 2, 16, QChar('0')).
      arg(to_byte(color.m_green), 2, 16, QChar('0')).
      arg(to_byte(color.m_blue), 2, 16, QChar('0')).toUpper();
  }

  std::optional<QColor> parse_hex(QString text) {
    text = text.trimmed();
    if(text.startsWith('#')) {
      text.remove(0, 1);
    }
    if(text.size() == 3) {
      text = QString("%1%1%2%2%3%3").arg(text[0]).arg(text[1]).arg(text[2]);
    }
    if(text.size() != 6) {
      return std::nullopt;
    }
    auto is_valid = false;
    auto rgb = text.toUInt(&is_valid, 16);
    if(!is_valid) {
      return std::nullopt;
    }
    return QColor(static_cast<int>((rgb >> 16) & 0xFF),
      static_cast<int>((rgb >> 8) & 0xFF), static_cast<int>(rgb & 0xFF));
  }

  std::optional<double> parse_number(QString text) {
    text = text.trimmed();
    if(text.endsWith('%')) {
      text.chop(1);
    }
    auto is_valid = false;
    auto number = text.trimmed().toDouble(&is_valid);
    if(!is_valid || !std::isfinite(number)) {
      return std::nullopt;
    }
    return number;
  }

  void paint_checker(QPainter& painter, const QRectF& rect) {
    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, WHITE);
    for(auto y = rect.top(); y < rect.bottom(); y += CHECKER) {
      auto row = static_cast<int>((y - rect.top()) / CHECKER);
      for(auto x = rect.left() + (row % 2) * CHECKER; x < rect.right();
          x += 2 * CHECKER) {
        painter.fillRect(QRectF(x, y, CHECKER, CHECKER),
          QColor(0xD8, 0xD8, 0xD8));
      }
    }
    painter.restore();
  }

  void paint_handle(QPainter& painter, QPointF center, double radius) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(WHITE, 2));
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
    painter.drawEllipse(center, radius + 1.5, radius + 1.5);
  }

  Color get_stop_color(
      const std::vector<GradientStop>& stops, double offset) {
    if(stops.empty()) {
      return Color(0, 0, 0, 1);
    } else if(offset <= stops.front().m_offset) {
      return stops.front().m_color;
    } else if(offset >= stops.back().m_offset) {
      return stops.back().m_color;
    }
    for(auto i = std::size_t(1); i != stops.size(); ++i) {
      auto& next = stops[i];
      if(offset <= next.m_offset) {
        auto& previous = stops[i - 1];
        auto span = next.m_offset - previous.m_offset;
        auto t = span > 0 ? (offset - previous.m_offset) / span : 0.0;
        auto mix = [&] (double from, double to) {
          return from + (to - from) * t;
        };
        return Color(mix(previous.m_color.m_red, next.m_color.m_red),
          mix(previous.m_color.m_green, next.m_color.m_green),
          mix(previous.m_color.m_blue, next.m_color.m_blue),
          mix(previous.m_color.m_alpha, next.m_color.m_alpha));
      }
    }
    return stops.back().m_color;
  }

  QLineEdit* make_field(const QString& name, int width) {
    auto field = new QLineEdit();
    field->setObjectName("number_field");
    field->setAccessibleName(translate("color_picker", name));
    field->setFixedWidth(width);
    return field;
  }

  class ChevronBox : public QComboBox {
    protected:
      void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Colors::ICON, 1.3));
        auto center = QPointF(width() - 10, height() / 2.0);
        painter.drawPolyline(QList<QPointF>({
          QPointF(center.x() - 4, center.y() - 2),
          QPointF(center.x(), center.y() + 2),
          QPointF(center.x() + 4, center.y() - 2)}));
      }
  };

  class ScreenOverlay : public QWidget {    public:
      struct Session {
        std::function<void (const Color&)> m_handler;
        std::function<void ()> m_cancel;
        std::vector<QPointer<ScreenOverlay>> m_overlays;
        bool m_is_done = false;

        void finish(std::optional<Color> color) {
          if(m_is_done) {
            return;
          }
          m_is_done = true;
          for(auto& overlay : m_overlays) {
            if(overlay) {
              overlay->close();
            }
          }
          if(color) {
            if(m_handler) {
              m_handler(*color);
            }
          } else if(m_cancel) {
            m_cancel();
          }
        }
      };

      ScreenOverlay(std::shared_ptr<Session> session, QScreen& screen)
          : QWidget(nullptr, Qt::FramelessWindowHint |
              Qt::WindowStaysOnTopHint | Qt::Tool),
            m_session(std::move(session)),
            m_image(screen.grabWindow(0).toImage()) {
        setAttribute(Qt::WA_DeleteOnClose);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);
        setGeometry(screen.geometry());
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.drawImage(rect(), m_image);
      }

      void mousePressEvent(QMouseEvent* event) override {
        if(event->button() != Qt::LeftButton) {
          m_session->finish(std::nullopt);
          return;
        }
        auto ratio = width() > 0 ?
          static_cast<double>(m_image.width()) / width() : 1.0;
        auto x = std::clamp(static_cast<int>(event->position().x() * ratio), 0,
          std::max(m_image.width() - 1, 0));
        auto y = std::clamp(static_cast<int>(event->position().y() * ratio), 0,
          std::max(m_image.height() - 1, 0));
        auto color = m_image.pixelColor(x, y);
        auto session = m_session;
        session->finish(make_color(color.red(), color.green(), color.blue()));
      }

      void keyPressEvent(QKeyEvent* event) override {
        if(event->key() == Qt::Key_Escape) {
          auto session = m_session;
          session->finish(std::nullopt);
          return;
        }
        QWidget::keyPressEvent(event);
      }

    private:
      std::shared_ptr<Session> m_session;
      QImage m_image;
  };
}

class ColorPicker::SaturationSquare : public QWidget {
  public:
    explicit SaturationSquare(ColorPicker& picker)
        : m_picker(&picker) {
      setObjectName("saturation_square");
      setFixedSize(SQUARE_SIZE, SQUARE_SIZE);
      setCursor(Qt::CrossCursor);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      auto bounds = QRectF(rect());
      painter.fillRect(bounds, QColor::fromHsvF(
        static_cast<float>(m_picker->m_hue / 360), 1, 1));
      auto horizontal = QLinearGradient(bounds.topLeft(), bounds.topRight());
      horizontal.setColorAt(0, WHITE);
      horizontal.setColorAt(1, QColor(255, 255, 255, 0));
      painter.fillRect(bounds, horizontal);
      auto vertical = QLinearGradient(bounds.topLeft(), bounds.bottomLeft());
      vertical.setColorAt(0, QColor(0, 0, 0, 0));
      vertical.setColorAt(1, QColor(0, 0, 0));
      painter.fillRect(bounds, vertical);
      paint_handle(painter, QPointF(m_picker->m_saturation * width(),
        (1 - m_picker->m_value) * height()), 6);
    }

    void mousePressEvent(QMouseEvent* event) override {
      pick(event->position());
    }

    void mouseMoveEvent(QMouseEvent* event) override {
      if(event->buttons() & Qt::LeftButton) {
        pick(event->position());
      }
    }

  private:
    ColorPicker* m_picker;

    void pick(QPointF position) {
      auto saturation = std::clamp(position.x() / width(), 0.0, 1.0);
      auto value = std::clamp(1 - position.y() / height(), 0.0, 1.0);
      m_picker->set_hsv(
        m_picker->m_hue, saturation, value, m_picker->m_alpha);
      m_picker->store_color();
    }
};

class ColorPicker::ChannelSlider : public QWidget {
  public:
    ChannelSlider(ColorPicker& picker, bool is_hue)
        : m_picker(&picker),
          m_is_hue(is_hue) {
      setObjectName(is_hue ? "hue_slider" : "alpha_slider");
      setFixedSize(SLIDER_WIDTH, SQUARE_SIZE);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      auto bounds = QRectF(rect()).adjusted(2, 0, -2, 0);
      auto gradient = QLinearGradient(bounds.topLeft(), bounds.bottomLeft());
      if(m_is_hue) {
        for(auto i = 0; i <= 6; ++i) {
          gradient.setColorAt(i / 6.0, QColor::fromHsvF(
            static_cast<float>(std::fmod(1 - i / 6.0, 1.0)), 1, 1));
        }
      } else {
        paint_checker(painter, bounds);
        auto color = QColor::fromHsvF(static_cast<float>(m_picker->m_hue / 360),
          static_cast<float>(m_picker->m_saturation),
          static_cast<float>(m_picker->m_value));
        gradient.setColorAt(0, color);
        color.setAlpha(0);
        gradient.setColorAt(1, color);
      }
      painter.fillRect(bounds, gradient);
      auto fraction = m_is_hue ? 1 - m_picker->m_hue / 360 :
        1 - m_picker->m_alpha;
      paint_handle(painter, QPointF(width() / 2.0,
        std::clamp(fraction * height(), 6.0, height() - 6.0)), 5);
    }

    void mousePressEvent(QMouseEvent* event) override {
      pick(event->position());
    }

    void mouseMoveEvent(QMouseEvent* event) override {
      if(event->buttons() & Qt::LeftButton) {
        pick(event->position());
      }
    }

  private:
    ColorPicker* m_picker;
    bool m_is_hue;

    void pick(QPointF position) {
      auto fraction = std::clamp(position.y() / height(), 0.0, 1.0);
      if(m_is_hue) {
        m_picker->set_hsv((1 - fraction) * 360, m_picker->m_saturation,
          m_picker->m_value, m_picker->m_alpha);
      } else {
        m_picker->set_hsv(m_picker->m_hue, m_picker->m_saturation,
          m_picker->m_value, 1 - fraction);
      }
      m_picker->store_color();
    }
};

class ColorPicker::GradientBar : public QWidget {
  public:
    explicit GradientBar(ColorPicker& picker)
        : m_picker(&picker),
          m_dragged(-1),
          m_is_removing(false) {
      setObjectName("gradient_bar");
      setFixedHeight(BAR_HEIGHT);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
      auto gradient = std::get_if<GradientFill>(&m_picker->m_fill);
      if(!gradient) {
        return;
      }
      auto painter = QPainter(this);
      painter.setRenderHint(QPainter::Antialiasing);
      auto bar = get_bar();
      paint_checker(painter, bar);
      auto paint = QLinearGradient(bar.topLeft(), bar.topRight());
      for(auto& stop : gradient->m_stops) {
        paint.setColorAt(
          std::clamp(stop.m_offset, 0.0, 1.0), to_qcolor(stop.m_color));
      }
      painter.fillRect(bar, paint);
      painter.setPen(QPen(Colors::BORDER, 1));
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(bar);
      for(auto i = 0; i != static_cast<int>(gradient->m_stops.size()); ++i) {
        if(i == m_dragged && m_is_removing) {
          continue;
        }
        auto& stop = gradient->m_stops[i];
        auto handle = get_handle(stop.m_offset);
        paint_checker(painter, handle);
        painter.fillRect(handle, to_qcolor(stop.m_color));
        auto is_selected = i == m_picker->m_stop;
        painter.setPen(QPen(is_selected ? Colors::SELECTION : WHITE,
          is_selected ? 2.0 : 1.5));
        painter.drawRect(handle);
      }
    }

    void mousePressEvent(QMouseEvent* event) override {
      auto gradient = std::get_if<GradientFill>(&m_picker->m_fill);
      if(!gradient || event->button() != Qt::LeftButton) {
        return;
      }
      auto position = event->position();
      for(auto i = 0; i != static_cast<int>(gradient->m_stops.size()); ++i) {
        if(get_handle(gradient->m_stops[i].m_offset).adjusted(
            -2, -4, 2, 4).contains(position)) {
          m_dragged = i;
          m_is_removing = false;
          m_picker->set_stop(i);
          return;
        }
      }
      if(position.y() > get_bar().bottom() + 4) {
        return;
      }
      auto offset = get_offset(position.x());
      auto stop =
        GradientStop(offset, get_stop_color(gradient->m_stops, offset));
      auto i = static_cast<int>(std::upper_bound(gradient->m_stops.begin(),
        gradient->m_stops.end(), offset, [] (double offset, const auto& stop) {
          return offset < stop.m_offset;
        }) - gradient->m_stops.begin());
      gradient->m_stops.insert(gradient->m_stops.begin() + i, stop);
      m_dragged = i;
      m_is_removing = false;
      m_picker->set_stop(i);
      m_picker->report();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
      auto gradient = std::get_if<GradientFill>(&m_picker->m_fill);
      if(!gradient || m_dragged < 0 ||
          m_dragged >= static_cast<int>(gradient->m_stops.size())) {
        return;
      }
      auto position = event->position();
      m_is_removing = gradient->m_stops.size() > 2 &&
        std::abs(position.y() - height() / 2.0) > REMOVE_DISTANCE;
      auto stop = gradient->m_stops[m_dragged];
      stop.m_offset = get_offset(position.x());
      gradient->m_stops.erase(gradient->m_stops.begin() + m_dragged);
      auto i = static_cast<int>(std::upper_bound(gradient->m_stops.begin(),
        gradient->m_stops.end(), stop.m_offset,
        [] (double offset, const auto& stop) {
          return offset < stop.m_offset;
        }) - gradient->m_stops.begin());
      gradient->m_stops.insert(gradient->m_stops.begin() + i, stop);
      m_dragged = i;
      m_picker->m_stop = i;
      update();
      if(!m_is_removing) {
        m_picker->report();
      }
    }

    void mouseReleaseEvent(QMouseEvent*) override {
      auto gradient = std::get_if<GradientFill>(&m_picker->m_fill);
      if(gradient && m_is_removing && m_dragged >= 0 &&
          m_dragged < static_cast<int>(gradient->m_stops.size()) &&
          gradient->m_stops.size() > 2) {
        gradient->m_stops.erase(gradient->m_stops.begin() + m_dragged);
        m_picker->set_stop(std::min(m_dragged,
          static_cast<int>(gradient->m_stops.size()) - 1));
        m_picker->report();
      }
      m_dragged = -1;
      m_is_removing = false;
      update();
    }

  private:
    ColorPicker* m_picker;
    int m_dragged;
    bool m_is_removing;

    QRectF get_bar() const {
      return QRectF(6.5, 4.5, width() - 13, 16);
    }

    QRectF get_handle(double offset) const {
      auto bar = get_bar();
      auto x = bar.left() + std::clamp(offset, 0.0, 1.0) * bar.width();
      return QRectF(x - 5, bar.bottom() + 4, 10, 10);
    }

    double get_offset(double x) const {
      auto bar = get_bar();
      return std::clamp((x - bar.left()) / bar.width(), 0.0, 1.0);
    }
};

ColorPicker::ColorPicker(QWidget* parent)
    : QFrame(parent, Qt::Popup),
      m_fill(SolidFill()),
      m_stop(0),
      m_hue(0),
      m_saturation(0),
      m_value(0),
      m_alpha(1),
      m_is_gradient_enabled(true),
      m_is_picking(false),
      m_is_updating(false),
      m_is_canvas_dragging(false),
      m_model(ColorModel::HEX),
      m_type(new ChevronBox()),
      m_bar(new GradientBar(*this)),
      m_square(new SaturationSquare(*this)),
      m_hue_slider(new ChannelSlider(*this, true)),
      m_alpha_slider(new ChannelSlider(*this, false)),
      m_model_box(new ChevronBox()),
      m_hex(new QLineEdit()),
      m_opacity(make_field("Opacity", 44)),
      m_swatch_row(new QWidget()),
      m_add(nullptr) {
  setObjectName("color_picker");
  setAttribute(Qt::WA_StyledBackground);
  setStyleSheet("QFrame#color_picker { background: #FFFFFF; "
    "border: 1px solid #D0D0D0; }");
  setFixedWidth(WIDTH);
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(MARGIN, 12, MARGIN, 14);
  layout->setSpacing(12);
  m_type->setObjectName("underlined_choice");
  m_type->setAccessibleName(translate("color_picker", "Fill Type"));
  for(auto& type : TYPES) {
    m_type->addItem(translate("color_picker", type));
  }
  m_type->setFixedWidth(150);
  QObject::connect(m_type, &QComboBox::activated, this, [this] (int index) {
    apply_type(index);
  });
  layout->addWidget(m_type);
  layout->addWidget(m_bar);
  auto sliders = new QWidget();
  auto sliders_layout = new QHBoxLayout(sliders);
  sliders_layout->setContentsMargins(0, 0, 0, 0);
  sliders_layout->setSpacing(14);
  sliders_layout->addWidget(m_square);
  sliders_layout->addWidget(m_hue_slider);
  sliders_layout->addWidget(m_alpha_slider);
  sliders_layout->addStretch(1);
  layout->addWidget(sliders);
  auto values = new QWidget();
  auto values_layout = new QHBoxLayout(values);
  values_layout->setContentsMargins(0, 0, 0, 0);
  values_layout->setSpacing(8);
  m_model_box->setObjectName("underlined_choice");
  m_model_box->setAccessibleName(translate("color_picker", "Color Model"));
  for(auto& model : MODELS) {
    m_model_box->addItem(translate("color_picker", model));
  }
  m_model_box->setFixedWidth(54);
  QObject::connect(m_model_box, &QComboBox::activated, this,
    [this] (int index) {
      apply_model(static_cast<ColorModel>(index));
    });
  values_layout->addWidget(m_model_box);
  m_hex->setObjectName("number_field");
  m_hex->setAccessibleName(translate("color_picker", "Hex"));
  values_layout->addWidget(m_hex, 1);
  QObject::connect(m_hex, &QLineEdit::editingFinished, this, [this] {
    commit_fields();
  });
  for(auto name : {"Red", "Green", "Blue"}) {
    auto field = make_field(name, 30);
    values_layout->addWidget(field);
    m_components.push_back(field);
    QObject::connect(field, &QLineEdit::editingFinished, this, [this] {
      commit_fields();
    });
  }
  values_layout->addWidget(m_opacity);
  QObject::connect(m_opacity, &QLineEdit::editingFinished, this, [this] {
    commit_fields();
  });
  auto dropper = new QToolButton();
  dropper->setObjectName("icon_toggle");
  dropper->setAutoRaise(true);
  dropper->setFocusPolicy(Qt::NoFocus);
  dropper->setIcon(get_icon(Glyph::EYEDROPPER));
  dropper->setIconSize(QSize(18, 18));
  dropper->setFixedSize(26, 26);
  dropper->setToolTip(translate("color_picker", "Pick color from screen"));
  dropper->setAccessibleName(dropper->toolTip());
  QObject::connect(dropper, &QToolButton::clicked, this, [this] {
    pick_from_screen();
  });
  values_layout->addWidget(dropper);
  layout->addWidget(values);
  layout->addWidget(m_swatch_row);
  rebuild_swatches();
  update_type();
  load_color();
}

const Fill& ColorPicker::get_fill() const {
  return m_fill;
}

void ColorPicker::set_fill(const Fill& fill) {
  m_fill = fill;
  if(auto gradient = std::get_if<GradientFill>(&m_fill)) {
    m_stop = std::clamp(m_stop, 0,
      std::max(static_cast<int>(gradient->m_stops.size()) - 1, 0));
  } else {
    m_stop = 0;
  }
  update_type();
  load_color();
}

Color ColorPicker::get_color() const {
  if(auto solid = std::get_if<SolidFill>(&m_fill)) {
    return solid->m_color;
  } else if(auto gradient = std::get_if<GradientFill>(&m_fill)) {
    if(m_stop >= 0 && m_stop < static_cast<int>(gradient->m_stops.size())) {
      return gradient->m_stops[m_stop].m_color;
    }
  } else if(std::holds_alternative<ImageFill>(m_fill)) {
    return Color(1, 1, 1, 1);
  }
  return to_color(QColor::fromHsvF(static_cast<float>(m_hue / 360),
    static_cast<float>(m_saturation), static_cast<float>(m_value),
    static_cast<float>(m_alpha)));
}

void ColorPicker::apply_color(const Color& color) {
  auto value = to_qcolor(color);
  auto hue = value.hsvHueF();
  set_hsv(hue < 0 ? m_hue : hue * 360, value.hsvSaturationF(),
    value.valueF(), color.m_alpha);
  write_color(color);
}

void ColorPicker::apply_type(int index) {
  auto color = get_color();
  if(index == 0) {
    if(std::holds_alternative<SolidFill>(m_fill)) {
      return;
    }
    m_fill = SolidFill(color);
  } else if(index >= 1 && index <= 3) {
    auto gradient = GradientFill();
    auto is_new = false;
    if(auto current = std::get_if<GradientFill>(&m_fill)) {
      gradient = *current;
    } else {
      gradient.m_stops = make_default_stops(color);
      is_new = true;
    }
    if(index == 1 && (is_new ||
        !std::holds_alternative<LinearGradient>(gradient.m_geometry))) {
      gradient.m_geometry = LinearGradient(Point(0.5, 0), Point(0.5, 1));
      gradient.m_units = GradientUnits::OBJECT_BOUNDING_BOX;
      gradient.m_transform = Matrix();
    } else if(index == 2 && (is_new ||
        !std::holds_alternative<RadialGradient>(gradient.m_geometry))) {
      gradient.m_geometry = RadialGradient();
      gradient.m_units = GradientUnits::OBJECT_BOUNDING_BOX;
      gradient.m_transform = Matrix();
    } else if(index == 3 && (is_new ||
        !std::holds_alternative<AngularGradient>(gradient.m_geometry))) {
      gradient.m_geometry = AngularGradient();
      gradient.m_units = GradientUnits::OBJECT_BOUNDING_BOX;
      gradient.m_transform = Matrix();
    } else if(std::holds_alternative<GradientFill>(m_fill)) {
      return;
    }
    m_fill = std::move(gradient);
    m_stop = 0;
  } else {
    return;
  }
  update_type();
  load_color();
  report();
}

bool ColorPicker::is_gradient_enabled() const {
  return m_is_gradient_enabled;
}

void ColorPicker::set_gradient_enabled(bool is_enabled) {
  m_is_gradient_enabled = is_enabled;
  update_type();
}

int ColorPicker::get_stop() const {
  return m_stop;
}

void ColorPicker::set_stop(int index) {
  m_stop = index;
  load_color();
  m_bar->update();
}

ColorModel ColorPicker::get_model() const {
  return m_model;
}

void ColorPicker::set_model(ColorModel model) {
  m_model = model;
  m_model_box->setCurrentIndex(static_cast<int>(model));
  update_fields();
}

void ColorPicker::apply_model(ColorModel model) {
  set_model(model);
  if(m_model_handler) {
    m_model_handler(model);
  }
}

void ColorPicker::set_model_handler(ModelHandler handler) {
  m_model_handler = std::move(handler);
}

void ColorPicker::set_swatches(std::vector<ColorSwatch> swatches) {
  m_swatches = std::move(swatches);
  rebuild_swatches();
}

void ColorPicker::set_change_handler(ChangeHandler handler) {
  m_change_handler = std::move(handler);
}

void ColorPicker::set_add_handler(AddHandler handler) {
  m_add_handler = std::move(handler);
  if(m_add) {
    m_add->setEnabled(static_cast<bool>(m_add_handler));
  }
}

void ColorPicker::set_close_handler(CloseHandler handler) {
  m_close_handler = std::move(handler);
}

void ColorPicker::popup(const QRect& anchor) {
  adjustSize();
  auto screen = QGuiApplication::screenAt(anchor.center());
  if(!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  auto available = screen ? screen->availableGeometry() : QRect();
  auto position = QPoint(anchor.left() - width() - 8,
    anchor.center().y() - height() / 2);
  if(!available.isEmpty()) {
    position.setX(std::max(position.x(), available.left()));
    position.setY(std::clamp(position.y(), available.top(),
      std::max(available.bottom() - height(), available.top())));
  }
  move(position);
  show();
  raise();
}

void ColorPicker::apply_fill(const Fill& fill) {
  set_fill(fill);
  report();
}

void ColorPicker::set_canvas_handler(CanvasHandler handler) {
  m_canvas_handler = std::move(handler);
}

void ColorPicker::set_key_handler(KeyHandler handler) {
  m_key_handler = std::move(handler);
}

bool ColorPicker::event(QEvent* event) {
  if(event->type() == QEvent::KeyPress &&
      handle_key(*static_cast<QKeyEvent*>(event))) {
    return true;
  }
  return QFrame::event(event);
}

void ColorPicker::mousePressEvent(QMouseEvent* event) {
  if(!rect().contains(event->position().toPoint()) && m_canvas_handler) {
    if(auto focus = focusWidget(); focus && focus != this) {
      focus->clearFocus();
    }
    if(m_canvas_handler(CanvasEvent::PRESS, event->globalPosition())) {
      m_is_canvas_dragging = true;
      event->accept();
      return;
    }
  }
  QFrame::mousePressEvent(event);
}

void ColorPicker::mouseMoveEvent(QMouseEvent* event) {
  if(m_is_canvas_dragging) {
    m_canvas_handler(CanvasEvent::MOVE, event->globalPosition());
    event->accept();
    return;
  }
  QFrame::mouseMoveEvent(event);
}

void ColorPicker::mouseReleaseEvent(QMouseEvent* event) {
  if(m_is_canvas_dragging) {
    m_is_canvas_dragging = false;
    m_canvas_handler(CanvasEvent::RELEASE, event->globalPosition());
    event->accept();
    return;
  }
  QFrame::mouseReleaseEvent(event);
}

void ColorPicker::hideEvent(QHideEvent* event) {
  QFrame::hideEvent(event);
  if(!m_is_picking && m_close_handler) {
    m_close_handler();
  }
}

void ColorPicker::keyPressEvent(QKeyEvent* event) {
  if(event->key() == Qt::Key_Escape) {
    hide();
    return;
  }
  QFrame::keyPressEvent(event);
}

bool ColorPicker::handle_key(QKeyEvent& event) {
  if(!m_key_handler) {
    return false;
  }
  auto focus = focusWidget();
  if((focus && focus != this) || !m_key_handler(event)) {
    return false;
  }
  event.accept();
  return true;
}

void ColorPicker::load_color() {
  auto color = to_qcolor(get_color());
  auto hue = color.hsvHueF();
  m_hue = hue < 0 ? m_hue : hue * 360;
  m_saturation = color.hsvSaturationF();
  m_value = color.valueF();
  m_alpha = color.alphaF();
  update_fields();
  m_square->update();
  m_hue_slider->update();
  m_alpha_slider->update();
  m_bar->update();
}

void ColorPicker::set_hsv(
    double hue, double saturation, double value, double alpha) {
  m_hue = std::clamp(hue, 0.0, 360.0);
  m_saturation = std::clamp(saturation, 0.0, 1.0);
  m_value = std::clamp(value, 0.0, 1.0);
  m_alpha = std::clamp(alpha, 0.0, 1.0);
}

void ColorPicker::store_color() {
  auto color = to_color(QColor::fromHsvF(
    static_cast<float>(std::fmod(m_hue, 360.0) / 360),
    static_cast<float>(m_saturation), static_cast<float>(m_value),
    static_cast<float>(m_alpha)));
  color.m_alpha = m_alpha;
  write_color(color);
}

void ColorPicker::write_color(const Color& color) {
  if(auto solid = std::get_if<SolidFill>(&m_fill)) {
    solid->m_color = color;
    solid->m_swatch = SwatchId();
  } else if(auto gradient = std::get_if<GradientFill>(&m_fill)) {
    if(m_stop >= 0 && m_stop < static_cast<int>(gradient->m_stops.size())) {
      gradient->m_stops[m_stop].m_color = color;
    }
  } else {
    m_fill = SolidFill(color);
    update_type();
  }
  update_fields();
  m_square->update();
  m_hue_slider->update();
  m_alpha_slider->update();
  m_bar->update();
  report();
}

void ColorPicker::update_fields() {
  m_is_updating = true;
  auto color = to_color(QColor::fromHsvF(
    static_cast<float>(std::fmod(m_hue, 360.0) / 360),
    static_cast<float>(m_saturation), static_cast<float>(m_value)));
  m_hex->setVisible(m_model == ColorModel::HEX);
  for(auto field : m_components) {
    field->setVisible(m_model != ColorModel::HEX);
  }
  m_hex->setText(to_hex(color));
  if(m_model == ColorModel::RGB) {
    m_components[0]->setText(QString::number(to_byte(color.m_red)));
    m_components[1]->setText(QString::number(to_byte(color.m_green)));
    m_components[2]->setText(QString::number(to_byte(color.m_blue)));
  } else if(m_model == ColorModel::HSB) {
    m_components[0]->setText(QString::number(std::lround(m_hue) % 360));
    m_components[1]->setText(QString::number(std::lround(m_saturation * 100)));
    m_components[2]->setText(QString::number(std::lround(m_value * 100)));
  }
  m_opacity->setText(QString("%1%").arg(std::lround(m_alpha * 100)));
  m_is_updating = false;
}

void ColorPicker::update_type() {
  m_type->setVisible(m_is_gradient_enabled);
  auto index = 0;
  if(auto gradient = std::get_if<GradientFill>(&m_fill)) {
    index = static_cast<int>(gradient->m_geometry.index()) + 1;
  }
  m_type->setCurrentIndex(index);
  m_bar->setVisible(std::holds_alternative<GradientFill>(m_fill));
  adjustSize();
}

void ColorPicker::rebuild_swatches() {
  if(auto layout = m_swatch_row->layout()) {
    while(auto item = layout->takeAt(0)) {
      delete item->widget();
      delete item;
    }
    delete layout;
  }
  auto layout = new QGridLayout(m_swatch_row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);
  auto columns = (WIDTH - 2 * MARGIN) / (SWATCH_SIZE + 6);
  auto add = new QToolButton();
  add->setObjectName("text_button");
  add->setFocusPolicy(Qt::NoFocus);
  add->setIcon(get_icon(Glyph::PLUS));
  add->setIconSize(QSize(18, 18));
  add->setFixedSize(SWATCH_SIZE + 4, SWATCH_SIZE + 4);
  add->setToolTip(
    translate("color_picker", "Add color to Document Assets"));
  add->setAccessibleName(translate("color_picker", "Add to Assets"));
  add->setEnabled(static_cast<bool>(m_add_handler));
  QObject::connect(add, &QToolButton::clicked, this, [this] {
    if(m_add_handler) {
      m_add_handler(m_fill);
    }
  });
  layout->addWidget(add, 0, 0);
  m_add = add;
  auto index = 1;
  for(auto& swatch : m_swatches) {
    auto button = new QToolButton();
    button->setObjectName("swatch");
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(SWATCH_SIZE + 4, SWATCH_SIZE + 4);
    button->setIconSize(QSize(SWATCH_SIZE, SWATCH_SIZE));
    auto ratio = devicePixelRatioF();
    auto pixmap = QPixmap(static_cast<int>(SWATCH_SIZE * ratio),
      static_cast<int>(SWATCH_SIZE * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    {
      auto painter = QPainter(&pixmap);
      auto bounds = QRectF(0.5, 0.5, SWATCH_SIZE - 1, SWATCH_SIZE - 1);
      paint_checker(painter, bounds);
      if(swatch.m_gradient.empty()) {
        painter.fillRect(bounds, to_qcolor(swatch.m_color));
      } else {
        auto gradient = QLinearGradient(bounds.topLeft(), bounds.topRight());
        for(auto& stop : swatch.m_gradient) {
          gradient.setColorAt(
            std::clamp(stop.m_offset, 0.0, 1.0), to_qcolor(stop.m_color));
        }
        painter.fillRect(bounds, gradient);
      }
      painter.setPen(QPen(Colors::BORDER, 1));
      painter.drawRect(bounds);
    }
    button->setIcon(QIcon(pixmap));
    auto label = swatch.m_name.empty() ? to_hex(swatch.m_color) :
      QString::fromStdString(swatch.m_name);
    button->setToolTip(label);
    button->setAccessibleName(label);
    QObject::connect(button, &QToolButton::clicked, this, [this, swatch] {
      if(!swatch.m_gradient.empty() && m_is_gradient_enabled) {
        auto gradient = GradientFill();
        if(auto current = std::get_if<GradientFill>(&m_fill)) {
          gradient = *current;
        } else {
          gradient.m_geometry = LinearGradient(Point(0.5, 0), Point(0.5, 1));
        }
        gradient.m_stops = swatch.m_gradient;
        m_fill = std::move(gradient);
        m_stop = 0;
        update_type();
        load_color();
        report();
        return;
      }
      auto color = swatch.m_gradient.empty() ? swatch.m_color :
        swatch.m_gradient.front().m_color;
      apply_color(color);
      if(auto solid = std::get_if<SolidFill>(&m_fill); solid &&
          swatch.m_gradient.empty()) {
        solid->m_swatch = swatch.m_id;
        report();
      }
    });
    layout->addWidget(button, index / columns, index % columns);
    ++index;
  }
  layout->setColumnStretch(columns, 1);
}

void ColorPicker::report() {
  if(m_change_handler) {
    m_change_handler(m_fill);
  }
}

void ColorPicker::commit_fields() {
  if(m_is_updating) {
    return;
  }
  auto current = get_color();
  auto alpha = current.m_alpha;
  if(auto opacity = parse_number(m_opacity->text())) {
    alpha = std::clamp(*opacity, 0.0, 100.0) / 100;
  }
  auto values = std::vector<std::optional<double>>();
  for(auto field : m_components) {
    values.push_back(parse_number(field->text()));
  }
  auto is_complete = values[0] && values[1] && values[2];
  if(m_model == ColorModel::HSB && is_complete) {
    auto hue = std::fmod(std::clamp(*values[0], 0.0, 360.0), 360.0);
    auto saturation = std::clamp(*values[1], 0.0, 100.0);
    auto brightness = std::clamp(*values[2], 0.0, 100.0);
    if(std::lround(hue) != std::lround(m_hue) % 360 ||
        std::lround(saturation) != std::lround(m_saturation * 100) ||
        std::lround(brightness) != std::lround(m_value * 100)) {
      set_hsv(hue, saturation / 100, brightness / 100, alpha);
      store_color();
      return;
    }
  }
  auto red = to_byte(current.m_red);
  auto green = to_byte(current.m_green);
  auto blue = to_byte(current.m_blue);
  if(m_model == ColorModel::HEX) {
    if(auto parsed = parse_hex(m_hex->text())) {
      red = parsed->red();
      green = parsed->green();
      blue = parsed->blue();
    }
  } else if(m_model == ColorModel::RGB && is_complete) {
    red = std::clamp(static_cast<int>(std::lround(*values[0])), 0, 255);
    green = std::clamp(static_cast<int>(std::lround(*values[1])), 0, 255);
    blue = std::clamp(static_cast<int>(std::lround(*values[2])), 0, 255);
  }
  auto result = current;
  if(red != to_byte(current.m_red) || green != to_byte(current.m_green) ||
      blue != to_byte(current.m_blue)) {
    result = make_color(red, green, blue);
  }
  result.m_alpha = alpha;
  if(result == current) {
    update_fields();
    return;
  }
  apply_color(result);
}
void ColorPicker::pick_from_screen() {
  m_is_picking = true;
  auto geometry = this->geometry();
  hide();
  auto self = QPointer<ColorPicker>(this);
  auto restore = [self, geometry] {
    if(!self) {
      return;
    }
    self->setGeometry(geometry);
    self->show();
    self->raise();
    self->m_is_picking = false;
  };
  pick_screen_color([self, restore] (const Color& color) {
    restore();
    if(self) {
      auto picked = color;
      picked.m_alpha = self->m_alpha;
      self->apply_color(picked);
    }
  }, restore);
}

std::vector<GradientStop> Rellu::make_default_stops(const Color& color) {
  auto halve = [] (double component) {
    return std::floor(component * 255 / 2 + 0.5) / 255;
  };
  return {GradientStop(0, color), GradientStop(1, Color(halve(color.m_red),
    halve(color.m_green), halve(color.m_blue), color.m_alpha))};
}

void Rellu::pick_screen_color(std::function<void (const Color& color)> handler,
    std::function<void ()> cancel) {
  auto session = std::make_shared<ScreenOverlay::Session>();
  session->m_handler = std::move(handler);
  session->m_cancel = std::move(cancel);
  for(auto screen : QGuiApplication::screens()) {
    auto overlay = new ScreenOverlay(session, *screen);
    session->m_overlays.push_back(overlay);
  }
  for(auto& overlay : session->m_overlays) {
    overlay->show();
    overlay->raise();
  }
  if(!session->m_overlays.empty()) {
    session->m_overlays.front()->activateWindow();
    session->m_overlays.front()->setFocus();
  }
}

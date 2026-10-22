#include <utility>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include "Rellu/Ui/Banner.hpp"
#include "Rellu/Ui/XdAlert.hpp"

using namespace Rellu;

namespace {
  constexpr auto PANEL_WIDTH = 544;
  constexpr auto ICON_SIZE = 36;
  const auto BACKDROP = QColor(0, 0, 0, 140);
  const auto WARNING = QColor(0xEC, 0x10, 0x14);
  const auto STYLE = QString(R"(
QFrame#xd_alert_panel {
  background: #F5F5F5;
  border: 1px solid #DDDDDD;
}
QLabel#xd_alert_message {
  color: #3F3F3F;
  font-size: 12px;
  font-weight: bold;
}
QLabel#xd_alert_explanation, QLabel#xd_alert_link {
  color: #3F3F3F;
  font-size: 12px;
}
QPushButton#xd_alert_button {
  background: transparent;
  border: none;
  color: #707070;
  font-size: 12px;
  font-weight: bold;
  padding: 4px 0px;
}
QPushButton#xd_alert_button[primary="true"] {
  color: #4B4B4B;
}
)");

  QString strip_links(const QString& text) {
    auto result = QString();
    for(auto& span : parse_banner_message(text)) {
      result += span.m_text;
    }
    return result;
  }

  class WarningIcon : public QWidget {
    public:
      WarningIcon() {
        setFixedSize(ICON_SIZE, ICON_SIZE);
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto triangle = QPainterPath();
        triangle.moveTo(ICON_SIZE / 2.0, 3);
        triangle.lineTo(ICON_SIZE - 3, ICON_SIZE - 5);
        triangle.lineTo(3, ICON_SIZE - 5);
        triangle.closeSubpath();
        painter.setPen(QPen(WARNING, 3, Qt::SolidLine, Qt::RoundCap,
          Qt::RoundJoin));
        painter.setBrush(WARNING);
        painter.drawPath(triangle);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(ICON_SIZE / 2.0 - 1.75, 12, 3.5, 11),
          1.5, 1.5);
        painter.drawEllipse(QPointF(ICON_SIZE / 2.0, 27), 2, 2);
      }
  };
}

XdAlert::XdAlert(XdAlertContent content, QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog),
      m_content(std::move(content)),
      m_choice(m_content.m_default) {
  setObjectName("xd_alert");
  setAttribute(Qt::WA_TranslucentBackground);
  setModal(true);
  setStyleSheet(STYLE);
  auto outer = new QVBoxLayout(this);
  outer->setAlignment(Qt::AlignCenter);
  auto panel = new QFrame();
  panel->setObjectName("xd_alert_panel");
  panel->setFixedWidth(PANEL_WIDTH);
  auto shadow = new QGraphicsDropShadowEffect(panel);
  shadow->setBlurRadius(16);
  shadow->setOffset(0, 2);
  shadow->setColor(QColor(0, 0, 0, 70));
  panel->setGraphicsEffect(shadow);
  outer->addWidget(panel);
  auto columns = new QHBoxLayout(panel);
  columns->setContentsMargins(34, 34, 27, 24);
  columns->setSpacing(29);
  auto icon = new WarningIcon();
  columns->addWidget(icon, 0, Qt::AlignTop);
  auto rows = new QVBoxLayout();
  rows->setSpacing(10);
  columns->addLayout(rows, 1);
  auto message = new QLabel(m_content.m_message);
  message->setObjectName("xd_alert_message");
  message->setWordWrap(true);
  rows->addWidget(message);
  if(!m_content.m_explanation.isEmpty()) {
    auto explanation = new QLabel(m_content.m_explanation);
    explanation->setObjectName("xd_alert_explanation");
    explanation->setWordWrap(true);
    rows->addWidget(explanation);
  }
  if(!m_content.m_link.isEmpty()) {
    auto link = new QLabel(strip_links(m_content.m_link));
    link->setObjectName("xd_alert_link");
    link->setWordWrap(true);
    rows->addWidget(link);
  }
  rows->addSpacing(16);
  auto buttons = new QHBoxLayout();
  buttons->setSpacing(32);
  buttons->addStretch(1);
  for(auto i = 0; i != static_cast<int>(m_content.m_buttons.size()); ++i) {
    auto button = new QPushButton(m_content.m_buttons[i]);
    button->setObjectName("xd_alert_button");
    button->setProperty("primary", i == m_content.m_default);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoDefault(false);
    connect(button, &QPushButton::clicked, this, [this, i] {
      m_choice = i;
      accept();
    });
    buttons->addWidget(button);
    m_buttons.push_back(button);
  }
  rows->addLayout(buttons);
}

const XdAlertContent& XdAlert::get_content() const {
  return m_content;
}

const std::vector<QPushButton*>& XdAlert::get_buttons() const {
  return m_buttons;
}

int XdAlert::get_choice() const {
  return m_choice;
}

void XdAlert::paintEvent(QPaintEvent* event) {
  auto painter = QPainter(this);
  painter.fillRect(rect(), BACKDROP);
}

void XdAlert::showEvent(QShowEvent* event) {
  if(auto parent = parentWidget()) {
    auto window = parent->window();
    setGeometry(QRect(window->mapToGlobal(QPoint(0, 0)), window->size()));
  }
  QDialog::showEvent(event);
}

void XdAlert::keyPressEvent(QKeyEvent* event) {
  if(event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    m_choice = m_content.m_default;
    accept();
    return;
  }
  QDialog::keyPressEvent(event);
}

int Rellu::show_xd_alert(XdAlertContent content, QWidget* parent) {
  auto alert = XdAlert(std::move(content), parent);
  alert.exec();
  return alert.get_choice();
}

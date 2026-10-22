#include <cmath>
#include <optional>
#include <tuple>
#include <utility>
#include <QColor>
#include <QEvent>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/FeatureCards.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto ICON_BUTTON_SIZE = 32;
  constexpr auto ICON_SIZE = 20;
  constexpr auto APP_BAR_MARGIN = 8;
  constexpr auto WORKSPACE_LEFT = 66;
  constexpr auto WORKSPACE_SPACING = 32;
  constexpr auto WORKSPACE_BASELINE = 76.0 / 3;
  constexpr auto WORKSPACE_TEXT_LEFT = -2.0 / 3;
  constexpr auto INDICATOR_HEIGHT = 2;
  const auto WORKSPACE_COLOR = QColor(0x8E, 0x8E, 0x8E);
  const auto HOVERED_WORKSPACE_COLOR = QColor(0x6E, 0x6E, 0x6E);
  const auto CURRENT_WORKSPACE_COLOR = QColor(0x3F, 0x3F, 0x3F);

  class WorkspaceButton : public QToolButton {
    public:
      QSize sizeHint() const override {
        return QSize(static_cast<int>(std::lround(get_advance())),
          AppBar::HEIGHT - INDICATOR_HEIGHT);
      }

      QSize minimumSizeHint() const override {
        return sizeHint();
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setFont(font());
        if(isChecked()) {
          painter.setPen(CURRENT_WORKSPACE_COLOR);
        } else if(underMouse()) {
          painter.setPen(HOVERED_WORKSPACE_COLOR);
        } else {
          painter.setPen(WORKSPACE_COLOR);
        }
        painter.drawText(
          QPointF(WORKSPACE_TEXT_LEFT, WORKSPACE_BASELINE), text());
        if(isChecked()) {
          painter.setRenderHint(QPainter::Antialiasing);
          painter.fillRect(QRectF(0, height() - INDICATOR_HEIGHT,
            get_advance(), INDICATOR_HEIGHT), CURRENT_WORKSPACE_COLOR);
        }
      }

    private:
      double get_advance() const {
        return QFontMetricsF(font()).horizontalAdvance(text());
      }
  };
  constexpr auto ZOOM_WIDTH = 70;
  constexpr auto ZOOM_HEIGHT = 32;
  constexpr auto ZOOM_FIELD_LEFT = -1;
  constexpr auto ZOOM_FIELD_TOP = 8;
  constexpr auto ZOOM_FIELD_HEIGHT = 20;
  constexpr auto ZOOM_BUTTON_WIDTH = 16;
  constexpr auto ZOOM_CHEVRON_WIDTH = 8.0;
  constexpr auto ZOOM_CHEVRON_HEIGHT = 14.0 / 3;
  constexpr auto ZOOM_CHEVRON_TOP = 52.0 / 3;
  constexpr auto ICON_BUTTON_TOP = 4;
  constexpr auto ICON_BUTTON_SPACING = 14;
  constexpr auto ZOOM_SPACING = 10;
  const auto ZOOM_LINE_COLOR = QColor(0xE4, 0xE4, 0xE4);

  class ZoomControl : public QWidget {
    public:
      ZoomControl(QLineEdit& field, QToolButton& presets)
          : m_ratio(0) {
        setObjectName("zoom_control");
        setFixedSize(ZOOM_WIDTH, ZOOM_HEIGHT);
        field.setParent(this);
        presets.setParent(this);
        field.setGeometry(ZOOM_FIELD_LEFT, ZOOM_FIELD_TOP,
          ZOOM_WIDTH - ZOOM_BUTTON_WIDTH - ZOOM_FIELD_LEFT, ZOOM_FIELD_HEIGHT);
        presets.setGeometry(
          ZOOM_WIDTH - ZOOM_BUTTON_WIDTH, 0, ZOOM_BUTTON_WIDTH, ZOOM_HEIGHT);
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto ratio = devicePixelRatioF();
        if(ratio != m_ratio) {
          m_ratio = ratio;
          m_chevron = make_fitted_glyph(Glyph::CHEVRON_DOWN, QSize(
            static_cast<int>(std::lround(ZOOM_CHEVRON_WIDTH * ratio)),
            static_cast<int>(std::lround(ZOOM_CHEVRON_HEIGHT * ratio))),
            ratio);
        }
        auto origin = QPointF(mapTo(window(), QPoint(0, 0))) * ratio;
        auto left = std::lround(origin.x());
        auto right = std::lround(origin.x() + width() * ratio);
        auto bottom = std::lround(origin.y() + height() * ratio);
        auto line = std::max(std::lround(ratio), 1L);
        auto painter = QPainter(this);
        painter.fillRect(QRectF((left - origin.x()) / ratio,
          (bottom - line - origin.y()) / ratio, (right - left) / ratio,
          line / ratio), ZOOM_LINE_COLOR);
        auto chevron_left = right - m_chevron.width();
        auto chevron_top = std::lround(origin.y() + ZOOM_CHEVRON_TOP * ratio);
        painter.drawPixmap(QPointF((chevron_left - origin.x()) / ratio,
          (chevron_top - origin.y()) / ratio), m_chevron);
      }

    private:
      double m_ratio;
      QPixmap m_chevron;
  };

  QToolButton* make_icon_button(Glyph glyph, const QString& tip) {
    auto button = new QToolButton();
    button->setObjectName("app_bar_button");
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(ICON_BUTTON_SIZE, ICON_BUTTON_SIZE);
    button->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    button->setIcon(get_icon(glyph));
    button->setToolTip(translate("app_bar", tip));
    button->setAccessibleName(translate("app_bar", tip));
    return button;
  }

  QToolButton* make_workspace_button(const QString& label) {
    auto button = new WorkspaceButton();
    button->setObjectName("workspace_button");
    button->setText(translate("app_bar", label));
    button->setAccessibleName(
      translate("app_bar", QString("%1 Workspace").arg(label)));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
  }
}

AppBar::AppBar(QWidget* parent)
    : QWidget(parent),
      m_workspace(Workspace::DESIGN),
      m_zoom(1),
      m_design(make_workspace_button("Design")),
      m_prototype(make_workspace_button("Prototype")),
      m_share(make_workspace_button("Share")),
      m_title(new QLabel()),
      m_history(make_icon_button(Glyph::CHEVRON_DOWN, "Version History")),
      m_zoom_field(new QLineEdit()),
      m_left(nullptr),
      m_middle(nullptr),
      m_right(nullptr) {
  setObjectName("app_bar");
  setAttribute(Qt::WA_StyledBackground);
  setFixedHeight(HEIGHT);
  auto layout = new QGridLayout(this);
  layout->setSizeConstraint(QLayout::SetNoConstraint);
  layout->setContentsMargins(APP_BAR_MARGIN, 0, 12, 0);
  layout->setHorizontalSpacing(0);
  auto left = new QWidget();
  m_left = left;
  auto left_layout = new QHBoxLayout(left);
  left_layout->setContentsMargins(0, 0, 0, 0);
  left_layout->setSpacing(0);
  auto home = make_icon_button(Glyph::HOME, "Home");
  home->setEnabled(false);
  left_layout->addWidget(home);
  left_layout->addSpacing(
    WORKSPACE_LEFT - APP_BAR_MARGIN - ICON_BUTTON_SIZE);
  for(auto button : {m_design, m_prototype, m_share}) {
    left_layout->addWidget(button, 0, Qt::AlignTop);
    left_layout->addSpacing(WORKSPACE_SPACING);
  }
  left_layout->addStretch(1);
  auto middle = new QWidget();
  m_middle = middle;
  auto middle_layout = new QHBoxLayout(middle);
  middle_layout->setContentsMargins(0, 0, 0, 0);
  middle_layout->setSpacing(2);
  m_title->setObjectName("document_title");
  middle_layout->addWidget(m_title);
  m_history->setFixedSize(20, 20);
  m_history->setIconSize(QSize(14, 14));
  m_history->setEnabled(false);
  middle_layout->addWidget(m_history);
  auto right = new QWidget();
  m_right = right;
  auto right_layout = new QHBoxLayout(right);
  right_layout->setContentsMargins(0, 0, 0, 0);
  right_layout->setSpacing(0);
  right_layout->addStretch(1);
  auto is_first = true;
  for(auto [glyph, tip, feature] : {std::tuple(Glyph::INVITE,
      "Invite to Document", std::optional<Feature>()),
      std::tuple(Glyph::DEVICE, "Device Preview",
        std::optional(Feature::DEVICE_PREVIEW)),
      std::tuple(Glyph::PLAY, "Desktop Preview",
        std::optional(Feature::DESKTOP_PREVIEW))}) {
    auto button = make_icon_button(glyph, tip);
    button->setEnabled(false);
    if(feature) {
      set_feature(*button, *feature);
    }
    if(!is_first) {
      right_layout->addSpacing(ICON_BUTTON_SPACING);
    }
    is_first = false;
    auto cell = new QWidget();
    auto cell_layout = new QVBoxLayout(cell);
    cell_layout->setContentsMargins(0, ICON_BUTTON_TOP, 0, 0);
    cell_layout->setSpacing(0);
    cell_layout->addWidget(button);
    cell_layout->addStretch(1);
    right_layout->addWidget(cell);
  }
  right_layout->addSpacing(ZOOM_SPACING);
  m_zoom_field->setObjectName("zoom_field");
  m_zoom_field->setAccessibleName(translate("app_bar", "Zoom"));
  m_zoom_field->setToolTip(translate("app_bar", "Zoom"));
  m_zoom_field->setFrame(false);
  m_zoom_field->installEventFilter(this);
  auto presets = make_icon_button(Glyph::CHEVRON_DOWN, "Zoom");
  presets->setIcon(QIcon());
  presets->setCursor(Qt::ArrowCursor);
  presets->setPopupMode(QToolButton::InstantPopup);
  presets->setObjectName("zoom_presets");
  auto menu = new QMenu(presets);
  for(auto preset : ZOOM_PRESETS) {
    auto text = translate("app_bar", format_zoom(preset));
    if(preset == 1) {
      text += "\t" + to_xd_shortcut_text(QKeySequence("Ctrl+1"));
    } else if(preset == 2) {
      text += "\t" + to_xd_shortcut_text(QKeySequence("Ctrl+2"));
    }
    auto action = menu->addAction(text);
    connect(action, &QAction::triggered, this, [this, preset] {
      pick_zoom(preset);
    });
  }
  presets->setMenu(menu);
  auto zoom = new ZoomControl(*m_zoom_field, *presets);
  zoom->setToolTip(translate("app_bar", "Zoom"));
  right_layout->addWidget(zoom, 0, Qt::AlignTop);
  layout->addWidget(left, 0, 0);
  layout->addWidget(middle, 0, 1, Qt::AlignCenter);
  layout->addWidget(right, 0, 2);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(2, 1);
  connect(m_design, &QToolButton::clicked, this, [this] {
    set_workspace(Workspace::DESIGN);
    if(m_workspace_handler) {
      m_workspace_handler(m_workspace);
    }
  });
  connect(m_prototype, &QToolButton::clicked, this, [this] {
    set_workspace(Workspace::PROTOTYPE);
    if(m_workspace_handler) {
      m_workspace_handler(m_workspace);
    }
  });
  connect(m_share, &QToolButton::clicked, this, [this] {
    set_workspace(Workspace::SHARE);
    if(m_workspace_handler) {
      m_workspace_handler(m_workspace);
    }
  });
  connect(m_zoom_field, &QLineEdit::editingFinished, this, [this] {
    enter_zoom();
  });
  connect(m_zoom_field, &QLineEdit::returnPressed, this, [this] {
    enter_zoom();
    m_zoom_field->clearFocus();
    if(m_release_handler) {
      m_release_handler();
    }
  });
  update_workspace();
  set_zoom(1);
}

Workspace AppBar::get_workspace() const {
  return m_workspace;
}

void AppBar::set_workspace(Workspace workspace) {
  m_workspace = workspace;
  update_workspace();
}

void AppBar::set_workspace_handler(WorkspaceHandler handler) {
  m_workspace_handler = std::move(handler);
}

QString AppBar::get_title() const {
  return m_title->text();
}

void AppBar::set_title(const QString& name, bool is_modified) {
  if(is_modified) {
    m_title->setText(QString("%1 *").arg(name));
  } else {
    m_title->setText(name);
  }
  m_history->setVisible(!name.isEmpty());
}

double AppBar::get_zoom() const {
  return m_zoom;
}

void AppBar::set_zoom(double zoom) {
  m_zoom = zoom;
  auto text = format_zoom(zoom);
  if(!m_zoom_field->hasFocus() && m_zoom_field->text() != text) {
    m_zoom_field->setText(text);
  }
}

void AppBar::set_zoom_handler(ZoomHandler handler) {
  m_zoom_handler = std::move(handler);
}

void AppBar::set_release_handler(ReleaseHandler handler) {
  m_release_handler = std::move(handler);
}

QLineEdit& AppBar::get_zoom_field() {
  return *m_zoom_field;
}

QToolButton& AppBar::get_workspace_button(Workspace workspace) {
  if(workspace == Workspace::PROTOTYPE) {
    return *m_prototype;
  } else if(workspace == Workspace::SHARE) {
    return *m_share;
  }
  return *m_design;
}

void AppBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  fit_controls();
}

void AppBar::fit_controls() {
  auto grid = static_cast<QGridLayout*>(layout());
  auto margins = grid->contentsMargins();
  auto needed = margins.left() + m_left->sizeHint().width() +
    m_middle->sizeHint().width() + m_right->sizeHint().width() +
    margins.right();
  auto is_fitting = width() >= needed;
  m_right->setVisible(is_fitting);
  grid->setColumnStretch(0, is_fitting ? 1 : 0);
  grid->setColumnStretch(1, is_fitting ? 0 : 1);
  grid->setColumnStretch(2, is_fitting ? 1 : 0);
}

bool AppBar::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_zoom_field) {
    if(event->type() == QEvent::FocusIn) {
      auto text = format_zoom(m_zoom);
      text.chop(1);
      m_zoom_field->setText(text);
      QTimer::singleShot(0, m_zoom_field, [this] {
        m_zoom_field->selectAll();
      });
    } else if(event->type() == QEvent::FocusOut) {
      QTimer::singleShot(0, m_zoom_field, [this] {
        if(!m_zoom_field->hasFocus()) {
          m_zoom_field->setText(format_zoom(m_zoom));
        }
      });
    } else if(event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      m_zoom_field->setText(format_zoom(m_zoom));
      m_zoom_field->clearFocus();
      if(m_release_handler) {
        m_release_handler();
      }
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void AppBar::update_workspace() {
  m_design->setChecked(m_workspace == Workspace::DESIGN);
  m_prototype->setChecked(m_workspace == Workspace::PROTOTYPE);
  m_share->setChecked(m_workspace == Workspace::SHARE);
}

void AppBar::enter_zoom() {
  if(auto zoom = parse_zoom(m_zoom_field->text())) {
    pick_zoom(*zoom);
  } else {
    m_zoom_field->setText(format_zoom(m_zoom));
  }
}

void AppBar::pick_zoom(double zoom) {
  m_zoom = zoom;
  m_zoom_field->setText(format_zoom(zoom));
  if(m_zoom_handler) {
    m_zoom_handler(zoom);
  }
}

QString Rellu::format_zoom(double zoom) {
  auto single = static_cast<double>(static_cast<float>(zoom));
  auto tenths = std::round(single * 1000);
  if(std::fmod(tenths, 10) == 0) {
    return QString("%1%").arg(tenths / 10, 0, 'f', 0);
  }
  return QString("%1%").arg(tenths / 10, 0, 'f', 1);
}

std::optional<double> Rellu::parse_zoom(const QString& text) {
  auto value = text.trimmed();
  if(value.endsWith('%')) {
    value.chop(1);
  }
  auto is_valid = false;
  auto percent = value.trimmed().toDouble(&is_valid);
  if(!is_valid || !std::isfinite(percent) || !(percent > 0)) {
    return std::nullopt;
  }
  return percent / 100;
}

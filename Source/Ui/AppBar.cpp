#include <cmath>
#include <utility>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QTimer>
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Rellu/Ui/XdMenus.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto ICON_BUTTON_SIZE = 32;
  constexpr auto ICON_SIZE = 20;
  constexpr auto ZOOM_FIELD_WIDTH = 64;

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
    auto button = new QToolButton();
    button->setObjectName("workspace_button");
    button->setText(translate("app_bar", label));
    button->setAccessibleName(
      translate("app_bar", QString("%1 Workspace").arg(label)));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(AppBar::HEIGHT);
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
      m_zoom_field(new QLineEdit()) {
  setObjectName("app_bar");
  setAttribute(Qt::WA_StyledBackground);
  setFixedHeight(HEIGHT);
  auto layout = new QGridLayout(this);
  layout->setContentsMargins(8, 0, 12, 0);
  layout->setHorizontalSpacing(0);
  auto left = new QWidget();
  auto left_layout = new QHBoxLayout(left);
  left_layout->setContentsMargins(0, 0, 0, 0);
  left_layout->setSpacing(0);
  auto home = make_icon_button(Glyph::HOME, "Home");
  home->setEnabled(false);
  left_layout->addWidget(home);
  left_layout->addSpacing(20);
  for(auto button : {m_design, m_prototype, m_share}) {
    left_layout->addWidget(button);
    left_layout->addSpacing(22);
  }
  left_layout->addStretch(1);
  auto middle = new QWidget();
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
  auto right_layout = new QHBoxLayout(right);
  right_layout->setContentsMargins(0, 0, 0, 0);
  right_layout->setSpacing(10);
  right_layout->addStretch(1);
  for(auto [glyph, tip] : {std::pair(Glyph::INVITE, "Invite to Document"),
      std::pair(Glyph::DEVICE, "Device Preview"),
      std::pair(Glyph::PLAY, "Desktop Preview")}) {
    auto button = make_icon_button(glyph, tip);
    button->setEnabled(false);
    right_layout->addWidget(button);
  }
  right_layout->addSpacing(4);
  auto zoom = new QWidget();
  zoom->setObjectName("zoom_control");
  zoom->setAttribute(Qt::WA_StyledBackground);
  auto zoom_layout = new QHBoxLayout(zoom);
  zoom_layout->setContentsMargins(0, 0, 0, 0);
  zoom_layout->setSpacing(0);
  m_zoom_field->setObjectName("zoom_field");
  m_zoom_field->setAccessibleName(translate("app_bar", "Zoom"));
  m_zoom_field->setFixedWidth(ZOOM_FIELD_WIDTH);
  m_zoom_field->setFrame(false);
  m_zoom_field->installEventFilter(this);
  zoom_layout->addWidget(m_zoom_field);
  auto presets = make_icon_button(Glyph::CHEVRON_DOWN, "Zoom");
  presets->setFixedSize(24, 24);
  presets->setIconSize(QSize(16, 16));
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
  zoom_layout->addWidget(presets);
  right_layout->addWidget(zoom);
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
  auto tenths = std::round(zoom * 1000);
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

#include <algorithm>
#include <utility>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/HomeWindow.hpp"

using namespace Rellu;

namespace {
  const auto FILE_FILTER = QString("Adobe XD Files (*.xd);;All Files (*)");

  QRect center(const QRect& bounds, const QPoint& point, const QRect& area) {
    auto result = bounds;
    result.moveCenter(point);
    if(area.isEmpty()) {
      return result;
    }
    result.moveLeft(std::max(area.left(),
      std::min(result.left(), area.right() - result.width() + 1)));
    result.moveTop(std::max(area.top(),
      std::min(result.top(), area.bottom() - result.height() + 1)));
    return result;
  }
}

HomeWindow::HomeWindow(DocumentWindows& windows)
    : m_windows(&windows),
      m_screen(std::make_unique<HomeScreen>()),
      m_open_dialog([] (QWidget& parent) {
        return QFileDialog::getOpenFileName(
          &parent, "Open", QString(), FILE_FILTER);
      }),
      m_quit_handler([] {
        QCoreApplication::quit();
      }) {
  m_screen->setWindowTitle("Rellu");
  m_screen->resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
  m_screen->installEventFilter(this);
  m_screen->set_create_handler([this] (const auto& preset) {
    m_windows->make_new(make_new_document(ArtboardPreset(
      preset.m_name.toStdString(), Size(preset.m_width, preset.m_height))),
      get_host());
    leave();
  });
  m_screen->set_new_handler([this] {
    m_windows->make_new(make_new_document(), get_host());
    leave();
  });
  m_screen->set_open_handler([this] (const auto& path) {
    m_windows->open(path, get_host());
    leave();
  });
  m_screen->set_open_dialog_handler([this] {
    if(!m_open_dialog) {
      return;
    }
    auto path = m_open_dialog(*m_screen);
    if(!path.isEmpty()) {
      m_windows->open(path, get_host());
      leave();
    }
  });
  m_screen->set_back_handler([this] {
    if(auto window = m_windows->get_active()) {
      m_windows->activate(*window);
    }
    leave();
  });
  m_subscription = m_windows->subscribe([this] {
    update();
  });
  update();
}

HomeWindow::~HomeWindow() {
  m_screen->removeEventFilter(this);
}

HomeScreen& HomeWindow::get_screen() {
  return *m_screen;
}

void HomeWindow::set_open_dialog(OpenDialog dialog) {
  m_open_dialog = std::move(dialog);
}

void HomeWindow::set_quit_handler(std::function<void ()> handler) {
  m_quit_handler = std::move(handler);
}

bool HomeWindow::is_shown() const {
  return m_screen->isVisible();
}

void HomeWindow::show(const QByteArray& geometry) {
  if(geometry.isEmpty() || !m_screen->restoreGeometry(geometry)) {
    m_screen->setWindowState(Qt::WindowNoState);
    m_screen->resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    if(auto screen = QGuiApplication::primaryScreen()) {
      auto area = screen->availableGeometry();
      m_screen->move(
        center(m_screen->frameGeometry(), area.center(), area).topLeft());
    }
  }
  m_screen->show();
  activate();
}

void HomeWindow::show_over(const QWidget& window) {
  if(!is_shown()) {
    m_screen->setWindowState(Qt::WindowNoState);
    m_screen->resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    auto area = QRect();
    if(auto screen = window.screen()) {
      area = screen->availableGeometry();
    }
    m_screen->move(center(m_screen->frameGeometry(),
      window.frameGeometry().center(), area).topLeft());
    m_screen->show();
  }
  activate();
}

void HomeWindow::activate() {
  if(!is_shown()) {
    return;
  }
  if(m_screen->isMinimized()) {
    m_screen->setWindowState(m_screen->windowState() & ~Qt::WindowMinimized);
  }
  m_screen->raise();
  m_screen->activateWindow();
}

void HomeWindow::hide() {
  m_screen->hide();
}

bool HomeWindow::open(const QString& path) {
  auto host = m_windows->get_windows().empty() ? get_host() : nullptr;
  if(!m_windows->open(path, host)) {
    return false;
  }
  if(host) {
    hide();
  }
  return true;
}

bool HomeWindow::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_screen.get() && event->type() == QEvent::Close &&
      m_windows->get_windows().empty() && m_quit_handler) {
    m_quit_handler();
  }
  return QObject::eventFilter(watched, event);
}

const QWidget* HomeWindow::get_host() const {
  if(is_shown()) {
    return m_screen.get();
  }
  return nullptr;
}

void HomeWindow::leave() {
  if(!m_windows->get_windows().empty()) {
    hide();
  }
}

void HomeWindow::update() {
  if(m_screen->get_recent_paths() != m_windows->get_recent_files()) {
    m_screen->set_recent_paths(m_windows->get_recent_files());
  }
  m_screen->set_back_enabled(!m_windows->get_windows().empty());
}

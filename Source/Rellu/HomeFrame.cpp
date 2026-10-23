#include "HomeFrame.hpp"
#include <QEvent>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QScreen>
#include "Rellu/Ui/DocumentWindows.hpp"
#include "CaptionButtons.hpp"
#include "NativeFrame.hpp"

using namespace Rellu;

HomeFrame::HomeFrame(QWidget* parent)
    : HomeScreen(parent),
      m_maximize(nullptr) {
  if(!is_custom_frame_supported()) {
    return;
  }
  auto buttons = make_caption_buttons(*this, *this);
  m_caption_buttons.assign(buttons.begin(), buttons.end());
  m_maximize = buttons[1];
  enable_custom_frame(*this);
  place_caption_buttons();
}

void HomeFrame::place_at(const QPoint& position) {
  m_position = position;
}

void HomeFrame::resizeEvent(QResizeEvent* event) {
  HomeScreen::resizeEvent(event);
  place_caption_buttons();
}

void HomeFrame::changeEvent(QEvent* event) {
  HomeScreen::changeEvent(event);
  if(event->type() == QEvent::WindowStateChange && m_maximize) {
    update_maximize_tip(*m_maximize, isMaximized());
  }
}

bool HomeFrame::nativeEvent(
    const QByteArray& type, void* message, qintptr* result) {
  if(m_position && is_showing(message)) {
    if(auto bounds = get_normal_bounds(*this); bounds.isValid()) {
      bounds.moveTopLeft(*m_position);
      set_normal_bounds(*this, bounds);
    }
    m_position.reset();
  }
  if(m_maximize && handle_frame_event(*this, message, result,
      [this] (QPoint position) {
        return is_caption(position);
      }, CaptionButton::HEIGHT, m_maximize)) {
    return true;
  }
  return HomeScreen::nativeEvent(type, message, result);
}

bool HomeFrame::is_caption(QPoint position) const {
  return position.y() >= 0 && position.y() < HEADER_HEIGHT &&
    rect().contains(position) && !childAt(position);
}

void HomeFrame::place_caption_buttons() {
  auto x = width();
  for(auto i = m_caption_buttons.rbegin(); i != m_caption_buttons.rend();
      ++i) {
    x -= (*i)->width();
    (*i)->move(x, 0);
    (*i)->raise();
  }
}

void Rellu::place_home_frame(
    HomeScreen& screen, const QWidget* window, const QRect& launch) {
  auto frame = dynamic_cast<HomeFrame*>(&screen);
  auto ratio = screen.devicePixelRatioF();
  if(window) {
    if(auto normal = get_normal_bounds(*window); frame && normal.isValid()) {
      auto position =
        get_cascade_position(normal.topLeft(), window->devicePixelRatioF());
      frame->place_at(position);
      screen.move(QPoint(static_cast<int>(position.x() / ratio),
        static_cast<int>(position.y() / ratio)));
      return;
    }
    screen.move(get_cascade_position(window->normalGeometry().topLeft(), 1));
    return;
  }
  if(frame && launch.isValid()) {
    frame->place_at(launch.topLeft());
    screen.move(QPoint(static_cast<int>(launch.x() / ratio),
      static_cast<int>(launch.y() / ratio)));
    return;
  }
  if(auto primary = QGuiApplication::primaryScreen()) {
    auto bounds = screen.frameGeometry();
    bounds.moveCenter(primary->availableGeometry().center());
    screen.move(bounds.topLeft());
  }
}

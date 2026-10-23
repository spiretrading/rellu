#include "NativeFrame.hpp"
#include <QCoreApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QPointer>
#include "Rellu/Tools/ViewFit.hpp"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>
#endif

using namespace Rellu;

#ifdef Q_OS_WIN
namespace {
  auto hovered = QPointer<QAbstractButton>();

  void hover(QAbstractButton* button) {
    if(hovered == button) {
      return;
    }
    if(hovered) {
      hovered->setDown(false);
      auto leave = QEvent(QEvent::Leave);
      QCoreApplication::sendEvent(hovered, &leave);
    }
    hovered = button;
    if(button) {
      auto global = QCursor::pos();
      auto local = button->mapFromGlobal(global);
      auto enter = QEnterEvent(local, local, global);
      QCoreApplication::sendEvent(button, &enter);
    }
  }

  int get_border(HWND handle) {
    auto dpi = GetDpiForWindow(handle);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
      GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
  }

  QRect to_rect(const RECT& rect) {
    return QRect(rect.left, rect.top, rect.right - rect.left,
      rect.bottom - rect.top);
  }

  void show_system_menu(HWND handle, POINT position) {
    auto menu = GetSystemMenu(handle, FALSE);
    if(!menu) {
      return;
    }
    auto is_maximized = IsZoomed(handle) != FALSE;
    auto is_minimized = IsIconic(handle) != FALSE;
    auto enable = [&] (UINT command, bool is_enabled) {
      EnableMenuItem(menu, command,
        MF_BYCOMMAND | (is_enabled ? MFS_ENABLED : MFS_GRAYED));
    };
    enable(SC_RESTORE, is_maximized || is_minimized);
    enable(SC_MOVE, !is_maximized && !is_minimized);
    enable(SC_SIZE, !is_maximized && !is_minimized);
    enable(SC_MINIMIZE, !is_minimized);
    enable(SC_MAXIMIZE, !is_maximized);
    enable(SC_CLOSE, true);
    SetMenuDefaultItem(menu, static_cast<UINT>(-1), FALSE);
    auto command = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN |
      TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, position.x, position.y,
      handle, nullptr);
    if(command) {
      PostMessageW(handle, WM_SYSCOMMAND, static_cast<WPARAM>(command), 0);
    }
  }

  void apply_frame(HWND handle) {
    auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    SetWindowLongPtrW(handle, GWL_STYLE, style | WS_THICKFRAME | WS_CAPTION |
      WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    auto margins = MARGINS(0, 0, 1, 0);
    DwmExtendFrameIntoClientArea(handle, &margins);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE |
      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  class FrameFilter : public QObject {
    public:
      using QObject::QObject;

      bool eventFilter(QObject* watched, QEvent* event) override {
        if(event->type() == QEvent::WinIdChange) {
          if(auto handle = static_cast<QWidget*>(watched)->internalWinId()) {
            apply_frame(reinterpret_cast<HWND>(handle));
          }
        }
        return QObject::eventFilter(watched, event);
      }
  };
}

bool Rellu::is_custom_frame_supported() {
  return true;
}

void Rellu::enable_custom_frame(QWidget& window) {
  window.installEventFilter(new FrameFilter(&window));
  apply_frame(reinterpret_cast<HWND>(window.winId()));
}

bool Rellu::handle_frame_event(QWidget& window, void* message,
    qintptr* result, const CaptionTest& is_caption, int caption_height,
    QAbstractButton* maximize) {
  auto native = static_cast<MSG*>(message);
  auto is_maximize = maximize && native->wParam == HTMAXBUTTON;
  if(native->message == WM_NCMOUSEMOVE) {
    if(is_maximize) {
      auto tracking = TRACKMOUSEEVENT();
      tracking.cbSize = sizeof(tracking);
      tracking.dwFlags = TME_LEAVE | TME_NONCLIENT;
      tracking.hwndTrack = native->hwnd;
      TrackMouseEvent(&tracking);
      hover(maximize);
      *result = DefWindowProcW(
        native->hwnd, native->message, native->wParam, native->lParam);
      return true;
    } else {
      hover(nullptr);
    }
    return false;
  } else if(native->message == WM_NCMOUSELEAVE ||
      native->message == WM_MOUSEMOVE) {
    hover(nullptr);
    return false;
  } else if(is_maximize && (native->message == WM_NCLBUTTONDOWN ||
      native->message == WM_NCLBUTTONDBLCLK)) {
    hover(maximize);
    maximize->setDown(true);
    *result = 0;
    return true;
  } else if(is_maximize && native->message == WM_NCLBUTTONUP) {
    if(maximize->isDown()) {
      maximize->setDown(false);
      maximize->click();
    }
    *result = 0;
    return true;
  } else if(native->message == WM_NCRBUTTONUP && native->wParam == HTCAPTION) {
    show_system_menu(native->hwnd,
      POINT(GET_X_LPARAM(native->lParam), GET_Y_LPARAM(native->lParam)));
    *result = 0;
    return true;
  } else if(native->message == WM_SYSKEYDOWN && native->wParam == VK_SPACE) {
    auto character = MSG();
    PeekMessageW(&character, native->hwnd, WM_SYSCHAR, WM_SYSCHAR, PM_REMOVE);
    auto origin = POINT(0, 0);
    ClientToScreen(native->hwnd, &origin);
    origin.y += static_cast<LONG>(caption_height * window.devicePixelRatioF());
    show_system_menu(native->hwnd, origin);
    *result = 0;
    return true;
  } else if(native->message == WM_NCACTIVATE) {
    *result =
      DefWindowProcW(native->hwnd, native->message, native->wParam, -1);
    return true;
  } else if(native->message == WM_GETMINMAXINFO) {
    auto info = reinterpret_cast<MINMAXINFO*>(native->lParam);
    auto border = get_border(native->hwnd);
    auto ratio = GetDpiForWindow(native->hwnd) /
      static_cast<double>(USER_DEFAULT_SCREEN_DPI);
    auto client = get_minimum_client_size(ratio, border);
    info->ptMinTrackSize.x = static_cast<LONG>(client.m_width) + 2 * border;
    info->ptMinTrackSize.y = static_cast<LONG>(client.m_height) + 1 + border;
    *result = 0;
    return true;
  } else if(native->message == WM_NCCALCSIZE && native->wParam) {
    auto params = reinterpret_cast<NCCALCSIZE_PARAMS*>(native->lParam);
    if(IsZoomed(native->hwnd)) {
      auto monitor =
        MonitorFromRect(&params->rgrc[0], MONITOR_DEFAULTTONEAREST);
      auto info = MONITORINFO();
      info.cbSize = sizeof(info);
      if(GetMonitorInfoW(monitor, &info)) {
        params->rgrc[0] = info.rcWork;
      }
    } else if(!IsIconic(native->hwnd)) {
      auto border = get_border(native->hwnd);
      params->rgrc[0].top += 1;
      params->rgrc[0].left += border;
      params->rgrc[0].right -= border;
      params->rgrc[0].bottom -= border;
    }
    *result = 0;
    return true;
  } else if(native->message != WM_NCHITTEST) {
    return false;
  }
  auto x = GET_X_LPARAM(native->lParam);
  auto y = GET_Y_LPARAM(native->lParam);
  auto bounds = RECT();
  GetWindowRect(native->hwnd, &bounds);
  if(!IsZoomed(native->hwnd)) {
    auto border = get_border(native->hwnd);
    auto is_left = x < bounds.left + border;
    auto is_right = x >= bounds.right - border;
    auto is_top = y < bounds.top + border;
    auto is_bottom = y >= bounds.bottom - border;
    if(is_top && is_left) {
      *result = HTTOPLEFT;
      return true;
    } else if(is_top && is_right) {
      *result = HTTOPRIGHT;
      return true;
    } else if(is_bottom && is_left) {
      *result = HTBOTTOMLEFT;
      return true;
    } else if(is_bottom && is_right) {
      *result = HTBOTTOMRIGHT;
      return true;
    } else if(is_left) {
      *result = HTLEFT;
      return true;
    } else if(is_right) {
      *result = HTRIGHT;
      return true;
    } else if(is_top) {
      *result = HTTOP;
      return true;
    } else if(is_bottom) {
      *result = HTBOTTOM;
      return true;
    }
  }
  auto origin = POINT(0, 0);
  ClientToScreen(native->hwnd, &origin);
  auto ratio = window.devicePixelRatioF();
  auto position = QPoint(static_cast<int>((x - origin.x) / ratio),
    static_cast<int>((y - origin.y) / ratio));
  if(maximize && maximize->isVisible() &&
      maximize->rect().contains(maximize->mapFrom(&window, position))) {
    *result = HTMAXBUTTON;
    return true;
  }
  if(is_caption && is_caption(position)) {
    *result = HTCAPTION;
    return true;
  }
  *result = HTCLIENT;
  return true;
}

QRect Rellu::get_normal_bounds(const QWidget& window) {
  auto handle = reinterpret_cast<HWND>(window.winId());
  if(!IsZoomed(handle) && !IsIconic(handle)) {
    auto bounds = RECT();
    if(!GetWindowRect(handle, &bounds)) {
      return QRect();
    }
    return to_rect(bounds);
  }
  auto placement = WINDOWPLACEMENT();
  placement.length = sizeof(placement);
  if(!GetWindowPlacement(handle, &placement)) {
    return QRect();
  }
  auto bounds = to_rect(placement.rcNormalPosition);
  auto info = MONITORINFO();
  info.cbSize = sizeof(info);
  if(GetMonitorInfoW(
      MonitorFromPoint(POINT(0, 0), MONITOR_DEFAULTTOPRIMARY), &info)) {
    bounds.translate(info.rcWork.left - info.rcMonitor.left,
      info.rcWork.top - info.rcMonitor.top);
  }
  return bounds;
}

void Rellu::set_normal_bounds(QWidget& window, const QRect& bounds) {
  auto handle = reinterpret_cast<HWND>(window.winId());
  if(!IsZoomed(handle) && !IsIconic(handle)) {
    SetWindowPos(handle, nullptr, bounds.x(), bounds.y(), bounds.width(),
      bounds.height(), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    return;
  }
  auto placement = WINDOWPLACEMENT();
  placement.length = sizeof(placement);
  if(!GetWindowPlacement(handle, &placement)) {
    return;
  }
  auto normal = bounds;
  auto info = MONITORINFO();
  info.cbSize = sizeof(info);
  if(GetMonitorInfoW(
      MonitorFromPoint(POINT(0, 0), MONITOR_DEFAULTTOPRIMARY), &info)) {
    normal.translate(info.rcMonitor.left - info.rcWork.left,
      info.rcMonitor.top - info.rcWork.top);
  }
  placement.rcNormalPosition = RECT(normal.x(), normal.y(),
    normal.x() + normal.width(), normal.y() + normal.height());
  SetWindowPlacement(handle, &placement);
}

bool Rellu::is_showing(void* message) {
  auto native = static_cast<MSG*>(message);
  return native->message == WM_SHOWWINDOW && native->wParam;
}

QRect Rellu::get_work_area(const QRect& bounds) {
  auto area = RECT(bounds.x(), bounds.y(), bounds.x() + bounds.width(),
    bounds.y() + bounds.height());
  auto info = MONITORINFO();
  info.cbSize = sizeof(info);
  if(!GetMonitorInfoW(
      MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST), &info)) {
    return QRect();
  }
  return to_rect(info.rcWork);
}

void Rellu::allow_foreground_activation() {
  AllowSetForegroundWindow(ASFW_ANY);
}
#else
bool Rellu::is_custom_frame_supported() {
  return false;
}

void Rellu::enable_custom_frame(QWidget&) {}

bool Rellu::handle_frame_event(QWidget&, void*, qintptr*, const CaptionTest&,
    int, QAbstractButton*) {
  return false;
}

QRect Rellu::get_normal_bounds(const QWidget&) {
  return QRect();
}

void Rellu::set_normal_bounds(QWidget&, const QRect&) {}

QRect Rellu::get_work_area(const QRect&) {
  return QRect();
}

bool Rellu::is_showing(void*) {
  return false;
}

void Rellu::allow_foreground_activation() {}
#endif

#include "NativeFrame.hpp"
#include <QEvent>
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
  window.setWindowFlag(Qt::FramelessWindowHint);
  window.installEventFilter(new FrameFilter(&window));
  apply_frame(reinterpret_cast<HWND>(window.winId()));
}

bool Rellu::handle_frame_event(QWidget& window, void* message,
    qintptr* result, const CaptionTest& is_caption, int caption_height) {
  auto native = static_cast<MSG*>(message);
  if(native->message == WM_NCRBUTTONUP && native->wParam == HTCAPTION) {
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
  } else if(native->message == WM_WINDOWPOSCHANGING) {
    auto style = GetWindowLongPtrW(native->hwnd, GWL_STYLE);
    auto frame = static_cast<LONG_PTR>(WS_THICKFRAME | WS_CAPTION);
    if(IsZoomed(native->hwnd)) {
      if(style & frame) {
        SetWindowLongPtrW(native->hwnd, GWL_STYLE, style & ~frame);
      }
    } else if(!IsIconic(native->hwnd) && (style & frame) != frame) {
      SetWindowLongPtrW(native->hwnd, GWL_STYLE, style | frame);
    }
    return false;
  } else if(native->message == WM_NCCALCSIZE && native->wParam) {
    if(IsZoomed(native->hwnd)) {
      auto params = reinterpret_cast<NCCALCSIZE_PARAMS*>(native->lParam);
      auto monitor =
        MonitorFromRect(&params->rgrc[0], MONITOR_DEFAULTTONEAREST);
      auto info = MONITORINFO();
      info.cbSize = sizeof(info);
      if(GetMonitorInfoW(monitor, &info)) {
        params->rgrc[0] = info.rcWork;
      }
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
    auto dpi = GetDpiForWindow(native->hwnd);
    auto border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
      GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
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
  auto ratio = window.devicePixelRatioF();
  auto position = QPoint(static_cast<int>((x - bounds.left) / ratio),
    static_cast<int>((y - bounds.top) / ratio));
  if(is_caption && is_caption(position)) {
    *result = HTCAPTION;
    return true;
  }
  *result = HTCLIENT;
  return true;
}
#else
bool Rellu::is_custom_frame_supported() {
  return false;
}

void Rellu::enable_custom_frame(QWidget&) {}

bool Rellu::handle_frame_event(
    QWidget&, void*, qintptr*, const CaptionTest&, int) {
  return false;
}
#endif

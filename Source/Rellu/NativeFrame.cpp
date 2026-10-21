#include "NativeFrame.hpp"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#endif

using namespace Rellu;

#ifdef Q_OS_WIN
bool Rellu::is_custom_frame_supported() {
  return true;
}

void Rellu::enable_custom_frame(QWidget& window) {
  window.setWindowFlag(Qt::FramelessWindowHint);
  auto handle = reinterpret_cast<HWND>(window.winId());
  auto style = GetWindowLongPtrW(handle, GWL_STYLE);
  SetWindowLongPtrW(handle, GWL_STYLE, style | WS_THICKFRAME | WS_CAPTION |
    WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
  SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE |
    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool Rellu::handle_frame_event(QWidget& window, void* message,
    qintptr* result, const CaptionTest& is_caption) {
  auto native = static_cast<MSG*>(message);
  if(native->message == WM_NCCALCSIZE && native->wParam) {
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
    QWidget&, void*, qintptr*, const CaptionTest&) {
  return false;
}
#endif

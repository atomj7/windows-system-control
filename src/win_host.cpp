#include "win_host.hpp"

#include "log.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

constexpr int kOpenId = 101;
constexpr int kExitId = 102;
std::wstring g_url;

std::wstring utf8_to_wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  return out;
}

LRESULT CALLBACK wnd_proc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_COMMAND:
      if (LOWORD(wParam) == kOpenId) {
        ShellExecuteW(window, L"open", g_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
      }
      if (LOWORD(wParam) == kExitId) {
        DestroyWindow(window);
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, msg, wParam, lParam);
}

}  // namespace

void open_helios_url(const std::string& url) {
  const std::wstring wide = utf8_to_wide(url);
  ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

int run_helios_window(const std::string& url) {
  g_url = utf8_to_wide(url);
  const wchar_t* className = L"HeliosHost";
  WNDCLASSW wc{};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = className;
  RegisterClassW(&wc);

  HWND window = CreateWindowExW(
      0, className, L"Helios — свет и воздух", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 460, 220, nullptr, nullptr, wc.hInstance, nullptr);
  if (!window) {
    log_error("не удалось создать окно Helios");
    return 1;
  }

  CreateWindowExW(0, L"STATIC",
                  L"Helios запущен и держит этот компьютер.\nЗакрой это окно, когда закончишь настраивать свет и вентиляторы.",
                  WS_CHILD | WS_VISIBLE, 20, 16, 400, 70, window, nullptr, wc.hInstance, nullptr);
  CreateWindowExW(0, L"STATIC", g_url.c_str(), WS_CHILD | WS_VISIBLE, 20, 90, 400, 24, window, nullptr, wc.hInstance,
                  nullptr);
  CreateWindowExW(0, L"BUTTON", L"Открыть интерфейс", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 20, 130, 180, 32, window,
                  reinterpret_cast<HMENU>(static_cast<intptr_t>(kOpenId)), wc.hInstance, nullptr);
  CreateWindowExW(0, L"BUTTON", L"Выход", WS_CHILD | WS_VISIBLE, 220, 130, 100, 32, window,
                  reinterpret_cast<HMENU>(static_cast<intptr_t>(kExitId)), wc.hInstance, nullptr);

  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  log_info("окно Helios открыто");

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  log_info("окно Helios закрыто");
  return 0;
}

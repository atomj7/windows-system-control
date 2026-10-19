#define CINTERFACE
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include "WebView2.h"

#include "win_host.hpp"

#include "ipc.hpp"
#include "log.hpp"
#include "resource.h"

#include <dwmapi.h>

#include <filesystem>
#include <string>

namespace {

constexpr int kTimerId = 1;
constexpr DWORD kDarkTitle = 20;

std::wstring wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  return out;
}

using CreateEnvFn = HRESULT(STDMETHODCALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

struct Host {
  HardwareSupervisor* hardware = nullptr;
  std::wstring url;
  HWND window = nullptr;
  ICoreWebView2Controller* controller = nullptr;
  ICoreWebView2* webview = nullptr;
  bool webviewFailed = false;
};

Host* host_of(HWND window) { return reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA)); }

void resize_browser(Host* host) {
  if (!host || !host->controller || !host->window) return;
  RECT bounds{};
  GetClientRect(host->window, &bounds);
  host->controller->lpVtbl->put_Bounds(host->controller, bounds);
}

void release_webview(Host* host) {
  if (!host) return;
  if (host->webview) {
    host->webview->lpVtbl->Release(host->webview);
    host->webview = nullptr;
  }
  if (host->controller) {
    host->controller->lpVtbl->Close(host->controller);
    host->controller->lpVtbl->Release(host->controller);
    host->controller = nullptr;
  }
}

HRESULT STDMETHODCALLTYPE env_query(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*, REFIID, void**) {
  return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE env_add(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*) { return 1; }
ULONG STDMETHODCALLTYPE env_release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*) { return 1; }
HRESULT STDMETHODCALLTYPE ctl_query(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*, REFIID, void**) {
  return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE ctl_add(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*) { return 1; }
ULONG STDMETHODCALLTYPE ctl_release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*) { return 1; }

class EventHandler;

HRESULT STDMETHODCALLTYPE env_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* self, HRESULT result,
                                     ICoreWebView2Environment* environment);
HRESULT STDMETHODCALLTYPE ctl_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* self, HRESULT result,
                                     ICoreWebView2Controller* controller);

ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl kEnvVtbl{env_query, env_add, env_release, env_invoke};
ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl kControllerVtbl{ctl_query, ctl_add, ctl_release, ctl_invoke};

class EventHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                     public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
 public:
  explicit EventHandler(Host* host)
      : ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler{&kEnvVtbl},
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler{&kControllerVtbl},
        host_(host) {}

  EventHandler(const EventHandler&) = delete;
  EventHandler& operator=(const EventHandler&) = delete;

  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environment_handler() { return this; }
  ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* controller_handler() { return this; }
  Host* host() const { return host_; }

 private:
  Host* host_ = nullptr;
};

HRESULT STDMETHODCALLTYPE env_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* self, HRESULT result,
                                     ICoreWebView2Environment* environment) {
  auto* handler = static_cast<EventHandler*>(self);
  Host* host = handler->host();
  if (FAILED(result) || !environment) {
    log_error("WebView2: среда не создалась");
    host->webviewFailed = true;
    InvalidateRect(host->window, nullptr, TRUE);
    return S_OK;
  }
  return environment->lpVtbl->CreateCoreWebView2Controller(environment, host->window, handler->controller_handler());
}

HRESULT STDMETHODCALLTYPE ctl_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* self, HRESULT result,
                                     ICoreWebView2Controller* controller) {
  auto* handler = static_cast<EventHandler*>(self);
  Host* host = handler->host();
  if (FAILED(result) || !controller) {
    log_error("WebView2: контроллер не создался");
    host->webviewFailed = true;
    InvalidateRect(host->window, nullptr, TRUE);
    return S_OK;
  }
  host->controller = controller;
  host->controller->lpVtbl->AddRef(host->controller);
  host->controller->lpVtbl->get_CoreWebView2(host->controller, &host->webview);
  if (host->webview) {
    host->webview->lpVtbl->AddRef(host->webview);
    host->webview->lpVtbl->Navigate(host->webview, host->url.c_str());
  }
  host->controller->lpVtbl->put_IsVisible(host->controller, TRUE);
  resize_browser(host);
  log_info("WebView2 открыл интерфейс Helios");
  return S_OK;
}

LRESULT CALLBACK wnd_proc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
  Host* host = host_of(window);
  switch (msg) {
    case WM_ERASEBKGND: {
      RECT client;
      GetClientRect(window, &client);
      HBRUSH brush = CreateSolidBrush(RGB(16, 19, 24));
      FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
      DeleteObject(brush);
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(window, &ps);
      if (host && host->webviewFailed) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(235, 230, 220));
        RECT box{};
        GetClientRect(window, &box);
        DrawTextW(dc,
                  L"Нужен Microsoft Edge WebView2 — тот же движок, что рисует интерфейс в браузере.\n"
                  L"Установите WebView2 Runtime и откройте Helios снова.\n"
                  L"https://go.microsoft.com/fwlink/p/?LinkId=2124703",
                  -1, &box, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
      }
      EndPaint(window, &ps);
      return 0;
    }
    case WM_SIZE:
      if (host) resize_browser(host);
      return 0;
    case WM_TIMER:
      if (host && host->hardware) host->hardware->ensure();
      return 0;
    case WM_CLOSE:
      if (host) release_webview(host);
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

HMODULE load_webview_loader() {
  wchar_t module[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (length > 0) {
    std::filesystem::path dir = std::filesystem::path(std::wstring(module, length)).parent_path();
    if (HMODULE handle = LoadLibraryW((dir / L"WebView2Loader.dll").c_str())) return handle;
  }
  return LoadLibraryW(L"WebView2Loader.dll");
}

}  // namespace

bool helios_relaunch_elevated() {
  wchar_t path[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
  const std::filesystem::path exe(path);
  const std::wstring directory = exe.parent_path().wstring();
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb = L"runas";
  info.lpFile = path;
  info.lpParameters = L"--hardware";
  info.lpDirectory = directory.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    log_error("Windows не подняла Helios от администратора, код " + std::to_string(GetLastError()));
    return false;
  }
  if (info.hProcess) CloseHandle(info.hProcess);
  log_info("запущен второй Helios с правами администратора");
  return true;
}

void helios_close_window() {
  HWND window = FindWindowW(L"HeliosHost", nullptr);
  if (window) PostMessageW(window, WM_CLOSE, 0, 0);
}

int run_helios_window(const std::string& url, HardwareSupervisor* hardware) {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
    log_error("не удалось поднять COM для окна");
  }

  Host host;
  host.hardware = hardware;
  host.url = wide(url);

  WNDCLASSW wc{};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_HELIOS));
  if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = CreateSolidBrush(RGB(16, 19, 24));
  wc.lpszClassName = L"HeliosHost";
  RegisterClassW(&wc);

  host.window = CreateWindowExW(0, L"HeliosHost", L"Helios — свет и воздух", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                                CW_USEDEFAULT, 1440, 900, nullptr, nullptr, wc.hInstance, nullptr);
  if (!host.window) {
    log_error("не удалось создать окно Helios");
    if (SUCCEEDED(com)) CoUninitialize();
    return 1;
  }
  SetWindowLongPtrW(host.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&host));
  BOOL dark = TRUE;
  DwmSetWindowAttribute(host.window, kDarkTitle, &dark, sizeof(dark));
  ShowWindow(host.window, SW_SHOW);
  UpdateWindow(host.window);

  EventHandler handler(&host);
  HMODULE loader = load_webview_loader();
  CreateEnvFn createEnv = nullptr;
  if (loader) {
    createEnv = reinterpret_cast<CreateEnvFn>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"));
  }
  if (!createEnv) {
    log_error("нет WebView2Loader.dll рядом с helios.exe");
    host.webviewFailed = true;
    InvalidateRect(host.window, nullptr, TRUE);
  } else {
    wchar_t module[MAX_PATH];
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const auto userData = std::filesystem::path(std::wstring(module)).parent_path() / L"webview-data";
    std::error_code error;
    std::filesystem::create_directories(userData, error);
    const HRESULT created = createEnv(nullptr, userData.wstring().c_str(), nullptr, handler.environment_handler());
    if (FAILED(created)) {
      log_error("CreateCoreWebView2EnvironmentWithOptions не удался");
      host.webviewFailed = true;
      InvalidateRect(host.window, nullptr, TRUE);
    }
  }

  SetTimer(host.window, kTimerId, 400, nullptr);
  log_info("окно Helios открыто");

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  release_webview(&host);
  if (loader) FreeLibrary(loader);
  if (com == S_OK) CoUninitialize();
  log_info("окно Helios закрыто");
  return 0;
}

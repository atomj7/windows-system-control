#include "win_host.hpp"

#include "engine.hpp"
#include "json.hpp"
#include "log.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr int kDevicesId = 201;
constexpr int kRescanId = 202;
constexpr int kHexId = 203;
constexpr int kSpeedId = 204;
constexpr int kBrightId = 205;
constexpr int kDutyId = 206;
constexpr int kEffectId = 300;
constexpr int kModeId = 320;
constexpr int kTimerId = 1;

const wchar_t* kEffects[] = {L"Статичный", L"Дыхание", L"Спектр", L"Радуга", L"Волна", L"Выкл"};
const char* kEffectKeys[] = {"static", "breathe", "spectrum", "rainbow", "wave", "off"};
const wchar_t* kModes[] = {L"Авто", L"Кривая", L"Ручной"};
const char* kModeKeys[] = {"auto", "curve", "manual"};

COLORREF rgb(int r, int g, int b) { return RGB(r, g, b); }

std::wstring wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  return out;
}

std::string utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
  return out;
}

std::string jstr_of(const Json* object, const char* key) {
  if (!object) return {};
  const Json* value = object->find(key);
  return value && value->is_string() ? value->as_string() : std::string{};
}

double jnum_of(const Json* object, const char* key, double fallback = 0) {
  if (!object) return fallback;
  const Json* value = object->find(key);
  return value && value->is_number() ? value->as_number() : fallback;
}

const Json* jobj(const Json& root, const char* key) {
  const Json* value = root.find(key);
  return value && value->is_object() ? value : nullptr;
}

const Json* at_id(const Json* array, const std::string& id) {
  if (!array || !array->is_array()) return nullptr;
  for (const auto& item : array->items()) {
    if (jstr_of(&item, "id") == id) return &item;
  }
  return nullptr;
}

struct Rgb {
  int r = 227, g = 145, b = 74;
};

Rgb hsv_to_rgb(double h, double s, double v) {
  const double c = v * s;
  const double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1));
  const double m = v - c;
  double r = 0, g = 0, b = 0;
  if (h < 60) {
    r = c;
    g = x;
  } else if (h < 120) {
    r = x;
    g = c;
  } else if (h < 180) {
    g = c;
    b = x;
  } else if (h < 240) {
    g = x;
    b = c;
  } else if (h < 300) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  return {static_cast<int>(std::lround((r + m) * 255)), static_cast<int>(std::lround((g + m) * 255)),
          static_cast<int>(std::lround((b + m) * 255))};
}

void rgb_to_hsv(const Rgb& color, double& h, double& s, double& v) {
  const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
  const double max = std::max(r, std::max(g, b));
  const double min = std::min(r, std::min(g, b));
  const double d = max - min;
  h = 0;
  if (d != 0) {
    if (max == r) h = std::fmod((g - b) / d, 6.0);
    else if (max == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
  }
  s = max == 0 ? 0 : d / max;
  v = max;
}

Rgb parse_hex(const std::string& hex) {
  if (hex.size() < 7) return {};
  return {std::stoi(hex.substr(1, 2), nullptr, 16), std::stoi(hex.substr(3, 2), nullptr, 16),
          std::stoi(hex.substr(5, 2), nullptr, 16)};
}

std::string to_hex(const Rgb& color) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.r, color.g, color.b);
  return buffer;
}

struct Host {
  Engine* engine = nullptr;
  HWND window = nullptr;
  HWND list = nullptr;
  HWND title = nullptr;
  HWND note = nullptr;
  HWND sensors = nullptr;
  HWND hex = nullptr;
  HWND speed = nullptr;
  HWND bright = nullptr;
  HWND duty = nullptr;
  HWND fanLabel = nullptr;
  HWND computer = nullptr;
  std::vector<std::string> ids;
  std::string selected;
  std::string zoneId;
  std::string fanId;
  double hue = 24;
  double sat = 0.72;
  double val = 0.89;
  bool dragging = false;
  double generation = -1;
  RECT sv{312, 250, 548, 430};
  RECT hueBar{312, 442, 548, 458};
  RECT chart{860, 90, 1248, 620};
};

Host* host_of(HWND window) { return reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA)); }

void set_text(HWND hwnd, const std::string& text) { SetWindowTextW(hwnd, wide(text).c_str()); }

const Json* selected_device(Host* host, const Json& state) {
  const Json* devices = state.find("devices");
  if (host->selected.empty()) return nullptr;
  return at_id(devices, host->selected);
}

void apply_zone(Host* host, Json body) {
  if (!host->engine || host->selected.empty() || host->zoneId.empty()) return;
  host->engine->patchZone(host->selected, host->zoneId, body);
}

void apply_fan(Host* host, Json body) {
  if (!host->engine || host->selected.empty() || host->fanId.empty()) return;
  host->engine->patchFan(host->selected, host->fanId, body);
}

void commit_color(Host* host) {
  const Rgb color = hsv_to_rgb(host->hue, host->sat, host->val);
  apply_zone(host, Json::object().set("color", jstr(to_hex(color))));
  set_text(host->hex, to_hex(color));
  InvalidateRect(host->window, &host->sv, FALSE);
}

void fill_devices(Host* host, const Json& state) {
  const double generation = jnum_of(&state, "generation", -1);
  if (generation == host->generation && !host->ids.empty()) return;
  host->generation = generation;
  SendMessageW(host->list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(host->list, LB_RESETCONTENT, 0, 0);
  host->ids.clear();
  const Json* devices = state.find("devices");
  if (devices && devices->is_array()) {
    for (const auto& device : devices->items()) {
      host->ids.push_back(jstr_of(&device, "id"));
      SendMessageW(host->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(jstr_of(&device, "name")).c_str()));
    }
  }
  int index = 0;
  for (size_t i = 0; i < host->ids.size(); ++i) {
    if (host->ids[i] == host->selected) index = static_cast<int>(i);
  }
  if (index >= 0 && !host->ids.empty()) {
    SendMessageW(host->list, LB_SETCURSEL, index, 0);
    host->selected = host->ids[static_cast<size_t>(index)];
  }
  SendMessageW(host->list, WM_SETREDRAW, TRUE, 0);
}

void refresh_stage(Host* host, const Json& state) {
  const Json* platform = jobj(state, "platform");
  const std::string computer = jstr_of(platform, "computer");
  const std::string detail = jstr_of(platform, "detail");
  set_text(host->computer, computer.empty() ? "Helios" : computer + (detail.empty() ? "" : "\n" + detail));
  const Json* device = selected_device(host, state);
  if (!device) {
    set_text(host->title, "Нет устройств");
    set_text(host->note, "Нажми «Обзор», если список пуст.");
    set_text(host->sensors, "");
    return;
  }
  set_text(host->title, jstr_of(device, "name"));
  set_text(host->note, jstr_of(device, "note"));
  std::string sensors;
  if (const Json* list = device->find("sensors"); list && list->is_array()) {
    for (const auto& sensor : list->items()) {
      char line[128];
      std::snprintf(line, sizeof(line), "%s  %.1f %s\n", jstr_of(&sensor, "label").c_str(), jnum_of(&sensor, "value"),
                    jstr_of(&sensor, "unit").c_str());
      sensors += line;
    }
  }
  set_text(host->sensors, sensors.empty() ? "Нет датчиков" : sensors);

  const Json* zones = device->find("zones");
  const Json* zone = nullptr;
  if (zones && zones->is_array() && !zones->items().empty()) {
    zone = at_id(zones, host->zoneId);
    if (!zone) {
      zone = &zones->items().front();
      host->zoneId = jstr_of(zone, "id");
    }
    if (!host->dragging) {
      try {
        const Rgb color = parse_hex(jstr_of(zone, "color"));
        rgb_to_hsv(color, host->hue, host->sat, host->val);
        set_text(host->hex, jstr_of(zone, "color"));
        SendMessageW(host->speed, TBM_SETPOS, TRUE, static_cast<LPARAM>(jnum_of(zone, "speed")));
        SendMessageW(host->bright, TBM_SETPOS, TRUE, static_cast<LPARAM>(jnum_of(zone, "brightness")));
      } catch (...) {
      }
    }
  }

  const Json* fans = device->find("fans");
  const Json* fan = nullptr;
  if (fans && fans->is_array() && !fans->items().empty()) {
    fan = at_id(fans, host->fanId);
    if (!fan) {
      fan = &fans->items().front();
      host->fanId = jstr_of(fan, "id");
    }
    char label[160];
    std::snprintf(label, sizeof(label), "%s   %.0f об/мин   %.0f%%", jstr_of(fan, "name").c_str(), jnum_of(fan, "rpm"),
                  jnum_of(fan, "duty"));
    set_text(host->fanLabel, label);
    if (!host->dragging) SendMessageW(host->duty, TBM_SETPOS, TRUE, static_cast<LPARAM>(jnum_of(fan, "duty")));
  } else {
    set_text(host->fanLabel, "Нет каналов вентилятора");
  }
  InvalidateRect(host->window, &host->chart, FALSE);
}

void paint_picker(HDC dc, Host* host) {
  const RECT sv = host->sv;
  for (int y = sv.top; y < sv.bottom; y += 2) {
    for (int x = sv.left; x < sv.right; x += 2) {
      const double s = static_cast<double>(x - sv.left) / std::max(1L, sv.right - sv.left);
      const double v = 1.0 - static_cast<double>(y - sv.top) / std::max(1L, sv.bottom - sv.top);
      const Rgb color = hsv_to_rgb(host->hue, s, v);
      SetPixel(dc, x, y, rgb(color.r, color.g, color.b));
    }
  }
  const RECT hue = host->hueBar;
  for (int x = hue.left; x < hue.right; ++x) {
    const Rgb color = hsv_to_rgb(360.0 * (x - hue.left) / std::max(1L, hue.right - hue.left), 1, 1);
    for (int y = hue.top; y < hue.bottom; ++y) SetPixel(dc, x, y, rgb(color.r, color.g, color.b));
  }
}

void paint_chart(HDC dc, Host* host) {
  RECT box = host->chart;
  HBRUSH back = CreateSolidBrush(rgb(18, 22, 27));
  FillRect(dc, &box, back);
  DeleteObject(back);
  if (!host->engine) return;
  const Json history = host->engine->history(180);
  const Json* series = history.find("series");
  if (!series || !series->is_array()) return;
  const COLORREF palette[] = {rgb(227, 145, 74), rgb(126, 184, 178), rgb(212, 101, 93), rgb(235, 230, 220)};
  int colorIndex = 0;
  HPEN axis = CreatePen(PS_SOLID, 1, rgb(60, 66, 74));
  SelectObject(dc, axis);
  for (int i = 0; i < 4; ++i) {
    const int y = box.top + 12 + i * (box.bottom - box.top - 28) / 3;
    MoveToEx(dc, box.left + 8, y, nullptr);
    LineTo(dc, box.right - 8, y);
  }
  DeleteObject(axis);
  for (const auto& item : series->items()) {
    if (jstr_of(&item, "kind") != "temperature") continue;
    const Json* points = item.find("points");
    if (!points || !points->is_array() || points->items().size() < 2) continue;
    double minT = 1e18, maxT = -1e18, minV = 1e18, maxV = -1e18;
    for (const auto& point : points->items()) {
      if (!point.is_array() || point.items().size() < 2) continue;
      minT = std::min(minT, point.items()[0].as_number());
      maxT = std::max(maxT, point.items()[0].as_number());
      minV = std::min(minV, point.items()[1].as_number());
      maxV = std::max(maxV, point.items()[1].as_number());
    }
    if (maxT - minT < 1) maxT = minT + 1;
    if (maxV - minV < 1) {
      minV -= 1;
      maxV += 1;
    }
    HPEN pen = CreatePen(PS_SOLID, 2, palette[colorIndex++ % 4]);
    SelectObject(dc, pen);
    bool first = true;
    const int left = box.left + 12, top = box.top + 12;
    const int width = box.right - box.left - 24, height = box.bottom - box.top - 28;
    for (const auto& point : points->items()) {
      if (!point.is_array() || point.items().size() < 2) continue;
      const double t = point.items()[0].as_number();
      const double value = point.items()[1].as_number();
      const int x = left + static_cast<int>((t - minT) / (maxT - minT) * width);
      const int y = top + static_cast<int>((maxV - value) / (maxV - minV) * height);
      if (first) {
        MoveToEx(dc, x, y, nullptr);
        first = false;
      } else {
        LineTo(dc, x, y);
      }
    }
    DeleteObject(pen);
  }
}

void refresh(Host* host) {
  if (!host->engine) return;
  try {
    const Json state = host->engine->state();
    fill_devices(host, state);
    refresh_stage(host, state);
  } catch (const std::exception& ex) {
    log_error(std::string("окно: ") + ex.what());
  }
}

void hit_picker(Host* host, int x, int y, bool commit) {
  POINT p{x, y};
  if (PtInRect(&host->sv, p)) {
    host->sat = std::clamp(static_cast<double>(x - host->sv.left) / (host->sv.right - host->sv.left), 0.0, 1.0);
    host->val = std::clamp(1.0 - static_cast<double>(y - host->sv.top) / (host->sv.bottom - host->sv.top), 0.0, 1.0);
    if (commit) commit_color(host);
    else InvalidateRect(host->window, &host->sv, FALSE);
  } else if (PtInRect(&host->hueBar, p)) {
    host->hue = std::clamp(360.0 * (x - host->hueBar.left) / (host->hueBar.right - host->hueBar.left), 0.0, 359.0);
    if (commit) commit_color(host);
    else InvalidateRect(host->window, &host->sv, FALSE);
  }
}

LRESULT CALLBACK wnd_proc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
  Host* host = host_of(window);
  switch (msg) {
    case WM_CREATE:
      return 0;
    case WM_ERASEBKGND: {
      RECT client;
      GetClientRect(window, &client);
      HBRUSH brush = CreateSolidBrush(rgb(16, 19, 24));
      FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
      DeleteObject(brush);
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(window, &ps);
      if (host) {
        RECT hit{};
        if (IntersectRect(&hit, &ps.rcPaint, &host->sv) || IntersectRect(&hit, &ps.rcPaint, &host->hueBar)) paint_picker(dc, host);
        if (IntersectRect(&hit, &ps.rcPaint, &host->chart)) paint_chart(dc, host);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, rgb(235, 230, 220));
        TextOutW(dc, 312, 220, L"Цвет", 4);
        TextOutW(dc, 860, 60, L"Температура", 11);
      }
      EndPaint(window, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN:
      if (host) {
        host->dragging = true;
        SetCapture(window);
        hit_picker(host, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
      }
      return 0;
    case WM_MOUSEMOVE:
      if (host && host->dragging) hit_picker(host, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
      return 0;
    case WM_LBUTTONUP:
      if (host && host->dragging) {
        host->dragging = false;
        ReleaseCapture();
        hit_picker(host, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
      }
      return 0;
    case WM_HSCROLL:
      if (host) {
        if (reinterpret_cast<HWND>(lParam) == host->speed) {
          apply_zone(host, Json::object().set("speed", jnum(SendMessageW(host->speed, TBM_GETPOS, 0, 0))));
        } else if (reinterpret_cast<HWND>(lParam) == host->bright) {
          apply_zone(host, Json::object().set("brightness", jnum(SendMessageW(host->bright, TBM_GETPOS, 0, 0))));
        } else if (reinterpret_cast<HWND>(lParam) == host->duty) {
          apply_fan(host, Json::object().set("mode", jstr("manual")).set("duty", jnum(SendMessageW(host->duty, TBM_GETPOS, 0, 0))));
        }
      }
      return 0;
    case WM_COMMAND:
      if (!host) break;
      if (LOWORD(wParam) == kDevicesId && HIWORD(wParam) == LBN_SELCHANGE) {
        const int index = static_cast<int>(SendMessageW(host->list, LB_GETCURSEL, 0, 0));
        if (index >= 0 && static_cast<size_t>(index) < host->ids.size()) {
          host->selected = host->ids[static_cast<size_t>(index)];
          host->zoneId.clear();
          host->fanId.clear();
          refresh(host);
        }
        return 0;
      }
      if (LOWORD(wParam) == kRescanId) {
        host->engine->requestScan();
        return 0;
      }
      if (LOWORD(wParam) >= kEffectId && LOWORD(wParam) < kEffectId + 6) {
        apply_zone(host, Json::object().set("effect", jstr(kEffectKeys[LOWORD(wParam) - kEffectId])));
        return 0;
      }
      if (LOWORD(wParam) >= kModeId && LOWORD(wParam) < kModeId + 3) {
        apply_fan(host, Json::object().set("mode", jstr(kModeKeys[LOWORD(wParam) - kModeId])));
        return 0;
      }
      if (LOWORD(wParam) == kHexId && HIWORD(wParam) == EN_KILLFOCUS) {
        wchar_t buffer[16] = {};
        GetWindowTextW(host->hex, buffer, 16);
        const std::string hex = utf8(buffer);
        if (hex.size() == 7 && hex[0] == '#') apply_zone(host, Json::object().set("color", jstr(hex)));
        return 0;
      }
      break;
    case WM_TIMER:
      if (host) refresh(host);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, msg, wParam, lParam);
}

HWND child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
  return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                         id ? reinterpret_cast<HMENU>(static_cast<intptr_t>(id)) : nullptr, GetModuleHandleW(nullptr), nullptr);
}

}  // namespace

int run_helios_window(Engine& engine) {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  Host host;
  host.engine = &engine;

  WNDCLASSW wc{};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(RGB(16, 19, 24));
  wc.lpszClassName = L"HeliosHost";
  RegisterClassW(&wc);

  host.window = CreateWindowExW(0, L"HeliosHost", L"Helios — свет и воздух",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr,
                                wc.hInstance, nullptr);
  if (!host.window) {
    log_error("не удалось создать окно Helios");
    return 1;
  }
  SetWindowLongPtrW(host.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&host));

  host.computer = child(host.window, L"STATIC", L"Helios", SS_LEFT, 20, 12, 820, 40, 0);
  child(host.window, L"BUTTON", L"Обзор", 0, 1140, 16, 100, 28, kRescanId);
  host.list = child(host.window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 20, 64, 270, 620, kDevicesId);
  host.title = child(host.window, L"STATIC", L"", SS_LEFT, 312, 64, 520, 28, 0);
  host.note = child(host.window, L"STATIC", L"", SS_LEFT, 312, 96, 520, 72, 0);
  host.sensors = child(host.window, L"STATIC", L"", SS_LEFT, 312, 172, 520, 44, 0);
  host.hex = child(host.window, L"EDIT", L"#e3914a", WS_BORDER | ES_AUTOHSCROLL, 312, 470, 120, 24, kHexId);
  for (int i = 0; i < 6; ++i) {
    child(host.window, L"BUTTON", kEffects[i], 0, 312 + (i % 3) * 90, 504 + (i / 3) * 32, 86, 28, kEffectId + i);
  }
  host.speed = child(host.window, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS, 312, 580, 236, 28, kSpeedId);
  host.bright = child(host.window, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS, 312, 612, 236, 28, kBrightId);
  SendMessageW(host.speed, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
  SendMessageW(host.bright, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
  SendMessageW(host.speed, TBM_SETPOS, TRUE, 45);
  SendMessageW(host.bright, TBM_SETPOS, TRUE, 70);
  host.fanLabel = child(host.window, L"STATIC", L"", SS_LEFT, 312, 648, 400, 22, 0);
  for (int i = 0; i < 3; ++i) {
    child(host.window, L"BUTTON", kModes[i], 0, 312 + i * 80, 672, 74, 26, kModeId + i);
  }
  host.duty = child(host.window, TRACKBAR_CLASSW, L"", TBS_AUTOTICKS, 560, 668, 220, 28, kDutyId);
  SendMessageW(host.duty, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));

  SetTimer(host.window, kTimerId, 400, nullptr);
  refresh(&host);
  ShowWindow(host.window, SW_SHOW);
  UpdateWindow(host.window);
  log_info("окно Helios открыто");

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  log_info("окно Helios закрыто");
  return 0;
}

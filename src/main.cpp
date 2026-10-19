#include "engine.hpp"
#include "http.hpp"
#include "platform.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

std::string executable_dir(const char* argv0) {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length > 0) return std::filesystem::path(buffer).parent_path().string();
#else
  char buffer[4096];
  const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length > 0) {
    buffer[length] = 0;
    return std::filesystem::path(buffer).parent_path().string();
  }
#endif
  return std::filesystem::absolute(argv0).parent_path().string();
}

std::string find_web_root(const std::string& exeDir) {
  const std::filesystem::path candidates[] = {
      std::filesystem::current_path() / "ui" / "dist",
      std::filesystem::path(exeDir) / "ui" / "dist",
      std::filesystem::path(exeDir) / ".." / "ui" / "dist",
      std::filesystem::path(exeDir) / ".." / ".." / "ui" / "dist",
  };
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate / "index.html", error)) return std::filesystem::weakly_canonical(candidate).string();
  }
  return (std::filesystem::current_path() / "ui" / "dist").string();
}

}  // namespace

int main(int argc, char** argv) {
  int port = 47621;
  bool simulate = false;
  bool openBrowser = true;
  std::string dataDir = "data";
#ifdef _WIN32
  bool defaultSimulate = false;
#else
  bool defaultSimulate = true;
#endif
  simulate = defaultSimulate;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need = [&](std::string& out) {
      if (i + 1 >= argc) return false;
      out = argv[++i];
      return true;
    };
    if (arg == "--port") {
      std::string value;
      if (!need(value)) return 1;
      port = std::stoi(value);
    } else if (arg == "--data") {
      if (!need(dataDir)) return 1;
    } else if (arg == "--simulate") {
      simulate = true;
    } else if (arg == "--hardware") {
      simulate = false;
    } else if (arg == "--no-browser") {
      openBrowser = false;
    } else if (arg == "--help") {
      std::cout << "Helios\n  --port 47621\n  --data data\n  --simulate\n  --hardware\n  --no-browser\n";
      return 0;
    }
  }

  auto platform = make_platform(simulate);
  Engine engine(std::move(platform), dataDir);
  engine.start();
  const std::string web = find_web_root(executable_dir(argv[0]));
  const std::string url = "http://127.0.0.1:" + std::to_string(port);
  std::cout << "Helios слушает " << url << "\n";
  std::cout << "Интерфейс: " << web << "\n";
#ifdef _WIN32
  if (openBrowser) {
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
#else
  (void)openBrowser;
#endif
  const int code = serve_http(engine, "127.0.0.1", port, web);
  if (code != 0) std::cerr << "Не удалось открыть порт " << port << "\n";
  return code;
}

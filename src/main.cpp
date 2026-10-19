#include "engine.hpp"
#include "http.hpp"
#include "ipc.hpp"
#include "log.hpp"
#include "net.hpp"
#include "platform.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include "win_host.hpp"
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

std::filesystem::path executable_path(const char* argv0) {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length > 0) return std::filesystem::path(std::wstring(buffer, length));
#else
  char buffer[4096];
  const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length > 0) {
    buffer[length] = 0;
    return std::filesystem::path(buffer);
  }
#endif
  return std::filesystem::absolute(argv0);
}

std::string find_web_root(const std::filesystem::path& exeDir) {
  const std::filesystem::path candidates[] = {
      std::filesystem::current_path() / "ui" / "dist",
      exeDir / "ui" / "dist",
      exeDir / "dist",
      exeDir / ".." / "ui" / "dist",
      exeDir / ".." / ".." / "ui" / "dist",
  };
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate / "index.html", error)) return std::filesystem::weakly_canonical(candidate).string();
  }
  return (exeDir / "ui" / "dist").string();
}

#ifdef _WIN32
LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
  if (info && info->ExceptionRecord) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode));
    log_error(std::string("авария Windows 0x") + buffer);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}  // namespace

int helios_main(int argc, char** argv) {
  int port = 47621;
  bool simulate = false;
  bool openBrowser = true;
  bool worker = false;
  std::string dataDir;
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
    } else if (arg == "--worker") {
      worker = true;
    } else if (arg == "--no-browser") {
      openBrowser = false;
    } else if (arg == "--help") {
      std::cout << "Helios\n  --port 47621\n  --data data\n  --simulate\n  --hardware\n  --worker\n  --no-browser\n";
      return 0;
    }
  }

  try {
    const auto exePath = executable_path(argc > 0 ? argv[0] : "");
    const auto exeDir = exePath.parent_path();
#ifdef _WIN32
    SetCurrentDirectoryW(exeDir.wstring().c_str());
    SetUnhandledExceptionFilter(crash_filter);
#endif
    if (dataDir.empty()) dataDir = (exeDir / "data").string();

    if (worker) {
      log_open(exeDir / "helios-worker.log");
      log_info("модуль железа из " + exeDir.string());
      return run_hardware_worker(dataDir, simulate);
    }

    log_open(exeDir / "helios.log");
    if (!net_startup()) {
      show_fatal("Не удалось поднять сетевой стек Windows.");
      return 1;
    }
    log_info("старт из " + exeDir.string());
    const std::string web = find_web_root(exeDir);
    log_info(std::string("режим ") + (simulate ? "стенд" : "железо") + ", интерфейс " + web);

#ifdef _WIN32
    std::unique_ptr<HardwareSupervisor> hardware;
    std::unique_ptr<Platform> platform;
    if (simulate) {
      platform = make_platform(true);
    } else {
      hardware = std::make_unique<HardwareSupervisor>(exePath, dataDir);
      hardware->ensure();
      platform = make_ipc_platform(dataDir);
      log_info("окно не грузит NVML и DXGI: железо в helios.exe --worker");
    }
    Engine engine(std::move(platform), dataDir);
#else
    Engine engine(make_platform(simulate), dataDir);
#endif
    engine.start();
    const std::string url = "http://127.0.0.1:" + std::to_string(port);
    int listenCode = 0;
    std::mutex listenMu;
    std::condition_variable listenCv;
    bool listening = false;
    std::thread server([&] {
      listenCode = serve_http(engine, "127.0.0.1", port, web, [&] {
        log_info("слушает " + url);
        {
          std::lock_guard lock(listenMu);
          listening = true;
        }
        listenCv.notify_all();
#ifndef _WIN32
        (void)openBrowser;
        std::cout << "Helios слушает " << url << "\n";
#endif
      });
      if (listenCode != 0) log_error("HTTP-служба остановилась с кодом " + std::to_string(listenCode));
      listenCv.notify_all();
    });

#ifdef _WIN32
    (void)openBrowser;
    {
      std::unique_lock lock(listenMu);
      listenCv.wait_for(lock, std::chrono::seconds(3), [&] { return listening || listenCode != 0; });
    }
    if (!listening) {
      engine.stop();
      interrupt_http();
      if (server.joinable()) server.join();
      if (hardware) hardware->stop();
      show_fatal("Не удалось открыть порт " + std::to_string(port) + ".\nСмотри helios.log в папке программы.");
      return listenCode != 0 ? listenCode : 1;
    }
    const int windowCode = run_helios_window(url, hardware.get());
    engine.stop();
    interrupt_http();
    if (server.joinable()) server.join();
    if (hardware) hardware->stop();
    return windowCode;
#else
    if (server.joinable()) server.join();
    if (listenCode != 0) {
      std::cerr << "Не удалось открыть порт " << port << "\n";
      return listenCode;
    }
    return 0;
#endif
  } catch (const std::exception& ex) {
    if (worker) {
      log_error(std::string("модуль железа остановился: ") + ex.what());
      return 1;
    }
    show_fatal(std::string("Helios остановился: ") + ex.what());
    return 1;
  } catch (...) {
    if (worker) {
      log_error("модуль железа остановился без текста ошибки.");
      return 1;
    }
    show_fatal("Helios остановился без текста ошибки.");
    return 1;
  }
}

int main(int argc, char** argv) { return helios_main(argc, argv); }

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return helios_main(__argc, __argv); }
#endif

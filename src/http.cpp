#include "http.hpp"

#include "log.hpp"
#include "net.hpp"
#include "util.hpp"

#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace {

std::string url_decode(std::string_view in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      out.push_back(static_cast<char>(std::stoi(std::string(in.substr(i + 1, 2)), nullptr, 16)));
      i += 2;
    } else if (in[i] == '+') {
      out.push_back(' ');
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

std::unordered_map<std::string, std::string> parse_query(std::string_view query) {
  std::unordered_map<std::string, std::string> out;
  while (!query.empty()) {
    const auto amp = query.find('&');
    const auto part = query.substr(0, amp);
    const auto eq = part.find('=');
    if (eq != std::string_view::npos) out[url_decode(part.substr(0, eq))] = url_decode(part.substr(eq + 1));
    if (amp == std::string_view::npos) break;
    query.remove_prefix(amp + 1);
  }
  return out;
}

const char* mime_for(const std::filesystem::path& path) {
  const auto ext = path.extension().string();
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".js") return "text/javascript; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".json") return "application/json";
  if (ext == ".woff2") return "font/woff2";
  if (ext == ".png") return "image/png";
  if (ext == ".ico") return "image/x-icon";
  return "application/octet-stream";
}

void send_response(Socket socket, int status, const std::string& type, const std::string& body, bool cache) {
  std::ostringstream head;
  head << "HTTP/1.1 " << status << (status == 200 ? " OK" : status == 400 ? " Bad Request" : status == 404 ? " Not Found" : " Error")
       << "\r\n";
  head << "Content-Type: " << type << "\r\n";
  head << "Content-Length: " << body.size() << "\r\n";
  head << "Access-Control-Allow-Origin: *\r\n";
  head << "Access-Control-Allow-Methods: GET, POST, PATCH, DELETE, OPTIONS\r\n";
  head << "Access-Control-Allow-Headers: Content-Type\r\n";
  head << "Cache-Control: " << (cache ? "public, max-age=3600" : "no-store") << "\r\n";
  head << "Connection: close\r\n\r\n";
  const std::string header = head.str();
  send_all(socket, reinterpret_cast<const uint8_t*>(header.data()), header.size());
  if (!body.empty()) send_all(socket, reinterpret_cast<const uint8_t*>(body.data()), body.size());
}

void send_json(Socket socket, const Json& body, int status = 200) { send_response(socket, status, "application/json; charset=utf-8", body.dump(), false); }

std::vector<std::string> split_path(std::string path) {
  std::vector<std::string> parts;
  if (!path.empty() && path.front() == '/') path.erase(path.begin());
  std::stringstream stream(path);
  std::string part;
  while (std::getline(stream, part, '/')) {
    if (!part.empty()) parts.push_back(url_decode(part));
  }
  return parts;
}

bool read_request(Socket socket, std::string& method, std::string& path, std::string& query, std::string& body) {
  std::string data;
  char buffer[4096];
  while (data.find("\r\n\r\n") == std::string::npos && data.size() < 64 * 1024) {
    size_t got = 0;
    if (!recv_some(socket, reinterpret_cast<uint8_t*>(buffer), sizeof(buffer), got, 5000)) return false;
    data.append(buffer, got);
  }
  const auto headerEnd = data.find("\r\n\r\n");
  if (headerEnd == std::string::npos) return false;
  std::string headers = data.substr(0, headerEnd);
  body = data.substr(headerEnd + 4);
  std::stringstream stream(headers);
  std::string requestLine;
  std::getline(stream, requestLine);
  if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();
  std::stringstream line(requestLine);
  std::string target;
  line >> method >> target;
  const auto q = target.find('?');
  path = q == std::string::npos ? target : target.substr(0, q);
  query = q == std::string::npos ? "" : target.substr(q + 1);
  int contentLength = 0;
  std::string header;
  while (std::getline(stream, header)) {
    if (!header.empty() && header.back() == '\r') header.pop_back();
    const auto colon = header.find(':');
    if (colon == std::string::npos) continue;
    if (lower_copy(header.substr(0, colon)) == "content-length") contentLength = std::atoi(header.substr(colon + 1).c_str());
  }
  if (contentLength < 0 || contentLength > 1024 * 256) return false;
  while (static_cast<int>(body.size()) < contentLength) {
    size_t got = 0;
    if (!recv_some(socket, reinterpret_cast<uint8_t*>(buffer), sizeof(buffer), got, 5000)) return false;
    body.append(buffer, got);
  }
  body.resize(static_cast<size_t>(contentLength));
  return true;
}

void stream_events(Socket socket, Engine& engine) {
  const std::string header =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-store\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: keep-alive\r\n\r\n";
  if (!send_all(socket, reinterpret_cast<const uint8_t*>(header.data()), header.size())) return;
  uint64_t seenMetrics = 0;
  uint64_t seenGeneration = 0;
  while (!engine.stopped()) {
    uint64_t metrics = 0;
    uint64_t generation = 0;
    Json payload;
    engine.waitForTick(seenMetrics, seenGeneration, 1000, metrics, generation, payload);
    if (engine.stopped()) break;
    const std::string frame = "event: tick\ndata: " + payload.dump() + "\r\n\r\n";
    if (!send_all(socket, reinterpret_cast<const uint8_t*>(frame.data()), frame.size())) break;
    seenMetrics = metrics;
    seenGeneration = generation;
  }
}

std::optional<Json> body_json(const std::string& body, std::string& error) {
  if (body.empty()) return Json::object();
  auto parsed = parse_json(body);
  if (!parsed || !parsed->is_object()) {
    error = "Нужен JSON-объект.";
    return std::nullopt;
  }
  return parsed;
}

void handle(Socket socket, Engine& engine, const std::filesystem::path& webRoot) {
  std::string method;
  std::string path;
  std::string query;
  std::string body;
  if (!read_request(socket, method, path, query, body)) {
    close_socket(socket);
    return;
  }
  if (method == "OPTIONS") {
    send_response(socket, 200, "text/plain", "", false);
    close_socket(socket);
    return;
  }
  const auto parts = split_path(path);
  auto queryMap = parse_query(query);

  if (method == "GET" && path == "/api/stream") {
    stream_events(socket, engine);
    close_socket(socket);
    return;
  }
  if (method == "GET" && path == "/api/state") {
    send_json(socket, engine.state());
  } else if (method == "GET" && path == "/api/history") {
    int seconds = 180;
    if (auto found = queryMap.find("seconds"); found != queryMap.end()) seconds = std::atoi(found->second.c_str());
    send_json(socket, engine.history(seconds));
  } else if (method == "POST" && path == "/api/scan") {
    engine.requestScan();
    send_json(socket, engine.state());
  } else if (parts.size() == 2 && parts[0] == "api" && parts[1] == "lighting" && method == "POST") {
    std::string error;
    auto json = body_json(body, error);
    send_json(socket, json ? engine.paintAll(*json) : Json::object().set("ok", jbool(false)).set("error", jstr(error)), json ? 200 : 400);
  } else if (parts.size() == 2 && parts[0] == "api" && parts[1] == "profiles" && method == "POST") {
    std::string error;
    auto json = body_json(body, error);
    const std::string name = json && json->find("name") ? json->find("name")->as_string() : "";
    send_json(socket, json ? engine.saveProfile(name) : Json::object().set("ok", jbool(false)).set("error", jstr(error)), json ? 200 : 400);
  } else if (parts.size() == 4 && parts[0] == "api" && parts[1] == "profiles" && parts[3] == "apply" && method == "POST") {
    send_json(socket, engine.applyProfile(parts[2]));
  } else if (parts.size() == 3 && parts[0] == "api" && parts[1] == "profiles" && method == "DELETE") {
    send_json(socket, engine.removeProfile(parts[2]));
  } else if (parts.size() == 5 && parts[0] == "api" && parts[1] == "devices" && parts[3] == "zones" && method == "PATCH") {
    std::string error;
    auto json = body_json(body, error);
    if (!json) send_json(socket, Json::object().set("ok", jbool(false)).set("error", jstr(error)), 400);
    else {
      Json result = engine.patchZone(parts[2], parts[4], *json);
      const bool ok = result.find("ok") && result.find("ok")->as_bool();
      send_json(socket, result, ok ? 200 : 400);
    }
  } else if (parts.size() == 5 && parts[0] == "api" && parts[1] == "devices" && parts[3] == "fans" && method == "PATCH") {
    std::string error;
    auto json = body_json(body, error);
    if (!json) send_json(socket, Json::object().set("ok", jbool(false)).set("error", jstr(error)), 400);
    else {
      Json result = engine.patchFan(parts[2], parts[4], *json);
      const bool ok = result.find("ok") && result.find("ok")->as_bool();
      send_json(socket, result, ok ? 200 : 400);
    }
  } else if (parts.size() == 4 && parts[0] == "api" && parts[1] == "devices" && parts[3] == "lighting" && method == "POST") {
    std::string error;
    auto json = body_json(body, error);
    if (!json) send_json(socket, Json::object().set("ok", jbool(false)).set("error", jstr(error)), 400);
    else {
      Json result = engine.paintDevice(parts[2], *json);
      const bool ok = result.find("ok") && result.find("ok")->as_bool();
      send_json(socket, result, ok ? 200 : 400);
    }
  } else if (method == "GET") {
    std::filesystem::path file = webRoot / (path == "/" ? "index.html" : path.substr(1));
    std::error_code error;
    file = std::filesystem::weakly_canonical(file, error);
    const auto root = std::filesystem::weakly_canonical(webRoot, error);
    const bool inside = !error && file.string().rfind(root.string(), 0) == 0;
    if (!inside || !std::filesystem::is_regular_file(file)) {
      const auto fallback = root / "index.html";
      if (path.rfind("/api/", 0) == 0) send_json(socket, Json::object().set("ok", jbool(false)).set("error", jstr("Нет такого запроса.")), 404);
      else if (std::filesystem::is_regular_file(fallback)) file = fallback;
      else {
        send_response(socket, 200, "text/html; charset=utf-8",
                      "<!doctype html><meta charset=\"utf-8\"><title>Helios</title><body style=\"font-family:sans-serif;background:#111;color:#eee;padding:32px\">"
                      "<p>Интерфейс ещё не собран. В каталоге ui выполни npm install и npm run build.</p></body>",
                      false);
        close_socket(socket);
        return;
      }
    }
    std::ifstream input(file, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const bool asset = file.string().find("/assets/") != std::string::npos;
    send_response(socket, 200, mime_for(file), content, asset);
  } else {
    send_json(socket, Json::object().set("ok", jbool(false)).set("error", jstr("Нет такого запроса.")), 404);
  }
  close_socket(socket);
}

}  // namespace

int serve_http(Engine& engine, const std::string& host, int port, const std::string& webRoot,
               const std::function<void()>& ready) {
  if (!net_startup()) {
    log_error("сокеты Windows не поднялись");
    return 1;
  }
  Socket server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server == kInvalidSocket) {
    log_error("не удалось создать сокет");
    return 1;
  }
  int reuse = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse), sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    close_socket(server);
    log_error("неверный адрес прослушивания");
    return 1;
  }
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(server, 32) != 0) {
#ifdef _WIN32
    log_error("порт " + std::to_string(port) + " занят или закрыт, код " + std::to_string(WSAGetLastError()));
#else
    log_error("порт " + std::to_string(port) + " занят или закрыт");
#endif
    close_socket(server);
    return 1;
  }
  if (ready) ready();
  const std::filesystem::path root = webRoot;
  while (!engine.stopped()) {
    sockaddr_in clientAddress{};
#ifdef _WIN32
    int length = sizeof(clientAddress);
#else
    socklen_t length = sizeof(clientAddress);
#endif
    Socket client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &length);
    if (client == kInvalidSocket) continue;
    std::thread([client, &engine, root] {
      try {
        handle(client, engine, root);
      } catch (const std::exception& ex) {
        log_error(std::string("запрос: ") + ex.what());
        close_socket(client);
      } catch (...) {
        log_error("запрос упал");
        close_socket(client);
      }
    }).detach();
  }
  close_socket(server);
  net_shutdown();
  return 0;
}

#pragma once

#include "engine.hpp"

#include <functional>
#include <string>

// Serves the JSON API and, when present, the built interface. Binds to loopback only.
int serve_http(Engine& engine, const std::string& host, int port, const std::string& webRoot,
               const std::function<void()>& ready = {});
void interrupt_http();

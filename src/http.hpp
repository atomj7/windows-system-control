#pragma once

#include "engine.hpp"

// Serves the JSON API and, when present, the built interface. Binds to loopback only.
int serve_http(Engine& engine, const std::string& host, int port, const std::string& webRoot);

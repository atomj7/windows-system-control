#pragma once

#include <filesystem>
#include <string>

void log_open(const std::filesystem::path& path);
void log_info(const std::string& message);
void log_error(const std::string& message);
void show_fatal(const std::string& message);

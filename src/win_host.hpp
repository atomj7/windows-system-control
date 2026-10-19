#pragma once

#include <string>

class HardwareSupervisor;

int run_helios_window(const std::string& url, HardwareSupervisor* hardware = nullptr);

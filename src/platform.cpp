#include "platform.hpp"

#include "mock_platform.hpp"

#ifdef _WIN32
#include "win_platform.hpp"
#endif

std::unique_ptr<Platform> make_platform(bool simulate) {
#ifdef _WIN32
  if (!simulate) return std::make_unique<WindowsPlatform>();
#else
  (void)simulate;
#endif
  return std::make_unique<MockPlatform>();
}

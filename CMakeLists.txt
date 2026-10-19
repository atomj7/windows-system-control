cmake_minimum_required(VERSION 3.20)
project(helios VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(helios_core
  src/json.cpp
  src/model.cpp
  src/curve.cpp
  src/catalog.cpp
  src/openrgb.cpp
  src/mock_platform.cpp
  src/platform.cpp
  src/engine.cpp
  src/http.cpp
  src/net.cpp
  src/log.cpp
)
target_include_directories(helios_core PUBLIC src)

if(WIN32)
  target_sources(helios_core PRIVATE
    src/win_platform.cpp
    src/win_nvml.cpp
    src/win_nzxt.cpp
  )
  target_compile_definitions(helios_core PUBLIC
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    NOMINMAX
  )
  target_link_libraries(helios_core PUBLIC
    ws2_32
    ole32
    oleaut32
    wbemuuid
    setupapi
    hid
    dxgi
    pdh
    shell32
    user32
    gdi32
    comctl32
  )
else()
  target_link_libraries(helios_core PUBLIC pthread)
endif()

if(MSVC)
  target_compile_options(helios_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(helios_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  add_executable(helios WIN32 src/main.cpp src/win_host.cpp)
else()
  add_executable(helios src/main.cpp)
endif()
target_link_libraries(helios PRIVATE helios_core)

add_executable(helios_tests src/tests/test_main.cpp)
target_link_libraries(helios_tests PRIVATE helios_core)

if(MINGW)
  foreach(target helios helios_tests)
    target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
  endforeach()
endif()

enable_testing()
add_test(NAME helios_tests COMMAND helios_tests)

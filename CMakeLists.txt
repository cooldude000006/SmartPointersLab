cmake_minimum_required(VERSION 3.14)
project(SmartPointersLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()

# Сборка с AddressSanitizer + UndefinedBehaviorSanitizer: -DSANITIZE=ON
# (работает в Linux/WSL с GCC или Clang; MinGW санитайзеры не поддерживает)
option(SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if (SANITIZE)
    if (MINGW OR MSVC)
        message(WARNING "SANITIZE=ON is supported only with GCC/Clang on Linux (use the WSL toolchain)")
    else ()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address,undefined)
    endif ()
endif ()

# ---------------------------------------------------------------------------
# Библиотека умных указателей (header-only: шаблоны целиком в заголовках)
# ---------------------------------------------------------------------------
add_library(SmartPointers INTERFACE)
target_include_directories(SmartPointers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (MSVC)
    target_compile_options(SmartPointers INTERFACE /W4)
else ()
    target_compile_options(SmartPointers INTERFACE -Wall -Wextra -Wpedantic)
endif ()

# ---------------------------------------------------------------------------
# Google Test
# ---------------------------------------------------------------------------
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Функциональные тесты
# ---------------------------------------------------------------------------
enable_testing()

add_executable(SmartPointersTests
        tests/TestUnqPtr.cpp
        tests/TestShrdPtr.cpp
)
target_link_libraries(SmartPointersTests PRIVATE SmartPointers gtest_main)

include(GoogleTest)
gtest_discover_tests(SmartPointersTests)

# ---------------------------------------------------------------------------
# Нагрузочные тесты (запускать в конфигурации Release)
# ---------------------------------------------------------------------------
add_executable(SmartPointersBenchmark
        benchmark/Benchmark.cpp
)
target_link_libraries(SmartPointersBenchmark PRIVATE SmartPointers)

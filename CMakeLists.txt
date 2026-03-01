cmake_minimum_required(VERSION 3.16)
project(PersistMemoryManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Основная библиотека (header-only) ────────────────────────────────────────
add_library(pmm INTERFACE)
target_include_directories(pmm INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Параметры компилятора
if(MSVC)
    target_compile_options(pmm INTERFACE /W4 /utf-8)
else()
    target_compile_options(pmm INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ─── Потоки (для тестов потокобезопасности) ───────────────────────────────────
find_package(Threads REQUIRED)

# ─── Тесты ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# ─── Примеры ──────────────────────────────────────────────────────────────────
add_subdirectory(examples)

# ─── Визуальное демо (опционально) ────────────────────────────────────────────
option(PMM_BUILD_DEMO "Build the visual demo application (requires OpenGL + GLFW)" OFF)
if(PMM_BUILD_DEMO)
    add_subdirectory(demo)
endif()

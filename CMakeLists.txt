cmake_minimum_required(VERSION 3.16)
project(PersistMemoryManager VERSION 0.57.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Основная библиотека (header-only) ────────────────────────────────────────
add_library(pmm INTERFACE)
target_include_directories(pmm INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/single_include/pmm
)

# Параметры компилятора
if(MSVC)
    target_compile_options(pmm INTERFACE /W4 /utf-8 /MP)
else()
    target_compile_options(pmm INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ─── Потоки (для тестов потокобезопасности) ───────────────────────────────────
find_package(Threads REQUIRED)

# ─── Тесты ────────────────────────────────────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
    # Test-only dependency graph stays inside BUILD_TESTING so non-test
    # configurations do not fetch or build Catch2.
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)
    if(MSVC)
        foreach(pmm_catch2_target Catch2 Catch2WithMain)
            if(TARGET ${pmm_catch2_target})
                target_compile_options(${pmm_catch2_target} PRIVATE /MP)
            endif()
        endforeach()
    endif()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    add_subdirectory(tests)
endif()

# ─── Примеры ──────────────────────────────────────────────────────────────────
option(PMM_BUILD_EXAMPLES "Build example executables" ON)
if(PMM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ─── Бенчмарки (опционально, Phase 5.3) ─────────────────────────────────────
option(PMM_BUILD_BENCHMARKS "Build Google Benchmark performance benchmarks" OFF)
if(PMM_BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmarks)
endif()

# ─── Визуальное демо (опционально) ────────────────────────────────────────────
option(PMM_BUILD_DEMO "Build the visual demo application (requires OpenGL + GLFW)" OFF)
if(PMM_BUILD_DEMO)
    add_subdirectory(demo)
endif()

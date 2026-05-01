cmake_minimum_required(VERSION 3.16)
project(PersistMemoryManager VERSION 6.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Локальный лимит параллелизма сборки (issue #371) ────────────────────────
# Локально (без переменной окружения CI) ограничиваем многопоточность сборки
# и компиляции MSVC значением PMM_LOCAL_BUILD_JOBS, чтобы один разработчик не
# съедал все ядра общей машины. В GitHub Actions переменная CI выставляется
# автоматически — там используем неограниченный /MP.
set(PMM_LOCAL_BUILD_JOBS "3" CACHE STRING
    "Default parallel job count for local builds (MSVC /MP and Ninja job pools). 0 = unbounded.")

if(NOT PMM_LOCAL_BUILD_JOBS MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "PMM_LOCAL_BUILD_JOBS must be a non-negative integer; "
        "got '${PMM_LOCAL_BUILD_JOBS}'. Use 0 for unbounded.")
endif()

if(DEFINED ENV{CI})
    set(_pmm_in_ci TRUE)
else()
    set(_pmm_in_ci FALSE)
endif()

if(_pmm_in_ci OR PMM_LOCAL_BUILD_JOBS STREQUAL "0")
    set(_pmm_msvc_mp_flag "/MP")
else()
    set(_pmm_msvc_mp_flag "/MP${PMM_LOCAL_BUILD_JOBS}")
    # CMAKE_JOB_POOLS is honoured by the Ninja generator; it caps compile and
    # link parallelism inside the generated build graph so an unparameterized
    # `cmake --build build` (no -j flag) does not saturate every core. Make
    # generators do not honour job pools — for Make/Ninja-Makefiles the cap
    # has to come from `cmake --build -j ${PMM_LOCAL_BUILD_JOBS}` (see the
    # CONTRIBUTING.md "Build parallelism" section and the helper scripts in
    # scripts/).
    set(CMAKE_JOB_POOLS
        "pmm_compile=${PMM_LOCAL_BUILD_JOBS};pmm_link=${PMM_LOCAL_BUILD_JOBS}"
        CACHE STRING "PMM local build parallelism job pools (Ninja generator)" FORCE)
    set(CMAKE_JOB_POOL_COMPILE "pmm_compile" CACHE STRING "" FORCE)
    set(CMAKE_JOB_POOL_LINK    "pmm_link"    CACHE STRING "" FORCE)
    message(STATUS "PMM: local build parallelism capped at ${PMM_LOCAL_BUILD_JOBS} jobs "
                   "(re-run cmake with -DPMM_LOCAL_BUILD_JOBS=0 or set CI=1 in env to remove the cap)")
endif()

# ─── Основная библиотека (header-only) ────────────────────────────────────────
add_library(pmm INTERFACE)
target_include_directories(pmm INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/single_include/pmm
)

# Параметры компилятора
if(MSVC)
    target_compile_options(pmm INTERFACE /W4 /utf-8 ${_pmm_msvc_mp_flag})
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
                target_compile_options(${pmm_catch2_target} PRIVATE ${_pmm_msvc_mp_flag})
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

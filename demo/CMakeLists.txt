cmake_minimum_required(VERSION 3.16)

include(FetchContent)

# ─── GLFW 3.4 ─────────────────────────────────────────────────────────────────
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ─── Dear ImGui (docking branch) ──────────────────────────────────────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# ─── OpenGL ───────────────────────────────────────────────────────────────────
find_package(OpenGL REQUIRED)

# ─── ImGui sources ────────────────────────────────────────────────────────────
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# ─── Demo library (all demo sources except main.cpp) ─────────────────────────
# Shared by pmm_demo executable and the Phase-8 headless test executables.
add_library(pmm_demo_lib STATIC
    demo_app.cpp
    mem_map_view.cpp
    metrics_view.cpp
    struct_tree_view.cpp
    scenario_manager.cpp
    scenarios.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(pmm_demo_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(pmm_demo_lib PUBLIC
    pmm
    glfw
    OpenGL::GL
    Threads::Threads
)

if(APPLE)
    target_compile_definitions(pmm_demo_lib PUBLIC GL_SILENCE_DEPRECATION)
    target_link_libraries(pmm_demo_lib PUBLIC "-framework OpenGL" "-framework Cocoa"
                          "-framework IOKit" "-framework CoreVideo")
endif()

if(MSVC)
    target_compile_options(pmm_demo_lib PRIVATE /W3 /utf-8)
else()
    target_compile_options(pmm_demo_lib PRIVATE -Wall -Wextra)
endif()

# ─── Demo executable ──────────────────────────────────────────────────────────
add_executable(pmm_demo main.cpp)

target_link_libraries(pmm_demo PRIVATE pmm_demo_lib)

if(MSVC)
    target_compile_options(pmm_demo PRIVATE /W3 /utf-8)
else()
    target_compile_options(pmm_demo PRIVATE -Wall -Wextra)
endif()

# ─── Phase 8: Headless demo tests ─────────────────────────────────────────────
# These tests exercise demo logic without opening a window.

function(pmm_demo_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE pmm_demo_lib)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3 /utf-8)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

pmm_demo_test(test_demo_headless         ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_demo_headless.cpp)
pmm_demo_test(test_mem_map_view          ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_mem_map_view.cpp)
pmm_demo_test(test_scenario_manager      ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_scenario_manager.cpp)
pmm_demo_test(test_scenario_coordinator  ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_scenario_coordinator.cpp)

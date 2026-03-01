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

# ─── Demo sources ─────────────────────────────────────────────────────────────
set(DEMO_SOURCES
    main.cpp
    demo_app.cpp
    mem_map_view.cpp
    metrics_view.cpp
    struct_tree_view.cpp
    scenario_manager.cpp
    scenarios.cpp
)

add_executable(pmm_demo
    ${DEMO_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(pmm_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(pmm_demo PRIVATE
    pmm
    glfw
    OpenGL::GL
    Threads::Threads
)

# ─── Platform-specific settings ───────────────────────────────────────────────
if(APPLE)
    target_compile_definitions(pmm_demo PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(pmm_demo PRIVATE "-framework OpenGL" "-framework Cocoa"
                          "-framework IOKit" "-framework CoreVideo")
endif()

if(MSVC)
    target_compile_options(pmm_demo PRIVATE /W3 /utf-8)
else()
    target_compile_options(pmm_demo PRIVATE -Wall -Wextra)
endif()

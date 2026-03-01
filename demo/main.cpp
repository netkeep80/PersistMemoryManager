/**
 * @file main.cpp
 * @brief Entry point for the PersistMemoryManager visual demo application.
 *
 * Initialises GLFW + OpenGL + Dear ImGui (docking), runs the main render loop,
 * and shuts down cleanly.
 */

#include "demo_app.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

// ─── Error callback ───────────────────────────────────────────────────────────

static void glfw_error_callback( int error, const char* description )
{
    std::fprintf( stderr, "GLFW error %d: %s\n", error, description );
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    glfwSetErrorCallback( glfw_error_callback );

    if ( !glfwInit() )
    {
        std::fprintf( stderr, "Failed to initialise GLFW\n" );
        return EXIT_FAILURE;
    }

    // OpenGL 3.3 core profile
    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 3 );
    glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );

#ifdef __APPLE__
    glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE );
#endif

    GLFWwindow* window = glfwCreateWindow( 1280, 800, "PersistMemoryManager Demo", nullptr, nullptr );
    if ( !window )
    {
        std::fprintf( stderr, "Failed to create GLFW window\n" );
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent( window );
    glfwSwapInterval( 1 ); // enable VSync

    // ── ImGui setup ───────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL( window, true );
    ImGui_ImplOpenGL3_Init( "#version 330" );

    // ── Application ───────────────────────────────────────────────────────────
    auto app = std::make_unique<demo::DemoApp>();

    // ── Main loop ─────────────────────────────────────────────────────────────
    while ( !glfwWindowShouldClose( window ) )
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app->render();

        ImGui::Render();

        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize( window, &display_w, &display_h );
        glViewport( 0, 0, display_w, display_h );
        glClearColor( 0.1f, 0.1f, 0.1f, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT );
        ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );

        // Multi-viewport support
        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            GLFWwindow* backup_ctx = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent( backup_ctx );
        }

        glfwSwapBuffers( window );
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    app.reset(); // stops all scenarios, destroys PMM

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow( window );
    glfwTerminate();

    return EXIT_SUCCESS;
}

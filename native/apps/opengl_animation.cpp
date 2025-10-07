#include <iostream>
#include <SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "mesh.h"
#include "loadShader.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

// Animation globals
Mesh* g_spinning_cube = nullptr;
GLuint g_shader_program = 0;
float g_rotation_angle = 0.0f;
Uint32 g_last_frame_time = 0;
bool g_animation_running = true;

bool InitializeOpenGL(SDL_Window* window) {
    // Set OpenGL attributes - use compatible versions for macOS
#ifdef __APPLE__
    // macOS supports OpenGL 4.1, but we'll try 3.3 first for better compatibility
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
    // Other platforms can use OpenGL 4.1
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    
    // Create OpenGL context
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        printf("Failed to create OpenGL context: %s\n", SDL_GetError());
        
#ifdef __APPLE__
        // On macOS, try with OpenGL 2.1 as fallback
        printf("Trying fallback OpenGL 2.1 context...\n");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context) {
            printf("Failed to create fallback OpenGL context: %s\n", SDL_GetError());
            return false;
        }
        printf("Successfully created OpenGL 2.1 context\n");
#else
        return false;
#endif
    }
    
    // Initialize GLAD
    if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) {
        printf("Failed to initialize GLAD\n");
        SDL_GL_DeleteContext(gl_context);
        return false;
    }
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    
    // Create spinning cube
    g_spinning_cube = new Mesh();
    
    // Load shaders - try multiple possible paths
    const char* vertex_paths[] = {
        "vertex.glsl",
        "./vertex.glsl", 
        "../vertex.glsl",
        "build-gui-mingw/vertex.glsl",
        "./build-gui-mingw/vertex.glsl"
    };
    
    const char* fragment_paths[] = {
        "fragment.glsl",
        "./fragment.glsl",
        "../fragment.glsl", 
        "build-gui-mingw/fragment.glsl",
        "./build-gui-mingw/fragment.glsl"
    };
    
    const char* vertex_path = "vertex.glsl";
    const char* fragment_path = "fragment.glsl";
    
    // Find the correct shader paths
    for (int i = 0; i < 5; i++) {
        FILE* test = fopen(vertex_paths[i], "rb");
        if (test) {
            vertex_path = vertex_paths[i];
            fclose(test);
            break;
        }
    }
    
    for (int i = 0; i < 5; i++) {
        FILE* test = fopen(fragment_paths[i], "rb");
        if (test) {
            fragment_path = fragment_paths[i];
            fclose(test);
            break;
        }
    }
    
    printf("Loading shaders: %s and %s\n", vertex_path, fragment_path);
    
    // Check if shader files exist
    FILE* vertex_file = fopen(vertex_path, "r");
    if (!vertex_file) {
        printf("ERROR: Vertex shader file not found: %s\n", vertex_path);
    } else {
        fclose(vertex_file);
        printf("Vertex shader file found: %s\n", vertex_path);
    }
    
    FILE* fragment_file = fopen(fragment_path, "r");
    if (!fragment_file) {
        printf("ERROR: Fragment shader file not found: %s\n", fragment_path);
    } else {
        fclose(fragment_file);
        printf("Fragment shader file found: %s\n", fragment_path);
    }
    
    g_shader_program = LoadShaders(vertex_path, fragment_path);
    if (g_shader_program == 0) {
        printf("Failed to load shaders\n");
        delete g_spinning_cube;
        g_spinning_cube = nullptr;
        SDL_GL_DeleteContext(gl_context);
        return false;
    }
    
    printf("Shaders loaded successfully\n");
    
    g_last_frame_time = SDL_GetTicks();
    printf("OpenGL animation initialized successfully\n");
    return true;
}

void RenderFrame(int screen_width, int screen_height) {
    if (!g_spinning_cube || g_shader_program == 0) return;
    
    // Calculate delta time
    Uint32 current_time = SDL_GetTicks();
    float delta_time = (current_time - g_last_frame_time) / 1000.0f;
    g_last_frame_time = current_time;
    
    // Update rotation
    g_rotation_angle += 50.0f * delta_time; // 50 degrees per second
    if (g_rotation_angle >= 360.0f) g_rotation_angle -= 360.0f;
    
    // Set viewport
    glViewport(0, 0, screen_width, screen_height);
    
    // Clear the screen with a dark background
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    
    // Create transformation matrices
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(g_rotation_angle), glm::vec3(0.5f, 1.0f, 0.0f));
    
    glm::mat4 view = glm::mat4(1.0f);
    view = glm::translate(view, glm::vec3(0.0f, 0.0f, -3.0f));
    
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 
                                           float(screen_width) / float(screen_height), 
                                           0.1f, 100.0f);
    
    // Use shader program
    glUseProgram(g_shader_program);
    
    // Set uniforms
    int modelLoc = glGetUniformLocation(g_shader_program, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    
    int viewLoc = glGetUniformLocation(g_shader_program, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    
    int projectionLoc = glGetUniformLocation(g_shader_program, "projection");
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
    
    // Set wireframe mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    
    // Draw the cube
    g_spinning_cube->draw();
    
    // Reset polygon mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void CleanupOpenGL() {
    if (g_spinning_cube) {
        delete g_spinning_cube;
        g_spinning_cube = nullptr;
    }
    if (g_shader_program != 0) {
        glDeleteProgram(g_shader_program);
        g_shader_program = 0;
    }
}

int main(int argc, char* argv[]) {
    printf("Starting Autobuild OpenGL Animation...\n");
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL failed initialization: %s\n", SDL_GetError());
        return -1;
    }
    
    printf("SDL initialized successfully\n");
    
    // Create window
    int screen_width = 800;
    int screen_height = 600;
    
    printf("Creating window (%dx%d)...\n", screen_width, screen_height);
    SDL_Window* window = SDL_CreateWindow("Autobuild Animation", 
                                         SDL_WINDOWPOS_CENTERED, 
                                         SDL_WINDOWPOS_CENTERED, 
                                         screen_width, 
                                         screen_height, 
                                         SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
    
    if (!window) {
        printf("Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    
    printf("Window created successfully\n");
    
    // Initialize OpenGL
    printf("Initializing OpenGL...\n");
    if (!InitializeOpenGL(window)) {
        printf("Failed to initialize OpenGL animation\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    
    printf("OpenGL initialized successfully\n");
    
    // Animation duration (3 seconds)
    Uint32 animation_start = SDL_GetTicks();
    Uint32 animation_duration = 3000; // 3 seconds
    
    printf("Starting animation loop (duration: %d ms)...\n", animation_duration);
    
    // Main animation loop
    while (g_animation_running) {
        Uint32 current_time = SDL_GetTicks();
        
        // Check if animation time is up
        if (current_time - animation_start >= animation_duration) {
            g_animation_running = false;
            break;
        }
        
        // Handle events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                g_animation_running = false;
                break;
            }
            if (event.type == SDL_KEYDOWN) {
                // Allow user to skip animation with any key
                g_animation_running = false;
                break;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                // Allow user to skip animation by clicking
                g_animation_running = false;
                break;
            }
        }
        
        // Render frame
        RenderFrame(screen_width, screen_height);
        SDL_GL_SwapWindow(window);
        
        // Cap frame rate
        SDL_Delay(16); // ~60 FPS
    }
    
    // Cleanup
    CleanupOpenGL();
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    printf("Animation complete, launching main application...\n");
    
    // Launch the main application
    #ifdef _WIN32
        // Use CreateProcess to launch the main GUI application
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOW; // Show the window for the main GUI
        
        char cmdLine[] = "autobuild_main.exe";
        if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 
                          0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            printf("Main application launched successfully\n");
        } else {
            printf("Failed to launch main application. Error: %lu\n", GetLastError());
        }
    #elif defined(__APPLE__)
        // On macOS, find and launch the main application
        printf("Launching main application on macOS...\n");
        // First try to find the main app in the same directory as this animation
        char exe_path[1024];
        uint32_t size = sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &size) == 0) {
            printf("Animation executable path: %s\n", exe_path);
            // Get directory containing this executable
            char* last_slash = strrchr(exe_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                std::string dir_path = exe_path;
                
                // Try to find autobuild_main in the same directory
                std::string main_app_path = dir_path + "/autobuild_main";
                printf("Looking for main app at: %s\n", main_app_path.c_str());
                if (access(main_app_path.c_str(), X_OK) == 0) {
                    // Found it, launch it
                    printf("Found main app executable, launching...\n");
                    std::string launch_cmd = "open \"" + main_app_path + "\"";
                    printf("Launch command: %s\n", launch_cmd.c_str());
                    system(launch_cmd.c_str());
                } else {
                    // Try as app bundle
                    std::string bundle_path = dir_path + "/autobuild_main.app";
                    printf("Looking for main app bundle at: %s\n", bundle_path.c_str());
                    if (access(bundle_path.c_str(), F_OK) == 0) {
                        printf("Found main app bundle, launching...\n");
                        std::string launch_cmd = "open \"" + bundle_path + "\"";
                        printf("Launch command: %s\n", launch_cmd.c_str());
                        system(launch_cmd.c_str());
                    } else {
                        // Fallback: try system PATH
                        printf("Main app not found locally, trying system PATH...\n");
                        system("open -a autobuild_main >/dev/null 2>&1");
                    }
                }
            }
        } else {
            // Fallback if we can't get executable path
            system("open -a autobuild_main >/dev/null 2>&1");
        }
    #else
        // Linux/Unix: rely on PATH first, then local fallback
        int rc = system("autobuild_main &");
        if (rc != 0) {
            system("./autobuild_main &");
        }
    #endif
    
    return 0;
}

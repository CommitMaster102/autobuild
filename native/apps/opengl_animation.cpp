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

// Animation globals
Mesh* g_spinning_cube = nullptr;
GLuint g_shader_program = 0;
float g_rotation_angle = 0.0f;
Uint32 g_last_frame_time = 0;
bool g_animation_running = true;

bool InitializeOpenGL(SDL_Window* window) {
    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    
    // Create OpenGL context
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        printf("Failed to create OpenGL context: %s\n", SDL_GetError());
        return false;
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
    g_shader_program = LoadShaders(vertex_path, fragment_path);
    if (g_shader_program == 0) {
        printf("Failed to load shaders\n");
        delete g_spinning_cube;
        g_spinning_cube = nullptr;
        SDL_GL_DeleteContext(gl_context);
        return false;
    }
    
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
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL failed initialization: %s\n", SDL_GetError());
        return -1;
    }
    
    // Create window
    int screen_width = 800;
    int screen_height = 600;
    
    SDL_Window* window = SDL_CreateWindow("", 
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
    
    // Initialize OpenGL
    if (!InitializeOpenGL(window)) {
        printf("Failed to initialize OpenGL animation\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    
    // Animation duration (3 seconds)
    Uint32 animation_start = SDL_GetTicks();
    Uint32 animation_duration = 3000; // 3 seconds
    
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
        // On macOS prefer LaunchServices if app bundle, else exec from PATH
        // Try to open by bundle identifier if installed
        int rc = system("open -b com.autobuild.main >/dev/null 2>&1");
        if (rc != 0) {
            // Fallback: try opening by executable name from PATH
            rc = system("open -a autobuild_main >/dev/null 2>&1");
        }
        if (rc != 0) {
            // Last resort: run from current directory/background
            rc = system("./autobuild_main &");
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

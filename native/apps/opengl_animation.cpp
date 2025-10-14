#include <iostream>
#include <SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "mesh.h"
#include "loadShader.h"

#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

// Animation globals
Mesh* g_spinning_mesh = nullptr;
GLuint g_shader_program = 0;
float g_rotation_angle = 0.0f;
Uint32 g_last_frame_time = 0;
bool g_animation_running = true;
ShapeType g_current_shape = ShapeType::CUBE;

// Text overlay globals
GLuint g_text_program = 0;
GLuint g_text_vao = 0;
GLuint g_text_vbo = 0;
GLuint g_text_texture = 0;

// Forward declarations for text overlay
static bool InitializeTextOverlay();
static void DestroyTextOverlay();
static void RenderNeonTextTop(int screen_width, int screen_height, const char* text, float time_seconds, int top_band_px);

// A tiny 5x7 bitmap font for uppercase letters and space, packed into a single-channel texture
// Each glyph cell is 6x8 (including 1px spacing), we'll pack characters we need: " INITIALIZING AUTOBUILD"
// For simplicity we predefine a minimal atlas for A-Z and space.
static const int kGlyphW = 6;
static const int kGlyphH = 8;
static const int kAtlasCols = 16; // 16x2 grid for up to 32 glyphs
static const int kAtlasRows = 2;
static const int kAtlasW = kGlyphW * kAtlasCols;
static const int kAtlasH = kGlyphH * kAtlasRows;

// Compact 5x7 bitmap font (rows top->bottom). Each glyph fits a 6x8 cell (1px padding).
struct GlyphDef { char ch; uint8_t rows[7]; };
static const GlyphDef kGlyphs[] = {
    {' ', {0,0,0,0,0,0,0}},
    // Uppercase used in title
    {'A', {0x1E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'G', {0x0E,0x10,0x10,0x17,0x11,0x11,0x0E}},
    {'I', {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    // Lowercase needed for "Initializing Autobuild"
    {'a', {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}},
    {'b', {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}},
    {'d', {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}},
    {'g', {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}},
    {'i', {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}},
    {'l', {0x04,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'n', {0x00,0x00,0x1C,0x12,0x12,0x12,0x12}},
    {'o', {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}},
    {'t', {0x04,0x04,0x1F,0x04,0x04,0x04,0x03}},
    {'u', {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}},
    {'z', {0x00,0x1F,0x02,0x04,0x08,0x10,0x1F}},
};
static const int kNumGlyphs = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

static int GlyphIndexFor(char c) {
    for (int i = 0; i < kNumGlyphs; ++i) if (kGlyphs[i].ch == c) return i;
    // Fallback to uppercase equivalent if defined
    if (c >= 'a' && c <= 'z') {
        char up = char(c - 'a' + 'A');
        for (int i = 0; i < kNumGlyphs; ++i) if (kGlyphs[i].ch == up) return i;
    }
    return 0; // space
}

static bool BuildTextAtlas(std::vector<uint8_t>& atlas) {
    atlas.assign(kAtlasW * kAtlasH, 0);
    // Place glyphs into atlas cells with 1px right/bottom padding
    for (int gi = 0; gi < kNumGlyphs; ++gi) {
        int col = gi % kAtlasCols;
        int row = gi / kAtlasCols;
        int baseX = col * kGlyphW;
        int baseY = row * kGlyphH;
        for (int y = 0; y < 7; ++y) {
            uint8_t bits = kGlyphs[gi].rows[y];
            for (int x = 0; x < 5; ++x) {
                int on = (bits >> (4 - x)) & 1;
                int px = baseX + x;
                // Write directly (no vertical flip)
                int py = baseY + y;
                if (px >= 0 && px < kAtlasW && py >= 0 && py < kAtlasH) {
                    atlas[py * kAtlasW + px] = on ? 255 : 0;
                }
            }
        }
    }
    return true;
}

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len + 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        printf("Text shader compile error: %s\n", log.data());
    }
    return s;
}

static bool InitializeTextOverlay() {
    // Build atlas
    std::vector<uint8_t> atlas;
    BuildTextAtlas(atlas);

    // Create GL texture (single channel)
    glGenTextures(1, &g_text_texture);
    glBindTexture(GL_TEXTURE_2D, g_text_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    // Use nearest to avoid sampling neighboring glyphs (prevents thin seams)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const char* vs = R"(
        #version 330 core
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec2 aUV;
        out vec2 vUV;
        void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
    )";
    const char* fs = R"(
        #version 330 core
        in vec2 vUV; out vec4 FragColor;
        uniform sampler2D uAtlas;
        uniform vec3 uNeonBase;      // base neon color
        uniform float uTime;
        uniform float uGlow;
        float smoothMask(float a){ return smoothstep(0.3, 0.7, a); }
        void main(){
            float a = texture(uAtlas, vUV).r;
            float core = smoothMask(a);
            float pulse = 0.7 + 0.3 * sin(uTime * 3.0);
            vec3 color = uNeonBase * (uGlow * pulse);
            // Outer glow by soft alpha
            float glow = smoothstep(0.1, 0.3, a) * 0.6;
            vec3 finalColor = color * (core + glow);
            float alpha = (core + glow) * 0.9;
            FragColor = vec4(finalColor, alpha);
        }
    )";

    GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
    g_text_program = glCreateProgram();
    glAttachShader(g_text_program, v);
    glAttachShader(g_text_program, f);
    glLinkProgram(g_text_program);
    glDeleteShader(v);
    glDeleteShader(f);

    // Fullscreen quad VBO/VAO (we will set coords per draw)
    glGenVertexArrays(1, &g_text_vao);
    glGenBuffers(1, &g_text_vbo);
    glBindVertexArray(g_text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 24, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    return g_text_program != 0 && g_text_texture != 0;
}

static void DestroyTextOverlay() {
    if (g_text_vbo) { glDeleteBuffers(1, &g_text_vbo); g_text_vbo = 0; }
    if (g_text_vao) { glDeleteVertexArrays(1, &g_text_vao); g_text_vao = 0; }
    if (g_text_texture) { glDeleteTextures(1, &g_text_texture); g_text_texture = 0; }
    if (g_text_program) { glDeleteProgram(g_text_program); g_text_program = 0; }
}

static void RenderNeonTextTop(int screen_width, int screen_height, const char* text, float time_seconds, int top_band_px) {
    if (!g_text_program || !g_text_texture) return;

    // Prepare text (uppercase only)
    // Use text as provided (mixed case supported); trim to ASCII we have glyphs for
    std::string s(text ? text : "");
    s.erase(std::remove_if(s.begin(), s.end(), [](char c){ return (unsigned char)c < 32 || (unsigned char)c > 122; }), s.end());

    // Scaling based on top band height
    int band_h = top_band_px;
    int scale = std::max(1, band_h / (kGlyphH + 4));
    // Compute total width in cells (include 1px kerning between glyphs)
    int total_cells_w = (int)s.size() * kGlyphW + std::max(0, (int)s.size()-1);
    // Constrain scale to fit horizontally with a small margin
    int margin_px = std::max(8, screen_width / 40);
    int max_scale_from_width = std::max(1, (screen_width - 2 * margin_px) / std::max(1, total_cells_w));
    scale = std::min(scale, max_scale_from_width);
    int draw_h = kGlyphH * scale;
    int draw_w = total_cells_w * scale;

    // Position in screen pixels
    int x = (screen_width - draw_w) / 2;
    int y = screen_height - band_h + (band_h - draw_h) / 2;

    // Build vertices per character
    std::vector<float> verts;
    verts.reserve(s.size() * 24);
    int pen_x = x;
    for (char c : s) {
        int gi = GlyphIndexFor(c);
        int col = gi % kAtlasCols;
        int row = gi / kAtlasCols;
        // Inset UVs by half a texel to avoid bleeding from neighbors
        float halfTexelU = 0.5f / float(kAtlasW);
        float halfTexelV = 0.5f / float(kAtlasH);
        float u0 = (col * kGlyphW) / float(kAtlasW) + halfTexelU;
        float u1 = ((col + 1) * kGlyphW) / float(kAtlasW) - halfTexelU;
        // Atlas rows are written bottom-up; flip within the row block so text is upright
        float v0 = ((row + 1) * kGlyphH) / float(kAtlasH) - halfTexelV;
        float v1 = (row * kGlyphH) / float(kAtlasH) + halfTexelV;

        int x0 = pen_x;
        int y0 = y;
        int x1 = pen_x + kGlyphW * scale;
        int y1 = y + kGlyphH * scale;

        float sx0 = -1.0f + 2.0f * (float)x0 / (float)screen_width;
        float sy0 = -1.0f + 2.0f * (float)y0 / (float)screen_height;
        float sx1 = -1.0f + 2.0f * (float)x1 / (float)screen_width;
        float sy1 = -1.0f + 2.0f * (float)y1 / (float)screen_height;

        // Two triangles per glyph
        float quad[24] = {
            sx0, sy0, u0, v0,
            sx1, sy0, u1, v0,
            sx1, sy1, u1, v1,
            sx0, sy0, u0, v0,
            sx1, sy1, u1, v1,
            sx0, sy1, u0, v1,
        };
        verts.insert(verts.end(), quad, quad + 24);
        pen_x += (kGlyphW * scale) + (1 * scale); // advance with kerning
    }

    // Render with blending, no depth
    GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(g_text_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_text_texture);
    GLint loc = glGetUniformLocation(g_text_program, "uAtlas");
    glUniform1i(loc, 0);
    glUniform3f(glGetUniformLocation(g_text_program, "uNeonBase"), 0.0f, 0.9f, 1.0f);
    glUniform1f(glGetUniformLocation(g_text_program, "uTime"), time_seconds);
    glUniform1f(glGetUniformLocation(g_text_program, "uGlow"), 1.0f);

    glBindVertexArray(g_text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_text_vbo);
    // Resize buffer if needed
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLint)(verts.size() / 4));
    glBindVertexArray(0);

    if (depth_enabled) glEnable(GL_DEPTH_TEST);
    if (!blend_enabled) glDisable(GL_BLEND);
}

bool InitializeOpenGL(SDL_Window* window) {
    // Set OpenGL attributes - use compatible versions for macOS
#ifdef __APPLE__
    // macOS supports OpenGL 4.1, but we'll try 3.3 first for better compatibility
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_FORWARD_COMPAT_FLAG, 1); // Required for macOS core profile
#else
    // Other platforms can use OpenGL 4.1
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    
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
        SDL_GL_SetAttribute(SDL_GL_FORWARD_COMPAT_FLAG, 0); // Not needed for compatibility profile
        
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
    
    // Print OpenGL information for debugging
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    printf("OpenGL Vendor: %s\n", glGetString(GL_VENDOR));
    printf("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));
    printf("OpenGL Shading Language Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    
    // Enable multisampling if available
    GLint samples;
    glGetIntegerv(GL_SAMPLES, &samples);
    if (samples > 0) {
        glEnable(GL_MULTISAMPLE);
        printf("Multisampling enabled with %d samples\n", samples);
    }
    
    // Create random spinning shape
    g_current_shape = Mesh::getRandomShape();
    g_spinning_mesh = new Mesh(g_current_shape);
    
    // Print shape information
    const char* shape_names[] = {
        "Cube", "Tetrahedron", "Octahedron", "Icosahedron", 
        "Torus", "Sphere", "Pyramid", "Diamond"
    };
    printf("Selected random shape: %s (will rotate for 5 seconds)\n", shape_names[static_cast<int>(g_current_shape)]);
    
    // Load shaders - try multiple possible paths
    const char* vertex_paths[] = {
        "vertex.glsl",
        "./vertex.glsl", 
        "../vertex.glsl",
        "build-gui-mingw/vertex.glsl",
        "./build-gui-mingw/vertex.glsl",
#ifdef __APPLE__
        // macOS app bundle paths
        "../Resources/vertex.glsl",
        "./Resources/vertex.glsl",
        "../../Resources/vertex.glsl",
        "../../../Resources/vertex.glsl"
#endif
    };
    
    const char* fragment_paths[] = {
        "fragment.glsl",
        "./fragment.glsl",
        "../fragment.glsl", 
        "build-gui-mingw/fragment.glsl",
        "./build-gui-mingw/fragment.glsl",
#ifdef __APPLE__
        // macOS app bundle paths
        "../Resources/fragment.glsl",
        "./Resources/fragment.glsl",
        "../../Resources/fragment.glsl",
        "../../../Resources/fragment.glsl"
#endif
    };
    
    const char* vertex_path = "vertex.glsl";
    const char* fragment_path = "fragment.glsl";
    
    // Find the correct shader paths
#ifdef __APPLE__
    int path_count = 9; // 5 original + 4 macOS bundle paths
#else
    int path_count = 5; // 5 original paths
#endif
    
    for (int i = 0; i < path_count; i++) {
        FILE* test = fopen(vertex_paths[i], "rb");
        if (test) {
            vertex_path = vertex_paths[i];
            fclose(test);
            break;
        }
    }
    
    for (int i = 0; i < path_count; i++) {
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
        printf("Failed to load shaders from %s and %s\n", vertex_path, fragment_path);
        
#ifdef __APPLE__
        // On macOS, try to provide more helpful error information
        printf("macOS Debug Info:\n");
        printf("  Current working directory: ");
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            printf("unknown\n");
        }
        
        printf("  Executable path: ");
        char exe_path[1024];
        uint32_t size = sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &size) == 0) {
            printf("%s\n", exe_path);
        } else {
            printf("unknown\n");
        }
        
        printf("  Bundle Resources path: ");
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            printf("%s/Contents/Resources/\n", exe_path);
        } else {
            printf("unknown\n");
        }
#endif
        
        delete g_spinning_mesh;
        g_spinning_mesh = nullptr;
        SDL_GL_DeleteContext(gl_context);
        return false;
    }
    
    printf("Shaders loaded successfully from %s and %s\n", vertex_path, fragment_path);
    
    // Initialize text overlay (neon top band)
    if (!InitializeTextOverlay()) {
        printf("Failed to initialize text overlay\n");
        delete g_spinning_mesh;
        g_spinning_mesh = nullptr;
        SDL_GL_DeleteContext(gl_context);
        return false;
    }

    g_last_frame_time = SDL_GetTicks();
    printf("OpenGL animation initialized successfully\n");
    return true;
}

void RenderFrame(int screen_width, int screen_height) {
    if (!g_spinning_mesh || g_shader_program == 0) return;
    
    // Calculate delta time
    Uint32 current_time = SDL_GetTicks();
    float delta_time = (current_time - g_last_frame_time) / 1000.0f;
    g_last_frame_time = current_time;
    
    // Update rotation
    g_rotation_angle += 50.0f * delta_time; // 50 degrees per second
    if (g_rotation_angle >= 360.0f) g_rotation_angle -= 360.0f;
    
    // Reserve a top band for neon text (e.g., 12% of height), render 3D below it
    const int top_band_px = std::max(36, int(screen_height * 0.12f));
    int viewport_height = screen_height - top_band_px;
    if (viewport_height < 100) viewport_height = screen_height; // safety fallback

    // Set viewport for 3D content (exclude top band)
    glViewport(0, 0, screen_width, viewport_height);
    glEnable(GL_DEPTH_TEST);
    
    // Clear the screen with a dark background
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    
    // Create transformation matrices
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(g_rotation_angle), glm::vec3(0.5f, 1.0f, 0.0f));
    
    glm::mat4 view = glm::mat4(1.0f);
    view = glm::translate(view, glm::vec3(0.0f, 0.0f, -3.0f));
    
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 
                                           float(screen_width) / float(viewport_height), 
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
    
    // Draw the mesh
    g_spinning_mesh->draw();
    
    // Reset polygon mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Render neon text overlay in top band after 3D
    // Use full-screen viewport for 2D overlay so NDC mapping is correct
    glViewport(0, 0, screen_width, screen_height);
    float time_seconds = SDL_GetTicks() / 1000.0f;
    RenderNeonTextTop(screen_width, screen_height, "Initializing Autobuild", time_seconds, top_band_px);
}

void CleanupOpenGL() {
    if (g_spinning_mesh) {
        delete g_spinning_mesh;
        g_spinning_mesh = nullptr;
    }
    if (g_shader_program != 0) {
        glDeleteProgram(g_shader_program);
        g_shader_program = 0;
    }
    DestroyTextOverlay();
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
    
    // Animation duration (5 seconds)
    Uint32 animation_start = SDL_GetTicks();
    Uint32 animation_duration = 5000; // 5 seconds
    
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

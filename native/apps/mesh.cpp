#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include "mesh.h"
#include <SDL.h>
#include <iostream>

// Define M_PI if not available (MinGW compatibility)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Use a more portable approach for pi
constexpr double PI = 3.14159265358979323846;

// Random number generator for shape selection
static std::random_device rd;
static std::mt19937 gen(rd());

Mesh::Mesh() : currentShape(ShapeType::CUBE) {
    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);
    createCube();
}

Mesh::Mesh(ShapeType shape) : currentShape(shape) {
    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);
    setShape(shape);
}

Mesh::~Mesh() {
    cleanup();
}

void Mesh::cleanup() {
    if (vertexbuffer) {
        glDeleteBuffers(1, &vertexbuffer);
        vertexbuffer = 0;
    }
    if (elementbuffer) {
        glDeleteBuffers(1, &elementbuffer);
        elementbuffer = 0;
    }
    if (VertexArrayID) {
        glDeleteVertexArrays(1, &VertexArrayID);
        VertexArrayID = 0;
    }
}

void Mesh::setShape(ShapeType shape) {
    currentShape = shape;
    cleanup();
    
    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);
    
    switch (shape) {
        case ShapeType::CUBE:
            createCube();
            break;
        case ShapeType::TETRAHEDRON:
            createTetrahedron();
            break;
        case ShapeType::OCTAHEDRON:
            createOctahedron();
            break;
        case ShapeType::ICOSAHEDRON:
            createIcosahedron();
            break;
        case ShapeType::TORUS:
            createTorus();
            break;
        case ShapeType::SPHERE:
            createSphere();
            break;
        case ShapeType::PYRAMID:
            createPyramid();
            break;
        case ShapeType::DIAMOND:
            createDiamond();
            break;
    }
}

ShapeType Mesh::getRandomShape() {
    static std::uniform_int_distribution<> dis(0, 7);
    return static_cast<ShapeType>(dis(gen));
}

void Mesh::setupBuffers(const std::vector<GLfloat>& vertices, const std::vector<GLuint>& indices) {
    glGenBuffers(1, &vertexbuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &elementbuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    vertex_size = indices.size();
}

void Mesh::createCube() {
    std::vector<GLfloat> vertices = {
        // Front face
        0.5f, -0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f,
        0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, 0.5f,
        // Back face
        0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,
        0.5f, 0.5f, -0.5f,
        -0.5f, 0.5f, -0.5f
    };

    std::vector<GLuint> indices = {
        0, 2, 3, 0, 3, 1,  // Front
        2, 6, 7, 2, 7, 3,  // Top
        6, 4, 5, 6, 5, 7,  // Back
        4, 0, 1, 4, 1, 5,  // Bottom
        1, 3, 7, 1, 7, 5,  // Left
        4, 6, 2, 4, 2, 0   // Right
    };

    setupBuffers(vertices, indices);
}

void Mesh::createTetrahedron() {
    std::vector<GLfloat> vertices = {
        0.0f, 0.5f, 0.0f,      // Top
        0.5f, -0.5f, 0.5f,     // Front right
        -0.5f, -0.5f, 0.5f,    // Front left
        0.0f, -0.5f, -0.5f     // Back
    };

    std::vector<GLuint> indices = {
        0, 1, 2,  // Front face
        0, 2, 3,  // Left face
        0, 3, 1,  // Right face
        1, 3, 2   // Bottom face
    };

    setupBuffers(vertices, indices);
}

void Mesh::createOctahedron() {
    std::vector<GLfloat> vertices = {
        0.0f, 0.5f, 0.0f,      // Top
        0.5f, 0.0f, 0.0f,      // Right
        0.0f, 0.0f, 0.5f,      // Front
        -0.5f, 0.0f, 0.0f,     // Left
        0.0f, 0.0f, -0.5f,     // Back
        0.0f, -0.5f, 0.0f      // Bottom
    };

    std::vector<GLuint> indices = {
        0, 1, 2,  // Top front right
        0, 2, 3,  // Top front left
        0, 3, 4,  // Top back left
        0, 4, 1,  // Top back right
        5, 2, 1,  // Bottom front right
        5, 3, 2,  // Bottom front left
        5, 4, 3,  // Bottom back left
        5, 1, 4   // Bottom back right
    };

    setupBuffers(vertices, indices);
}

void Mesh::createIcosahedron() {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f; // Golden ratio
    
    std::vector<GLfloat> vertices = {
        -1.0f,  t, 0.0f,   1.0f,  t, 0.0f,   -1.0f, -t, 0.0f,   1.0f, -t, 0.0f,
         0.0f, -1.0f,  t,   0.0f,  1.0f,  t,    0.0f, -1.0f, -t,   0.0f,  1.0f, -t,
          t, 0.0f, -1.0f,    t, 0.0f,  1.0f,   -t, 0.0f, -1.0f,   -t, 0.0f,  1.0f
    };

    // Normalize vertices
    for (size_t i = 0; i < vertices.size(); i += 3) {
        float length = std::sqrt(vertices[i]*vertices[i] + vertices[i+1]*vertices[i+1] + vertices[i+2]*vertices[i+2]);
        vertices[i] /= length;
        vertices[i+1] /= length;
        vertices[i+2] /= length;
    }

    std::vector<GLuint> indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };

    setupBuffers(vertices, indices);
}

void Mesh::createTorus(int segments, int rings, float outerRadius, float innerRadius) {
    std::vector<GLfloat> vertices;
    std::vector<GLuint> indices;

    for (int i = 0; i <= rings; i++) {
        float v = (float)i / rings * 2.0f * PI;
        float cosV = cos(v);
        float sinV = sin(v);

        for (int j = 0; j <= segments; j++) {
            float u = (float)j / segments * 2.0f * PI;
            float cosU = cos(u);
            float sinU = sin(u);

            float x = (outerRadius + innerRadius * cosU) * cosV;
            float y = (outerRadius + innerRadius * cosU) * sinV;
            float z = innerRadius * sinU;

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }

    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segments; j++) {
            int first = i * (segments + 1) + j;
            int second = first + segments + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    setupBuffers(vertices, indices);
}

void Mesh::createSphere(int segments, int rings) {
    std::vector<GLfloat> vertices;
    std::vector<GLuint> indices;

    for (int i = 0; i <= rings; i++) {
        float v = (float)i / rings * PI;
        float cosV = cos(v);
        float sinV = sin(v);

        for (int j = 0; j <= segments; j++) {
            float u = (float)j / segments * 2.0f * PI;
            float cosU = cos(u);
            float sinU = sin(u);

            vertices.push_back(cosU * sinV);
            vertices.push_back(cosV);
            vertices.push_back(sinU * sinV);
        }
    }

    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segments; j++) {
            int first = i * (segments + 1) + j;
            int second = first + segments + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    setupBuffers(vertices, indices);
}

void Mesh::createPyramid() {
    std::vector<GLfloat> vertices = {
        0.0f, 0.5f, 0.0f,      // Top
        0.5f, -0.5f, 0.5f,     // Front right
        -0.5f, -0.5f, 0.5f,    // Front left
        0.5f, -0.5f, -0.5f,    // Back right
        -0.5f, -0.5f, -0.5f    // Back left
    };

    std::vector<GLuint> indices = {
        0, 1, 2,  // Front face
        0, 2, 4,  // Left face
        0, 4, 3,  // Back face
        0, 3, 1,  // Right face
        1, 3, 4, 1, 4, 2  // Bottom face (two triangles)
    };

    setupBuffers(vertices, indices);
}

void Mesh::createDiamond() {
    std::vector<GLfloat> vertices = {
        0.0f, 0.5f, 0.0f,      // Top point
        0.3f, 0.2f, 0.0f,      // Upper right
        0.0f, 0.2f, 0.3f,      // Upper front
        -0.3f, 0.2f, 0.0f,     // Upper left
        0.0f, 0.2f, -0.3f,     // Upper back
        0.3f, -0.2f, 0.0f,     // Lower right
        0.0f, -0.2f, 0.3f,     // Lower front
        -0.3f, -0.2f, 0.0f,    // Lower left
        0.0f, -0.2f, -0.3f,    // Lower back
        0.0f, -0.5f, 0.0f      // Bottom point
    };

    std::vector<GLuint> indices = {
        // Top pyramid
        0, 1, 2,  0, 2, 3,  0, 3, 4,  0, 4, 1,
        // Middle band
        1, 5, 6,  1, 6, 2,  2, 6, 7,  2, 7, 3,  3, 7, 8,  3, 8, 4,  4, 8, 5,  4, 5, 1,
        // Bottom pyramid
        9, 6, 5,  9, 7, 6,  9, 8, 7,  9, 5, 8
    };

    setupBuffers(vertices, indices);
}

void Mesh::draw() {
    // Ensure our VAO is bound (core profile requires a VAO for vertex attrib state)
    glBindVertexArray(VertexArrayID);

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);
    glDrawElements(GL_TRIANGLES, vertex_size, GL_UNSIGNED_INT, 0);

    glDisableVertexAttribArray(0);
}
#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>
#include <vector>

enum class ShapeType {
    CUBE,
    TETRAHEDRON,
    OCTAHEDRON,
    ICOSAHEDRON,
    TORUS,
    SPHERE,
    PYRAMID,
    DIAMOND
};

class Mesh {
	public:
		Mesh();
		Mesh(ShapeType shape);
		~Mesh();
		void draw();
		void setShape(ShapeType shape);
		ShapeType getCurrentShape() const { return currentShape; }
		
		// Static method to get random shape
		static ShapeType getRandomShape();
		
	private:
		void createCube();
		void createTetrahedron();
		void createOctahedron();
		void createIcosahedron();
		void createTorus(int segments = 16, int rings = 8, float outerRadius = 0.5f, float innerRadius = 0.3f);
		void createSphere(int segments = 16, int rings = 8);
		void createPyramid();
		void createDiamond();
		
		void cleanup();
		void setupBuffers(const std::vector<GLfloat>& vertices, const std::vector<GLuint>& indices);
		
		GLuint VertexArrayID, vertexbuffer, elementbuffer;
		GLuint vertex_size;
		ShapeType currentShape;
};

#endif

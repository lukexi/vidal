#ifndef QUAD_H
#define QUAD_H

#include <GL/glew.h>

void InitGLEW();
void GLCheck(const char* name);
GLuint CreateQuad(const float* QuadVertices);

#endif // QUAD_H

#ifndef SHADER_H
#define SHADER_H

#include <GL/glew.h>

char* ReadFile(const char *file);

GLuint CreateShader(GLenum ShaderType, const char* ShaderSource);

int LinkProgram(GLuint ShaderProgram);

GLuint CreateVertFragProgram(const char* VertSource, const char *FragSource);
GLuint CreateVertFragProgramFromPath(const char* VertPath, const char* FragPath);

#endif /* SHADER_H */

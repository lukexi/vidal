#ifndef TEXTURE_H
#define TEXTURE_H

#include <GL/glew.h>

int CreateTexture(int width, int height, int channels);

void UpdateTexture(GLuint Tex, int Width, int Height, GLenum Format, const void* Data);

#endif // TEXTURE_H

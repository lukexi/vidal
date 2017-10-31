#include <SDL.h>
#include <GL/glew.h>
#include "shader.h"

static float FullscreenQuadVertices[8] = {
    -1, -1, // Left Top
    -1, 1,  // Left Bottom
    1, -1,  // Right Top
    1, 1    // Right Bottom
};

void GLCheck(const char* name) {
    GLenum Error = glGetError();
    if (Error != GL_NO_ERROR) {
        printf("%s: ", name);
        switch (Error) {
            case GL_INVALID_ENUM: printf("Invalid enum\n");
                break;
            case GL_INVALID_VALUE: printf("Invalid value\n");
                break;
            case GL_INVALID_OPERATION: printf("Invalid operation\n");
                break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: printf("Invalid framebuffer op\n");
                break;
            case GL_OUT_OF_MEMORY: printf("GL Out of memory\n");
                break;
        }
        exit(1);
    }
}

GLuint CreateQuad(const float* QuadVertices) {

    GLuint VAO;
    glGenVertexArrays(1,&VAO);
    glBindVertexArray(VAO);


    /*

    1__3
    | /|
    |/_|
    2  4

    */
    const int NumVertComponents = 2;

    GLuint VertBuffer;
    glGenBuffers(1, &VertBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, VertBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QuadVertices)*8, QuadVertices, GL_STATIC_DRAW);

    const GLuint PositionAttrIndex = 0; // layout(location = 0) in vert shader
    glVertexAttribPointer(
        PositionAttrIndex,  // Attribute Index
        NumVertComponents,  // Attribute Size
        GL_FLOAT,           // Attribute Type
        GL_FALSE,           // Normalize values?
        0,                  // Stride
        0                   // Pointer to first item
        );
    glEnableVertexAttribArray(PositionAttrIndex);


    // UVs
    static float QuadUVs[8] = {
        0, 1, // Left Top
        0, 0, // Left Bottom
        1, 1, // Right Top
        1, 0  // Right Bottom
    };

    const int NumUVComponents = 2;

    GLuint UVsBuffer;
    glGenBuffers(1, &UVsBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, UVsBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QuadUVs), QuadUVs, GL_STATIC_DRAW);

    const GLuint UVsAttrIndex = 1; // layout(location = 1) in vert shader
    glVertexAttribPointer(
        UVsAttrIndex,       // Attribute Index
        NumUVComponents,    // Attribute Size
        GL_FLOAT,           // Attribute Type
        GL_FALSE,           // Normalize values?
        0,                  // Stride
        0                   // Pointer to first item
        );
    glEnableVertexAttribArray(UVsAttrIndex);

    return VAO;
}

static void InitGLEW() {
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if(glewInit() != GLEW_OK) {
        printf("Could not init glew.\n");
        exit(1);
    }
    GLenum GLEWError = glGetError();
    if (GLEWError) {
        printf("GLEW returned error: %i\n", GLEWError);
    }
}

#define NUM_QUADS 20

int main(int argc, char const *argv[]) {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* Window = SDL_CreateWindow("Veil", 10,10, 1024,1024, SDL_WINDOW_OPENGL);
    SDL_GLContext GLContext = SDL_GL_CreateContext(Window);
    SDL_GL_MakeCurrent(Window, GLContext);
    InitGLEW();

    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");
    glUseProgram(QuadProgram);

    GLuint Quads[NUM_QUADS];
    const float BoxSize = 1.0/NUM_QUADS;
    for (int QuadIndex = 0; QuadIndex < NUM_QUADS; QuadIndex++) {

        const float X0 = BoxSize * (QuadIndex + 0) * 2 - 1;
        const float X1 = BoxSize * (QuadIndex + 1) * 2 - 1;
        const float Y0 = 0 - BoxSize;
        const float Y1 = 0 + BoxSize;

        const float Vertices[8] = {
            X0, Y0,  // Left Top
            X0, Y1,  // Left Bottom
            X1, Y0,  // Right Top
            X1, Y1   // Right Bottom
        };
        Quads[QuadIndex] = CreateQuad(Vertices);
    }

    GLint VarLoc = glGetUniformLocation(QuadProgram, "VAR");
    GLint TimeLoc = glGetUniformLocation(QuadProgram, "uTime");

    while (1) {
        SDL_Event Event;
        while (SDL_PollEvent(&Event)) {
            if (Event.type == SDL_QUIT) exit(0);
        }

        glClearColor(0, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1f(TimeLoc, SDL_GetTicks());

        for (int QuadIndex = 0; QuadIndex < NUM_QUADS; QuadIndex++) {
            glUniform1f(VarLoc, (float)QuadIndex/(float)NUM_QUADS);
            glBindVertexArray(Quads[QuadIndex]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        SDL_GL_SwapWindow(Window);
    }

    return 0;
}

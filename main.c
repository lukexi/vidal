#include <SDL.h>
#include <GL/glew.h>
#include <stdbool.h>
#include "shader.h"
#include "quad.h"
// #include "texture.h"
// #include "pa_ringbuffer.h"
#include "video-audio.h"
#include "video.h"
// #include <portaudio.h>
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"


void DrawVideo(video* Video, GLuint QuadProgram, GLuint Quad)
{
    glUniform1i(glGetUniformLocation(QuadProgram, "uTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Video->Texture);

    glBindVertexArray(Quad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}



int main(int argc, char const *argv[])
{
    av_register_all();

    audio_state* AudioState = StartAudio();

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

    NVGcontext* NVG = nvgCreateGL3(0);

    // video* Video = OpenVideo("pinball.mov");
    // video* Video = OpenVideo("mario.mp4");
    // video* Video = OpenVideo("Martin_Luther_King_PBS_interview_with_Kenneth_B._Clark_1963.mp4");
    video* Video = OpenVideo("MartinLutherKing.mp4", NVG);

    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");
    glUseProgram(QuadProgram);

    float Verts[8] = {
        -1, -1, // Left Top
        -1, 1,  // Left Bottom
        1, -1,  // Right Top
        1, 1    // Right Bottom
    };
    GLuint Quad = CreateQuad(Verts);

    while (1) {
        SDL_Event Event;
        while (SDL_PollEvent(&Event)) {
            if (Event.type == SDL_QUIT) exit(0);
        }

        glClearColor(0, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        TickVideo(Video, AudioState);
        DrawVideo(Video, Quad, QuadProgram);

        SDL_GL_SwapWindow(Window);
    }

    FreeVideo(Video, NVG);



    return 0;
}

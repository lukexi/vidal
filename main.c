#include <SDL.h>
#include <GL/glew.h>
#include <stdbool.h>
#include "shader.h"
#include "quad.h"
#include "utils.h"
#include "video-audio.h"
#include "video.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"


typedef struct {
    video* Video;
    GLuint Quad;
} video_quad;

void DrawVideo(video_quad* VideoQuad, GLuint QuadProgram)
{
    if (!VideoQuad || !VideoQuad->Video) return;

    glUniform1i(glGetUniformLocation(QuadProgram, "uTex"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, VideoQuad->Video->Texture);

    glBindVertexArray(VideoQuad->Quad);
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
    SDL_GL_SetSwapInterval(0);
    InitGLEW();

    GLuint QuadProgram = CreateVertFragProgramFromPath(
        "quad.vert",
        "quad.frag");
    glUseProgram(QuadProgram);

    NVGcontext* NVG = nvgCreateGL3(0);


    const char* VideoNames[] = {
        "videos/Martin_Luther_King_PBS_interview_with_Kenneth_B._Clark_1963.mp4",
        // "videos/tmnt.mp4",
        // "videos/timeline-students.mov",
        // "videos/best_token_table_clip_sm2.mp4",
        // "videos/Luge 8-6-2016.m4a"
    };

    const size_t NumVideos = ARRAY_LEN(VideoNames);
    const float BoxSize = 1.0/NumVideos;
    video_quad* VideoQuads = calloc(NumVideos, sizeof(video_quad));
    for (int QuadIndex = 0; QuadIndex < NumVideos; QuadIndex++)
    {
        video_quad* VideoQuad = &VideoQuads[QuadIndex];
        const char* VideoName = VideoNames[QuadIndex];

        VideoQuad->Video = OpenVideo(VideoName, NVG, AudioState);

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
        VideoQuad->Quad = CreateQuad(Vertices);
    }

    while (1) {

        SDL_Event Event;
        while (SDL_PollEvent(&Event)) {
            if (Event.type == SDL_QUIT) exit(0);
        }


        glClearColor(0, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        for (int QuadIndex = 0; QuadIndex < NumVideos; QuadIndex++) {
            video_quad* VideoQuad = &VideoQuads[QuadIndex];
            TickVideo(VideoQuad->Video);
            DrawVideo(VideoQuad, QuadProgram);
        }

        SDL_GL_SwapWindow(Window);
    }

    for (int QuadIndex = 0; QuadIndex < NumVideos; QuadIndex++) {
        video_quad* VideoQuad = &VideoQuads[QuadIndex];
        FreeVideo(VideoQuad->Video);
    }

    return 0;
}

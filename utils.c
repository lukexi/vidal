#include "utils.h"

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>


void Fatal(const char *format, ...)
{
    va_list ap;

    fprintf(stderr, "ERROR: ");

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    exit(1);
}

void Graph(char* sym, int N) {
    for (int i = 0; i < N; ++i) {
        printf("%s", sym);
    }
    printf("\n");
}

uint64_t GetTimeInMicros () {
    struct timeval Now;
    gettimeofday(&Now, NULL);
    return Now.tv_sec * (uint64_t)1000000 + Now.tv_usec;
}

fps MakeFPS() {
    struct timeval Now;
    gettimeofday(&Now, NULL);
    return (fps){
        .CurrentSecond = (int)Now.tv_sec,
        .FramesThisSecond = 0,
        .FramesLastSecond = 0
    };
}

static void SyncFPS(fps* FPS) {
    struct timeval Now;
    gettimeofday(&Now, NULL);
    int NowSecond = Now.tv_sec;
    if (NowSecond > FPS->CurrentSecond) {
        FPS->FramesLastSecond = (NowSecond == FPS->CurrentSecond + 1) ? FPS->FramesThisSecond : 0;
        FPS->FramesThisSecond = 0;
        FPS->CurrentSecond = NowSecond;
    }
}

void TickFPS(fps* FPS) {
    SyncFPS(FPS);
    FPS->FramesThisSecond++;
}

int GetFPS(fps* FPS) {
    SyncFPS(FPS);
    return FPS->FramesLastSecond;
}

void GLCheck(const char* name) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("%s: ", name);
        switch (err) {
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

void GLAPIENTRY DebugCallback(GLenum Source, GLenum Type,
    GLuint ID, GLenum Severity, GLsizei Length,
    const GLchar* Message, const void* UserParam) {
    printf("===OPENGL DEBUG 0x%X: %s\n", ID, Message);
}

void EnableGLDebug() {
    glDebugMessageCallback(DebugCallback, NULL);
    glDebugMessageControl(GL_DONT_CARE,
        GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glEnable(GL_DEBUG_OUTPUT);
}

int NextPowerOfTwo(int x) {
    x--;
    x |= x >> 1; // handle 2 bit numbers
    x |= x >> 2; // handle 4 bit numbers
    x |= x >> 4; // handle 8 bit numbers
    x |= x >> 8; // handle 16 bit numbers
    x |= x >> 16; // handle 32 bit numbers
    x++;
    return x;
}

void WaitSync(GLsync Sync) {
    if (!Sync) {
        return;
    }
    while (1) {
        GLenum WaitResult = glClientWaitSync(Sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1);
        if (WaitResult == GL_ALREADY_SIGNALED ||
            WaitResult == GL_CONDITION_SATISFIED) {
            return;
        }
        printf("***WAITING ON A BUFFERED TEXTURE SYNC OBJECT (tell Luke if you see this)\n");
    }
}

void LockSync(GLsync* Sync) {
    glDeleteSync(*Sync);
    *Sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

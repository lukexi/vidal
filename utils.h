
#if !defined(UTILS_H)
#define UTILS_H
#include <GL/glew.h>

#define ARRAY_LEN(_arr) ((int)sizeof(_arr) / (int)sizeof(*_arr))
#define UNUSED(x) (void)(x)
#define RANDFLOAT ((float)rand()/(float)(RAND_MAX))
#define RANDRANGE(low,hi) (RANDFLOAT*(hi-low) + low)

#define MAX(a,b) (a > b ? a : b)
#define MIN(a,b) (a < b ? a : b)
#define CLAMP(l,h,a) (MAX(l, MIN(h, a)))
#define MOD(a, b) (((a) % (b) + (b)) % (b))

void Fatal(const char *format, ...);

void GLCheck(const char* name);

void WaitSync(GLsync Sync);
void LockSync(GLsync* Sync);

// Turns on basic OpenGL 4.3 Debugging output.
// Lots more fancy options available,
// like synchronous output, debug groups,
// object naming, see here page 65:
// https://www.slideshare.net/Mark_Kilgard/opengl-45-update-for-nvidia-gpus
void EnableGLDebug();

int NextPowerOfTwo(int x);

void Graph(char* sym, int N);

uint64_t GetTimeInMicros();

typedef struct {
    int CurrentSecond;
    int FramesThisSecond;
    int FramesLastSecond;
} fps;

fps MakeFPS();
void TickFPS(fps* FPS);
int GetFPS(fps* FPS);

#endif /* UTILS_H */

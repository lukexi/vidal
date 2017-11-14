all: vidal

FLAGS=`pkg-config --libs --cflags SDL2 GLEW libavcodec libavformat libswscale`
FLAGS+=-framework OpenGL
FLAGS+=-lportaudio

SOURCES+=main.c
SOURCES+=shader.c
SOURCES+=quad.c
SOURCES+=texture.c
SOURCES+=pa_ringbuffer.c
SOURCES+=video-audio.c

vidal: $(SOURCES)
	clang -o $@.app $^ $(FLAGS) -g -Wall

all: vidal

FLAGS=`pkg-config --libs --cflags SDL2 GLEW jack libavcodec libavformat libswscale`
FLAGS+=-framework OpenGL

SOURCES+=main.c
SOURCES+=shader.c
SOURCES+=quad.c
SOURCES+=texture.c
SOURCES+=pa_ringbuffer.c
SOURCES+=video-audio.c
SOURCES+=utils.c
SOURCES+=video.c
SOURCES+=nanovg.c

vidal: $(SOURCES)
	clang -o $@.app $^ $(FLAGS) -g -Wall

all: vidal

FLAGS=`pkg-config --libs --cflags SDL2 GLEW libavcodec libavformat libswscale`
FLAGS+=-framework OpenGL
FLAGS+=-lportaudio

vidal: main.c shader.c quad.c texture.c pa_ringbuffer.c
	clang -o $@.app $^ $(FLAGS) -g -Wall

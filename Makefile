all: vidal

FLAGS=`pkg-config --libs --cflags SDL2 GLEW libavcodec libavformat`
FLAGS+=-framework OpenGL

vidal: main.c shader.c quad.c texture.c
	clang -o $@.app $^ $(FLAGS) -g

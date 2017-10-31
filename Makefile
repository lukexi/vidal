all: veil

FLAGS=`pkg-config --libs --cflags SDL2 GLEW`
FLAGS+=-framework OpenGL

veil: main.c shader.c
	clang -o $@.app $^ $(FLAGS)

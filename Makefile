all: vital

FLAGS=`pkg-config --libs --cflags SDL2 GLEW libavcodec libavformat`
FLAGS+=-framework OpenGL

vital: main.c shader.c quad.c texture.c
	clang -o $@.app $^ $(FLAGS) -g

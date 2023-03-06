build:
	gcc ./src/*.c `sdl2-config --libs --cflags` -lm -o app

run:
	./app

gif:
	ffmpeg -i frames/frame-200.bmp -vf palettegen=128 palette.png
	ffmpeg -i frames/frame-%03d.bmp -i palette.png -filter_complex "fps=20,scale=600:-1:flags=lanczos[x];[x][1:v]paletteuse" output.gif

clean:
	rm app
	rm frames/*
	rm palette.png
	rm output.gif
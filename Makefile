all: pianoterm.c
	cc pianoterm.c -o pianoterm
run: all
	./pianoterm

CC=gcc
CFLAGS=-O2 -Wall

OBJS = main.o game.o fileio.o eval.o ai.o

all: chess

chess: $(OBJS)
	$(CC) $(CFLAGS) -o chess $(OBJS)

main.o: main.c structs.h game.h fileio.h ai.h eval.h
game.o: game.c game.h structs.h
fileio.o: fileio.c fileio.h structs.h
eval.o: eval.c eval.h structs.h
ai.o: ai.c ai.h structs.h game.h eval.h

clean:
	rm -f *.o chess
